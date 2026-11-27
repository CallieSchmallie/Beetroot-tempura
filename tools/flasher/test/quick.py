src = open('run.py').read()
head = src[:src.index('    # 1. Normal install')]
keep = ['    # 1. Normal install', '    # 3. Loader fails', '    # 2. Cable pulled']
body = ''
for k in keep:
    i = src.index(k); j = src.index('    page.context.close()', i) + len('    page.context.close()\n')
    body += src[i:j] + '\n'
exec(compile(head + body + "    b.close()\nprint(json.dumps({'passed': sum(r[1] for r in results), 'total': len(results)}))\n", 'quick', 'exec'))
