#!/usr/bin/env python3
"""Compare the FM-1 flash layout (where the settings area starts) of two builds.

The settings area (VM) starts right after the firmware image, rounded up to
4 KB, so a size change can move it. An update that moves it from under the
settings saved on the device may be refused by the update loader, so warn.
usage: layout_check.py <stable report.json> <current report.json>
"""
import json, sys
def vm(path):
    rep = json.load(open(path, encoding='utf-8-sig'))
    found = {}
    def walk(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k in ('VM_START_ADDR', 'VM_END_ADDR', 'FLASH_BIN_SIZE', 'app_image_bytes') and isinstance(v, (int, float)): found[k] = int(v)
                walk(v)
        elif isinstance(o, list):
            for v in o: walk(v)
    walk(rep)
    if 'VM_START_ADDR' not in found and 'app_image_bytes' in found:
        n = found['app_image_bytes']; found['VM_START_ADDR'] = (0x4000 + 0x120 + n + 383 + 64 + 4095) // 4096 * 4096
    return found
a, b = vm(sys.argv[1]), vm(sys.argv[2])
sa, sb = a.get('VM_START_ADDR'), b.get('VM_START_ADDR')
print(f"::notice::Settings area starts at {sb} (current) vs {sa} (1.2); app image {b.get('app_image_bytes')} vs {a.get('app_image_bytes')}")
if sa != sb:
    print(f"::warning::The settings area moves from {sa} to {sb}: updating from 1.2 may be refused. Pad or trim the app to keep it at {sa}.")
