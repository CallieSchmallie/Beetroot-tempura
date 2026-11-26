import sys, runpy, re
src = open('run.py').read()
start = src.index('    # 1b. Android'); end = src.index('    # 2. Cable pulled')
head = src[:src.index('    # 1. Normal install')]
tail = "    b.close()\n\nprint(json.dumps({'passed': sum(r[1] for r in results), 'total': len(results)}))\n"
exec(compile(head + src[start:end] + tail, 'only_android', 'exec'))
