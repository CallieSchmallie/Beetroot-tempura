#!/usr/bin/env python3
"""Audio-interrupt stack depth, from the compiler's own frame sizes.

Recompiles the audio sources with the exact device flags from
compile_commands.json plus -Wframe-larger-than=0 (without LTO, which only changes
cross-file inlining), then adds up the deepest call chain the audio
interrupt can take through Beetroot Tempura. Run it for two builds to
compare a new version against one proven on hardware.

usage: stack_usage.py <build dir> [<label>]
"""
import json, os, re, subprocess, sys, tempfile

build = sys.argv[1]
label = sys.argv[2] if len(sys.argv) > 2 else build
commands = json.load(open(os.path.join(build, 'compile_commands.json'), encoding='utf-8-sig'))
wanted = ('tempura_engine.c', 'tempura_fx.c', 'audio.c')
frames = {}
out = tempfile.mkdtemp()
for c in commands:
    if not c['file'].replace('\\', '/').endswith(wanted):
        continue
    args, skip = [], False
    for a in c['arguments']:
        if skip: skip = False; continue
        if a in ('-o', '-MF'): skip = True; continue
        if a.startswith('-flto') or a == '-MMD': continue
        args.append(a)
    obj = os.path.join(out, os.path.basename(c['file']) + '.o')
    # The JieLi clang (4.0.1) has no -fstack-usage; -Wframe-larger-than=0
    # makes the backend report every function's frame size instead.
    args = [a for a in args if not a.startswith('-Werror')]
    r = subprocess.run(args + ['-fno-lto', '-Wframe-larger-than=0', '-o', obj], cwd=c['directory'], capture_output=True, text=True)
    if r.returncode:
        print(f'::warning::{label}: frame-size compile of {c["file"]} failed: {r.stderr[:400]}')
        continue
    found = 0
    for m in re.finditer(r"stack frame size of (\d+) bytes in function '([^']+)'", r.stderr):
        frames[m.group(2)] = (int(m.group(1)), 'static'); found += 1
    for m in re.finditer(r"stack frame size \((\d+)\) exceeds limit \(\d+\) in (?:function )?'([^']+)'", r.stderr):
        frames[m.group(2)] = (int(m.group(1)), 'static'); found += 1
    if not found:
        print(f'::warning::{label}: no frame sizes reported for {c["file"]}: {r.stderr[:300]}')

# The interrupt's call tree through Beetroot Tempura (callee lists; a missing
# function was inlined into its caller, so it adds nothing on its own).
tree = {
    'tempura_render': ['process_events', 'arp_block', 'update_voices', 'spawn', 'render_grain', 'finish_emitters', 'tp_fx_render', 'tp_fx_clear'],
    'process_events': ['arp_key', 'note_on', 'note_off', 'tp_fx_clear'],
    'arp_block': ['arp_clear', 'arp_release', 'tp_arp_sequence', 'note_on'],
    'arp_clear': ['note_off'], 'arp_release': ['note_off'],
    'tp_arp_sequence': ['arp_sort'],
    'spawn': ['reader_start', 'cache_try'], 'reader_start': ['cache_try'],
    'render_grain': ['cache_try', 'reader_next'], 'reader_next': ['cache_try'],
    'cache_try': ['tp_sea5_decode'],
    'tp_fx_render': ['modulation', 'delay', 'reverb'],
}
def depth(f, seen=()):
    if f in seen: return 0, [f + '(loop)']
    own = frames.get(f, (0, ''))[0]
    best, path = 0, []
    for g in tree.get(f, []):
        d, p = depth(g, seen + (f,))
        if d > best: best, path = d, p
    return own + best, [f'{f}={own}'] + path

total, path = depth('tempura_render')
live = frames.get('tempura_live_feed', (0, ''))[0]
irq = max((v[0] for k, v in frames.items() if 'audio' in k and 'irq' in k), default=0)
print(f'::notice::{label}: deepest audio-interrupt chain {total} bytes ({" > ".join(path)}); live feed {live}; irq handler {irq}')
dyn = [k for k, v in frames.items() if 'dynamic' in v[1] and k in tree]
if dyn: print(f'::warning::{label}: dynamic stack in {", ".join(dyn)}')
for k in sorted(frames, key=lambda k: -frames[k][0])[:12]:
    print(f'  {frames[k][0]:6d} {frames[k][1]:10s} {k}')
