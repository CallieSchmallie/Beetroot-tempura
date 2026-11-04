"""Print the largest input objects and symbols in a linker map (lld-style or GNU)."""
import re, sys, collections
text = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
by_obj, by_sym = collections.Counter(), []
cur_obj = None; cur_out = ''
for line in text:
    m = re.match(r'\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\d+)\s+(.*)$', line)  # lld: VMA LMA Size Align rest
    if m:
        size = int(m.group(3), 16); rest = m.group(5)
        if not rest.startswith(' ') and not rest.startswith('\t') and re.match(r'^\.', rest.strip()) and ':' not in rest: cur_out = rest.strip(); continue
        if ':(' in rest or '.o' in rest or '.a(' in rest:
            cur_obj = rest.strip()
            if cur_out.startswith(('.text', '.rodata', '.data', '.sdata', '.code', '.ram', '.flash')) or True:
                by_obj[re.sub(r':\(.*', '', cur_obj)] += size
        elif size:
            by_sym.append((size, rest.strip(), cur_obj))
        continue
    g = re.match(r'\s*(\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o\S*)', line)  # GNU: section addr size file
    if g: by_obj[g.group(4)] += int(g.group(3), 16)
print('::group::Largest objects')
for k, v in by_obj.most_common(30): print(f'{v:9d}  {k}')
print('::endgroup::')
print('::group::Largest symbols')
for size, sym, obj in sorted(by_sym, reverse=True)[:40]: print(f'{size:9d}  {sym}  [{obj}]')
print('::endgroup::')
top = ', '.join(f'{k.split("/")[-1]}={v//1024}K' for k, v in by_obj.most_common(8))
print(f'::notice::Largest objects: {top}')
