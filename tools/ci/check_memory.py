"""Post-build memory and placement report for Beetroot Tempura.

Reads `llvm-nm --print-size sdk.elf` output and fails the build if
  * the boot-time heap is under 176 KB. 1.0 booted with 192 KB; 1.1 had
    89 KB and restarted endlessly on hardware before drawing anything. The
    SDK's own 48 KB minimum is not enough for this app's Bluetooth set-up, or
  * any Beetroot Tempura audio function sits outside internal SRAM
    (audio code must keep running while Flash is busy).
"""
import re, sys
SRAM = (0x01c00000, 0x01c80000)
AUDIO = ['tempura_render', 'tempura_cpu_report', 'tempura_live_feed', 'tempura_monitor_gain_q7',
         'tp_fx_render', 'tp_fx_clear', 'fm1_output_mix', 'fm1_audio_irq', 'block_used',
         'cache_try', 'tp_sea5_decode', 'render_grain', 'spawn', 'spawn_grains', 'reader_start',
         'process_events', 'note_on', 'note_off', 'update_voices', 'finish_emitters',
         'modulation', 'delay', 'reverb', 'grain_slot', 'nth_cell', 'lfo_shape', 'voice_release']
BUFFERS = ['cache_small', 'cache', 'live', 'dly', 'rev_mem', 'mod_line', 'window_table', 'grains', 'voices']
sym = {}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = re.match(r'^([0-9a-fA-F]+)\s+(?:([0-9a-fA-F]+)\s+)?([A-Za-z])\s+(\S+)', line)
    if m:
        sym.setdefault(m.group(4), (int(m.group(1), 16), int(m.group(2) or '0', 16), m.group(3)))
problems = []
heap = sym.get('_MALLOC_SIZE') or sym.get('MALLOC_SIZE')
if not heap: problems.append('heap size symbol not found')
else:
    kb = heap[0] / 1024
    print(f'::notice::Runtime heap for Bluetooth, USB and updates: {heap[0]} bytes ({kb:.1f} KB; SDK minimum 48 KB)')
    if heap[0] < 176 * 1024:
        problems.append(f'boot-time heap only {heap[0]} bytes ({kb:.1f} KB); 1.1 boot-looped at 89 KB, 1.0 booted at 192 KB. Need at least 176 KB.')
found = []
for name in AUDIO:
    if name in sym:
        addr = sym[name][0]
        found.append(name)
        if not (SRAM[0] <= addr < SRAM[1]): problems.append(f'audio function {name} at {addr:#x} is outside internal SRAM')
print('Audio functions checked in SRAM:', ', '.join(found) or 'none (inlined)')
for helper in ['__udivdi3', '__divdi3', '__umoddi3', '__moddi3']:
    if helper in sym and not (SRAM[0] <= sym[helper][0] < SRAM[1]):
        print(f'note: {helper} is in Flash; Beetroot Tempura audio code does not call it')
total = 0
for name in BUFFERS:
    if name in sym and sym[name][1]:
        total += sym[name][1]; print(f'  {name:14s} {sym[name][1]:7d} bytes')
print(f'::notice::Beetroot Tempura static buffers: {total} bytes ({total/1024:.0f} KB)')
for p in problems: print(f'::error::{p}')
sys.exit(1 if problems else 0)
