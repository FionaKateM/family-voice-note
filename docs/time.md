# Time

How the box knows the time, the timezone and whether it's quiet hours. Code is in `lib/app/src/app/time/`.

## The clock

The device sets its clock from internet time servers (NTP) and keeps it in UTC (`Core2Clock::startInternetTime`, called once Wi-Fi is up). The laptop build uses the laptop's own clock, which is already set.

Local time is worked out by the box's own code, not the C library. The C library on the ESP32 can only hold one timezone at a time, and the box needs several at once: its own for quiet hours, and each contact's for the asleep picture.

## Timezones and daylight saving

Each timezone has a rule like `GMT0BST,M3.5.0/1,M10.5.0`, which says: GMT normally, BST (one hour ahead) from 01:00 on the last Sunday in March to 02:00 on the last Sunday in October. `PosixTimeZone` reads these rules and works out the offset at any moment. The tests check every rule in the table against the laptop's C library, every 3 hours for four years and at every clock change.

The table of countries, cities and rules is `TimeZoneData.cpp`. It's made by a script from the IANA time zone database, the same one phones and computers use:

```bash
python3 scripts/make-timezones.py
```

It reads `/usr/share/zoneinfo` on the computer it runs on (Linux and Mac have it). The file records which release it was made from. Rerun it and release new firmware when a country changes its clocks; this happens a few times a year somewhere in the world, but rarely to any one family.

Only today's rules are kept, not history. Old names (for example `Europe/Kiev`) still work and lead to the current entry.

## Where the box is

`LocationService` asks ipinfo.io (or ipapi.co if that fails) where the box's internet connection is. This happens during setup, so the box can suggest its timezone (ticket 1.8), and again whenever the box joins a different Wi-Fi network from last time.

## Moving country

`MovedCountry` (in `lib/app/src/app/settings/`) remembers the last Wi-Fi network it checked on. On a new network, once the box's timezone has been set, it asks `LocationService` where it is. If that country isn't the one the box's timezone is in, every admin is asked "Looks like the box is now in France. Switch to France time? [Yes] [No]". No is remembered, so it never asks about that country again. The unanswered question is saved, so it still works after a restart.

On the laptop there's no real Wi-Fi network, so the desktop build pretends to be on one called "Laptop". Start it with `--wifi <another name>` to make it check again, as if it had moved. The check uses the laptop's real internet connection, so to see the question, first set the box's timezone in `/settings` to a different country from the one you're in.
