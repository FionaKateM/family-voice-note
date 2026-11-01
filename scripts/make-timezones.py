#!/usr/bin/env python3
"""Builds lib/app/src/app/time/TimeZoneData.cpp from the IANA time zone database.

    python3 scripts/make-timezones.py [/usr/share/zoneinfo]

The box can't carry the full time zone database, so it keeps a small table:

  - every country, with its name
  - for each country, one entry per distinct set of clock rules in use today,
    named after its main city (so the United States gets New York, Chicago,
    Denver, Phoenix, Los Angeles and so on, not all 29 IANA zones)
  - each entry's current rules as a POSIX TZ string, e.g.
    "GMT0BST,M3.5.0/1,M10.5.0", which says when daylight saving starts and ends
  - every other IANA name (including old ones like "Europe/Kiev") pointing at the
    entry it behaves the same as, so a name from the internet can be matched

Only today's rules are kept, not the history. That's all the box needs: it only
ever asks "what time is it there now?".

When a country changes its clocks, rerun this on a computer with up-to-date tzdata
and release new firmware.
"""

import os
import sys
from collections import OrderedDict

ZONEINFO = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/zoneinfo"
OUT = os.path.join(os.path.dirname(__file__), "..", "lib", "app", "src", "app", "time", "TimeZoneData.cpp")

# Names from iso3166.tab that read oddly in a button list.
COUNTRY_NAME_OVERRIDES = {
    "GB": "United Kingdom",
    "KR": "South Korea",
    "KP": "North Korea",
    "CD": "Congo (DR)",
    "CG": "Congo",
    "MM": "Myanmar",
    "SZ": "Eswatini",
    "TC": "Turks & Caicos Islands",
    "AS": "American Samoa",
    "WS": "Samoa",
}


def read_tab(name):
    with open(os.path.join(ZONEINFO, name), encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            yield line.rstrip("\n").split("\t")


def posix_rule(zone):
    """The POSIX TZ string at the end of a version 2+ TZif file."""
    with open(os.path.join(ZONEINFO, zone), "rb") as f:
        data = f.read()
    if not data.startswith(b"TZif") or data[4:5] < b"2":
        raise SystemExit(f"{zone}: not a version 2 or later TZif file")
    footer = data.rstrip(b"\n").rsplit(b"\n", 1)[-1].decode("ascii")
    if not footer:
        raise SystemExit(f"{zone}: no POSIX rule in file")
    return footer


def city_name(zone):
    return zone.rsplit("/", 1)[-1].replace("_", " ")


def tzdata_version():
    with open(os.path.join(ZONEINFO, "tzdata.zi"), encoding="utf-8") as f:
        first = f.readline().strip()
    return first.replace("# version", "").strip()


def links():
    """Old or alternative name -> current name, from tzdata.zi."""
    result = {}
    with open(os.path.join(ZONEINFO, "tzdata.zi"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("L "):
                _, target, name = line.split()
                result[name] = target
    return result


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    names = {row[0]: COUNTRY_NAME_OVERRIDES.get(row[0], row[1]) for row in read_tab("iso3166.tab")}

    # zonenow.tab names one well-known city for each set of rules in use today
    # (Sydney rather than Macquarie Island). Prefer those when naming an entry.
    well_known = {row[2] for row in read_tab("zonenow.tab")}

    # country -> rule -> zones with that rule, in zone.tab order.
    groups = OrderedDict()
    for row in read_tab("zone.tab"):
        country, zone = row[0], row[2]
        groups.setdefault(country, OrderedDict()).setdefault(posix_rule(zone), []).append(zone)

    by_country = OrderedDict()  # country -> rule -> the zone kept for it
    zone_to_kept = {}
    for country, rules in groups.items():
        kept_rules = by_country.setdefault(country, OrderedDict())
        for rule, members in rules.items():
            kept = next((z for z in members if z in well_known), members[0])
            kept_rules[rule] = kept
            for z in members:
                zone_to_kept[z] = kept

    for name, target in links().items():
        if name not in zone_to_kept and target in zone_to_kept:
            zone_to_kept[name] = zone_to_kept[target]

    countries = sorted(by_country, key=lambda c: names[c])
    zones = []  # (iana, city, rule, country)
    country_rows = []  # (code, name, first zone index, count)
    for code in countries:
        first = len(zones)
        # Cities in alphabetical order, as they'll appear in the picker.
        for rule, zone in sorted(by_country[code].items(), key=lambda item: city_name(item[1])):
            zones.append((zone, city_name(zone), rule, code))
        country_rows.append((code, names[code], first, len(zones) - first))

    index_of = {z[0]: i for i, z in enumerate(zones)}
    aliases = sorted((name, index_of[kept]) for name, kept in zone_to_kept.items() if name != kept)

    out = []
    out.append("// Made by scripts/make-timezones.py from tzdata " + tzdata_version() + ". Don't edit by hand.")
    out.append("")
    out.append('#include "app/time/TimeZoneData.h"')
    out.append("")
    out.append("namespace fvb {")
    out.append("namespace tzdata {")
    out.append("")
    out.append('const char* const kVersion = "' + tzdata_version() + '";')
    out.append("")
    out.append("const TimeZoneInfo kZones[] = {")
    for zone, city, rule, code in zones:
        out.append(f"    {{{c_string(zone)}, {c_string(city)}, {c_string(rule)}, {c_string(code)}}},")
    out.append("};")
    out.append(f"const size_t kZoneCount = {len(zones)};")
    out.append("")
    out.append("const CountryInfo kCountries[] = {")
    for code, name, first, count in country_rows:
        out.append(f"    {{{c_string(code)}, {c_string(name)}, {first}, {count}}},")
    out.append("};")
    out.append(f"const size_t kCountryCount = {len(country_rows)};")
    out.append("")
    out.append("// Sorted by name, for a binary search.")
    out.append("const ZoneAlias kAliases[] = {")
    for name, idx in aliases:
        out.append(f"    {{{c_string(name)}, {idx}}},")
    out.append("};")
    out.append(f"const size_t kAliasCount = {len(aliases)};")
    out.append("")
    out.append("}  // namespace tzdata")
    out.append("}  // namespace fvb")
    out.append("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print(f"{len(country_rows)} countries, {len(zones)} zones, {len(aliases)} other names -> {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
