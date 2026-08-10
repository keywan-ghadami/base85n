# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Short field-level samples of the kind that dominate real traffic.

Large files make ratios look tidy, but most encoded payloads in a real
system are small: an identifier, a phone number, a name, one record of
JSON. Those are also where a block encoding's fixed overhead and
rounding-up to whole groups hurt most, so they are worth measuring
separately rather than averaging away.

All values here are invented. Phone numbers use the ranges reserved for
fiction (+1-555-01xx in North America, +49-30-23125xx in Germany), the
addresses use example.com, and the identifiers are random.
"""

from __future__ import annotations

WIRE_SAMPLES: list[tuple[str, str]] = [
    # (label, value)
    ("first + last name", "Ada Lovelace"),
    ("name, umlauts", "Anna-Lena Müller-Schmidt"),
    ("customer number", "4711"),
    ("order number", "ORD-2026-0000184223"),
    ("hex value (8 byte)", "deadbeefcafebabe"),
    ("hex digest (SHA-256)",
     "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"),
    ("phone number, E.164", "+493023125190"),
    ("phone number, formatted", "+1 (415) 555-0132"),
    ("email address", "ada.lovelace@example.com"),
    ("URL", "https://api.example.com/v2/orders/184223?expand=items"),
    ("UUID v4", "b0f1c2d3-4e5a-4b6c-8d9e-0f1a2b3c4d5e"),
    ("ISO 8601 timestamp", "2026-08-10T07:12:44.318Z"),
    ("IPv4 address", "192.0.2.147"),
    ("IPv6 address", "2001:db8:85a3::8a2e:370:7334"),
    ("MAC address", "3c:5a:b4:0f:2e:91"),
    ("IBAN", "DE89370400440532013000"),
    ("currency amount", "1284.95 EUR"),
    ("CSV row",
     "184223,Ada Lovelace,+493023125190,1284.95,EUR,2026-08-10,shipped"),
    ("JSON record",
     '{"id":184223,"name":"Ada Lovelace","phone":"+493023125190",'
     '"total":1284.95,"currency":"EUR"}'),
    ("HTTP header block",
     "GET /v2/orders/184223 HTTP/1.1\r\n"
     "Host: api.example.com\r\n"
     "Accept: application/json\r\n"
     "User-Agent: acme-client/3.1.0\r\n\r\n"),
    ("JWT (3 segments)",
     "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
     "eyJzdWIiOiIxODQyMjMiLCJuYW1lIjoiQWRhIExvdmVsYWNlIiwiaWF0IjoxNzY3MjI1NjAwfQ."
     "3Yv1kQ8Zr7pNc2LxWmA4hTgKdF9sBvE0uJqRnXoYiPs"),
    ("log line",
     '2026-08-10T07:12:44Z INFO  order.service  order=184223 '
     'user=ada status=shipped duration_ms=41'),
    ("SQL statement",
     "SELECT id, name, total FROM orders WHERE customer_id = 184223 "
     "AND status = 'shipped' ORDER BY created_at DESC LIMIT 50"),
]


def as_bytes() -> list[tuple[str, bytes]]:
    return [(label, value.encode("utf-8")) for label, value in WIRE_SAMPLES]
