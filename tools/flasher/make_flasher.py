#!/usr/bin/env python3
"""Build the Beetroot Tempura USB flasher page from the SDK's updater.

  python tools/flasher/make_flasher.py
      -> page where the user picks the .fwsc and build-identity.json
  python tools/flasher/make_flasher.py --firmware build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc \\
      --identity build/dx7/build-identity.json --out site/index.html
      -> self-contained page with the firmware built in (nothing to upload)
"""
import argparse, base64, json, pathlib
root = pathlib.Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('--firmware'); ap.add_argument('--identity')
ap.add_argument('--notice', help='HTML that replaces the news notice (for test pages)')
ap.add_argument('--out', default=str(root / 'tools/flasher/beetroot-tempura-flasher.html'))
a = ap.parse_args()
page = (root / 'tools/flasher/flasher.html.in').read_text(encoding='utf-8')
code = (root / 'sdk/WebUpdate.js').read_text(encoding='utf-8')
samples = (root / 'sdk/Samples.js').read_text(encoding='utf-8')
# Keep only the bank-format helpers; the rest of Samples.js drives M-VAVE's workbench page.
cut = samples.index('globalThis.FM1Samples=')
samples = samples[:samples.index('\n', cut) + 1] + '})();\n'
library = (root / 'tools/flasher/sound_library.json').read_text(encoding='utf-8')
dqt = (root / 'tools/flasher/sea_dqt.json').read_text(encoding='utf-8')
sea5 = (root / 'tools/flasher/sea5.js').read_text(encoding='utf-8')
if a.notice:
    start = page.index('<section class="step notice" id="news">')
    end = page.index('</section>', start) + len('</section>')
    page = page[:start] + pathlib.Path(a.notice).read_text(encoding='utf-8').strip() + page[end:]
embedded = 'null'
if a.firmware:
    fw = pathlib.Path(a.firmware).read_bytes()
    ident = json.loads(pathlib.Path(a.identity).read_text(encoding='utf-8-sig'))
    embedded = json.dumps({'name': 'Beetroot Tempura ' + str(ident.get('release_version', '')),
                           'identity': {'build_id': ident['build_id'], 'release_version': ident.get('release_version', '')},
                           'fwsc': base64.b64encode(fw).decode()})
out = pathlib.Path(a.out); out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(page.replace('__WEBUPDATE__', code).replace('__SAMPLES__', samples).replace('__EMBEDDED__', embedded).replace('__LIBRARY__', library).replace('__DQT__', dqt).replace('__SEA5__', sea5), encoding='utf-8')
print(out)
