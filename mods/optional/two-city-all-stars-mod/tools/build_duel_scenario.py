"""Author the duel once, offline, as an actual CE native scenario container."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
from native_scenario_codec import unpack, repack

BASE_SHA256 = '79f11f0ccefc2bc4752163941d5fd7b9b7ccf9cf8e952aa00f226c7c4030c93c'
PREFECTURES = {15: [99, 102, 106, 108, 109, 111], 16: [113, 115, 116, 118, 119, 120]}

def author(reference: bytes, definition: dict) -> tuple[bytes, dict]:
    # All offsets are offline file layout, never process addresses. Refuse other
    # revisions rather than applying this layout to an unrelated save/scenario.
    if hashlib.sha256(reference).hexdigest() != BASE_SHA256:
        raise ValueError('Expected the attested original CE scenario 01')
    offset, raw = unpack(reference)
    data = bytearray(raw)
    def put(at, fmt, *values):
        struct.pack_into('<' + fmt, data, at, *values)
    def text(at, count):
        return raw[at:at + count * 2].decode('utf-16le').split('\0', 1)[0]
    d = definition['campaign']
    names = {}
    for slot in range(1400):
        at = 44884 + slot * 372
        name = text(at + 52, 9) + text(at + 70, 9)
        if name:
            names[name] = 0 if name in names else slot + 1
    sides = d['sides']
    rosters = [[names[n] for n in s['officers']] for s in sides]
    if not all(person for roster in rosters for person in roster):
        raise ValueError('Ambiguous required native officer')
    rulers = [names[s['ruler']] for s in sides]
    advisors = [names[s['advisor']] for s in sides]
    members = {person: side for side, roster in enumerate(rosters) for person in roster}
    # These arrays include the native zero sentinel. Their counts stay intact.
    for force in range(1, 52):
        at = 5178 + force * 329
        put(at, 'H', rulers[force - 1] if force <= 2 else 0)
        data[at + 2:at + 54] = bytes(100 if other == force else 50 for other in range(52))
        put(at + 54, 'B', 9)
        put(at + 59, 'H', advisors[force - 1] if force <= 2 else 0)
    # Corps source-force/number/leader records; native rebuilding allocates the
    # first two physical corps and reconstructs its own cached force lists.
    for corps in range(52):
        put(22294 + corps * 4, 'BBH', 0, 0, 0)
    for side in range(2):
        put(22294 + (side + 1) * 4, 'BBH', side + 1, 1, rulers[side])
    for person in range(1, 1401):
        at = 44884 + (person - 1) * 372
        side = members.get(person)
        active = side is not None
        # Keep original unavailable special officers absent from authored scenarios.
        if not active and raw[at + 269] == 0:
            continue
        city = sides[side]['city'] if active else 0
        put(at + 264, 'BHHBBB', side + 1 if active else 0, city, city,
            (1 if person == rulers[side] else 4) if active else 9, 73, 100)
        if active and not d['rules']['natural_death']:
            put(at + 329, 'H', 65535)
    defaults = d['city_defaults']
    for city in range(1, 52):
        side = next((i for i, s in enumerate(sides) if s['city'] == city), None)
        at = 3746 + city * 23
        data[at:at + 23] = bytes(23)
        if side is not None:
            put(at, 'HBIIIB', rulers[side], side + 1, defaults['gold'], defaults['food'], defaults['troops'], defaults['morale'])
            put(at + 20, 'B', defaults['size'])
        building = 26534 + city * 6
        put(building + 3, 'B', side + 1 if side is not None else 0)
        if side is not None:
            put(building + 4, 'H', defaults['durability'])
    # Empty gates and every non-city building owner, preserving location/type.
    for gate in range(1, 11):
        at = 4946 + gate * 20
        data[at:at + 20] = bytes(20)
    for building in range(52, 3001):
        put(26534 + building * 6 + 3, 'B', 0)
    prefectures = [PREFECTURES[s['city']] for s in sides]
    owners = {area: side + 1 for side, rows in enumerate(prefectures) for area in rows}
    for area in range(1, 501):
        at = 22518 + area * 8
        put(at, 'HBBB', d['prefecture_defaults']['population'] if area in owners else 0,
            d['prefecture_defaults']['public_order'], owners.get(area, 0), 0)
    # CScenarioData's fixed 9-code-unit caption and native five-byte dates.
    caption = definition['scenario']['name'].encode('utf-16le')
    data[771830:771848] = caption.ljust(18, b'\0')
    for at in (771863, 771868):
        put(at, 'HBBB', d['start_year'], 1, 1, 0)
    header = bytearray(reference)
    header[20:422] = definition['scenario']['description'].encode('utf-16le').ljust(402, b'\0')
    header[422:440] = caption.ljust(18, b'\0')
    struct.pack_into('<H', header, 456, d['start_year'])
    result = repack(bytes(header), bytes(data))
    report = {'base_sha256': BASE_SHA256, 'sha256': hashlib.sha256(result).hexdigest(),
              'bytes': len(result), 'payload_bytes': len(data), 'rosters': rosters,
              'rulers': rulers, 'advisors': advisors, 'prefectures': prefectures,
              'terrain': 'unchanged', 'native_initialization': True}
    return result, report

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('definition', type=Path)
    p.add_argument('-o', '--output', type=Path, required=True)
    a = p.parse_args()
    result, report = author(a.base.read_bytes(), json.loads(a.definition.read_text('utf-8')))
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(result)
    a.output.with_suffix('.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=True))

if __name__ == '__main__':
    main()
