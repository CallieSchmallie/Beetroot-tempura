"""Brick-risk scenarios for the Beetroot Tempura flasher page, against a mock FM-1."""
import json, sys, time, pathlib
from playwright.sync_api import sync_playwright
PAGE = pathlib.Path(sys.argv[1]).resolve().as_uri()
MOCK = pathlib.Path(__file__).with_name('mock_fm1.js').read_text()
results = []

def m(page, id): return page.eval_on_selector('#' + id, 'e => e.textContent')
def wait_text(page, id, pattern, timeout=60):
    import re
    end = time.time() + timeout
    while time.time() < end:
        t = m(page, id)
        if re.search(pattern, t): return t
        page.wait_for_timeout(200)
    return 'TIMEOUT: ' + m(page, id)
def ready(page):
    wait_text(page, 'm1', 'Firmware checked', 20)
def connect(page):
    page.click('#connect')
    page.wait_for_timeout(100)
    return wait_text(page, 'm2', r'^(?!Asking).+', 40)
def tick(page):
    for el in page.query_selector_all('.ack'): el.check()
def check(name, ok, detail):
    results.append((name, ok, detail)); print(('PASS ' if ok else 'FAIL ') + name + ' :: ' + detail[:300], flush=True)

with sync_playwright() as p:
    b = p.chromium.launch()
    def fresh(devices):
        ctx = b.new_context(); page = ctx.new_page(); page.add_init_script(MOCK)
        page.on('dialog', lambda d: d.dismiss())
        page.goto(PAGE); ready(page)
        for d in devices: page.evaluate('o => window.__fm1.add(o)', d)
        return page

    # 1. Normal install from a running Beetroot Tempura.
    page = fresh([{}])
    c = connect(page); tick(page)
    unload = page.evaluate('() => !!window.onbeforeunload')
    page.evaluate("() => document.getElementById('m4').textContent = ''"); page.click('#flash'); page.wait_for_timeout(300)
    guard = page.evaluate('() => !!window.onbeforeunload')
    done = wait_text(page, 'm4', r'Done\.|update mode|not visible|old firmware|Checking', 240)
    log = page.evaluate('() => window.__fm1.log'); wake = page.evaluate('() => window.__fm1.wake')
    build = page.evaluate('() => window.__fm1.dev(0).build === window.__fm1.buildId')
    check('normal install completes and verifies', 'Done.' in done and build, f'{c} | {done} | log={log}')
    check('leaving the page is guarded during install', guard and not unload and not page.evaluate('() => !!window.onbeforeunload'), f'during={guard}')
    check('screen kept awake during install, released after', wake[:1] == ['on'] and wake[-1:] == ['off'], f'wake={wake}')
    c2 = connect(page)
    check('same build again: nothing to install', 'already runs exactly this build' in c2, c2)
    page.context.close()

    # 2. Cable pulled at 40 % of the write, device stays in update mode.
    page = fresh([{}])
    connect(page); tick(page)
    page.evaluate('() => window.__fm1.opts.stopAt = 0.4')
    page.evaluate("() => document.getElementById('m4').textContent = ''"); page.click('#flash')
    diag = wait_text(page, 'm4', r'update mode and waiting|not visible|old firmware|Done\.', 300)
    check('interrupted write: page explains it is in update mode', 'update mode and waiting' in diag or 'not visible' in diag, diag)
    page.wait_for_timeout(2500)
    c = connect(page)
    resumed = 'ready to resume' in c
    tick(page); page.evaluate("() => document.getElementById('m4').textContent = ''"); page.click('#flash')
    done = wait_text(page, 'm4', r'Done\.|update mode|not visible|old firmware', 300)
    build = page.evaluate('() => window.__fm1.dev(0).build === window.__fm1.buildId')
    check('interrupted write: reconnect resumes and completes', resumed and 'Done.' in done and build, f'{c} | {done} | log={page.evaluate("() => window.__fm1.log")}')
    page.context.close()

    # 3. Loader fails to start; FM-1 comes back on its old firmware.
    page = fresh([{}])
    connect(page); tick(page)
    page.evaluate('() => window.__fm1.opts.failLoader = true')
    page.evaluate("() => document.getElementById('m4').textContent = ''"); page.click('#flash')
    diag = wait_text(page, 'm4', r'old firmware|update mode|not visible|Done\.', 300)
    check('loader did not start: nothing written, old firmware reported', 'old firmware' in diag and page.evaluate('() => window.__fm1.dev(0).build') == '11'*32, diag)
    page.context.close()

    # 4. Two FM-1s plugged in: refuse to guess.
    page = fresh([{}, {}])
    c = connect(page)
    check('two FM-1s: refuses to pick one', 'Connected' not in c, c)
    page.context.close()

    # 5. Only another MIDI device (a keyboard) plugged in.
    page = fresh([{'portName': 'KeyStep 37', 'silent': True}])
    c = connect(page)
    check('other MIDI device only: not treated as an FM-1', 'Connected' not in c, c)
    page.context.close()

    # 6. FM-1 running other firmware (stock or community): needs explicit opt-in.
    page = fresh([{'identity': 'SLOOP_FM1'}])
    c = connect(page)
    flash_off = page.eval_on_selector('#flash', 'e => e.disabled')
    check('other firmware: install blocked until "replace" is ticked', ('Found an FM-1 running' in c or 'Connected' not in c) and flash_off, c)
    page.context.close()

    # 7. Swap FM-1s between Connect and Install.
    page = fresh([{}])
    connect(page); tick(page)
    page.evaluate("() => { const d = window.__fm1.dev(0); d.identityOverride = 'FM1_COMMUNITY_7'; }")
    page.evaluate("() => document.getElementById('m4').textContent = ''"); page.click('#flash')
    diag = wait_text(page, 'm4', r'Done\.|Checking|update mode|not visible|old firmware|changed|identity', 60)
    check('device changed after Connect: install refused, nothing written', 'Done.' not in diag and page.evaluate('() => window.__fm1.log.every(l => !l.startsWith("arm"))'), diag)
    page.context.close()

    # 8. Cable pulled and FM-1 left unplugged.
    page = fresh([{}])
    connect(page); tick(page)
    page.evaluate("() => { window.__fm1.opts.stopAt = 0.7; const d = window.__fm1.dev(0); const r = d.restart.bind(d); d.restart = (mode, delay) => { d.plugged = false; window.__fm1.log.push('unplugged'); }; }")
    page.evaluate("() => document.getElementById('m4').textContent = ''"); page.click('#flash')
    diag = wait_text(page, 'm4', r'not visible|update mode|old firmware|Done\.', 300)
    check('FM-1 left unplugged: page says so and how to resume', 'not visible' in diag, diag)
    page.context.close()
    b.close()

print(json.dumps({'passed': sum(r[1] for r in results), 'total': len(results)}))
