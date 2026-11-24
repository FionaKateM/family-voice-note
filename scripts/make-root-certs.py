#!/usr/bin/env python3
"""Makes lib/platform_core2/src/platform/core2/RootCertificates.h.

The device checks every HTTPS server it talks to against a short list of
trusted root certificates. This script picks those roots out of Mozilla's
list (the same list browsers use) so the header can be remade when a service
changes who issues its certificates.

    python3 scripts/make-root-certs.py <mozilla-roots.pem>

Mozilla's list as a PEM file: https://curl.se/ca/cacert.pem, or
components/mbedtls/esp_crt_bundle/cacrt_all.pem in the ESP-IDF repo.
Needs the `cryptography` package (pip install cryptography).

Who uses which root (checked November 2026):
  api.telegram.org                      Go Daddy (Starfield as a backup)
  github.com, api.github.com            Sectigo / USERTrust
  release downloads (*.githubusercontent.com)   DigiCert
  ipapi.co, ipinfo.io (location guess)  Google Trust Services, Let's Encrypt, SSL.com
Amazon is included because several of these sit behind AWS at times.
"""

import re
import sys
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import serialization

WANTED = [
    "Go Daddy Root Certificate Authority - G2",
    "Starfield Root Certificate Authority - G2",
    "USERTrust ECC Certification Authority",
    "USERTrust RSA Certification Authority",
    "Sectigo Public Server Authentication Root E46",
    "Sectigo Public Server Authentication Root R46",
    "DigiCert Global Root G2",
    "DigiCert Global Root G3",
    "ISRG Root X1",
    "ISRG Root X2",
    "GTS Root R1",
    "GTS Root R3",
    "GTS Root R4",
    "Amazon Root CA 1",
    "SSL.com Root Certification Authority RSA",
    "SSL.com Root Certification Authority ECC",
]

OUT = Path(__file__).resolve().parent.parent / "lib/platform_core2/src/platform/core2/RootCertificates.h"


def common_name(cert):
    names = [a.value for a in cert.subject if a.oid == x509.NameOID.COMMON_NAME]
    return names[0] if names else ""


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    text = Path(sys.argv[1]).read_text()
    found = {}
    for pem in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", text, re.S):
        cert = x509.load_pem_x509_certificate(pem.encode())
        name = common_name(cert)
        if name in WANTED and name not in found:
            found[name] = cert

    missing = [n for n in WANTED if n not in found]
    if missing:
        sys.exit("Not in that file: " + ", ".join(missing))

    lines = [
        "#pragma once",
        "",
        "// Made by scripts/make-root-certs.py. Don't edit by hand.",
        "//",
        "// The root certificates the device trusts for HTTPS. See the script for",
        "// which service uses which, and how to remake this file.",
        "",
        "namespace fvb {",
        "",
        "static const char kRootCertificates[] =",
    ]
    for name in WANTED:
        cert = found[name]
        lines.append(f"    // {name} (valid until {cert.not_valid_after_utc.date()})")
        pem = cert.public_bytes(serialization.Encoding.PEM).decode()
        for row in pem.strip().splitlines():
            lines.append(f'    "{row}\\n"')
    lines[-1] = lines[-1] + ";"
    lines += ["", "}  // namespace fvb", ""]
    OUT.write_text("\n".join(lines))
    print(f"Wrote {len(WANTED)} certificates to {OUT}")


if __name__ == "__main__":
    main()
