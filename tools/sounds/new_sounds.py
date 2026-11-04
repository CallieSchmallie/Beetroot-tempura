"""Twenty more original textures for the Beetroot Tempura sound library.
All synthesized from scratch: 2.4 s, mono, 32 kHz. Writes <name>.raw (int16 LE)."""
import numpy as np, os, sys
from scipy.signal import lfilter
SR = 32000; N = int(2.4 * SR); t = np.arange(N) / SR
rng = np.random.default_rng(2026)
def norm(x, p=0.85): return x / (np.max(np.abs(x)) + 1e-9) * p
def fade(x, a=0.01, r=0.12):
    e = np.ones_like(x); na, nr = max(1, int(a*SR)), int(r*SR)
    e[:na] = np.linspace(0, 1, na); e[-nr:] *= np.linspace(1, 0, nr); return x * e
def lp(x, fc):
    a = np.exp(-2*np.pi*fc/SR); return lfilter([1-a], [1, -a], x)
def hp(x, fc): return x - lp(x, fc)
def lp_var(x, fc):
    """One-pole low-pass with a time-varying cutoff."""
    a = np.exp(-2*np.pi*np.asarray(fc)/SR); y = np.empty_like(x); s = 0.0
    for i in range(len(x)): s = (1 - a[i])*x[i] + a[i]*s; y[i] = s
    return y
def reson(x, f, bw):
    r = np.exp(-np.pi*bw/SR); th = 2*np.pi*f/SR
    return lfilter([1-r], [1, -2*r*np.cos(th), r*r], x)
def mtof(m): return 440 * 2**((m-69)/12)
def at(x, start, sig):
    s0 = int(start*SR); n = min(len(sig), N - s0)
    if n > 0: x[s0:s0+n] += sig[:n]
def env_exp(n, k): return np.exp(-np.arange(n)/SR * k)
def ks(f, dur, damp=0.996, bright=0.5):
    """Karplus-Strong plucked string."""
    n = int(dur*SR); p = max(2, int(SR/f)); buf = rng.uniform(-1, 1, p); buf = lp(buf, 2000 + 8000*bright)
    out = np.zeros(n)
    for i in range(n):
        j = i % p; out[i] = buf[j]; buf[j] = damp * 0.5 * (buf[j] + buf[(j+1) % p])
    return out
def saw(freq, phase=0.0):
    ph = np.cumsum(np.broadcast_to(freq, (N,)) / SR) + phase; return 2*(ph % 1) - 1
S = {}

# 1 MARIMBA phrase
x = np.zeros(N)
for i, m in enumerate([60, 64, 67, 72, 69, 65, 62, 67, 71, 74]):
    f = mtof(m); n = int(1.2*SR); tt = np.arange(n)/SR
    note = (np.sin(2*np.pi*f*tt) + 0.25*np.sin(2*np.pi*f*3.93*tt)*np.exp(-tt*25) + 0.1*np.sin(2*np.pi*f*9.2*tt)*np.exp(-tt*60)) * np.exp(-tt*6)
    at(x, i*0.22, note * (0.75 + 0.25*rng.random()))
S['MARIMBA'] = fade(norm(x), 0.001)
# 2 MUSIC BOX
x = np.zeros(N)
for i, m in enumerate([84, 88, 91, 86, 89, 93, 84, 96, 91, 88, 84]):
    f = mtof(m); n = int(1.6*SR); tt = np.arange(n)/SR
    note = (np.sin(2*np.pi*f*tt) + 0.4*np.sin(2*np.pi*f*4.1*tt)*np.exp(-tt*12)) * np.exp(-tt*2.8)
    at(x, i*0.2 + 0.01*rng.random(), note)
S['MUSIC BOX'] = fade(norm(x), 0.001)
# 3 BOWED METAL: inharmonic partials with slow bowing swell
x = np.zeros(N)
for k, ratio in enumerate([1, 2.76, 5.40, 8.93, 13.34]):
    f = 220*ratio; x += np.sin(2*np.pi*f*t + rng.random()*6) * (0.6**k) * (0.7 + 0.3*np.sin(2*np.pi*(0.3+k*0.17)*t))
x *= np.minimum(1, t/0.8); x += 0.02*lp(rng.standard_normal(N), 3000)
S['BOWED METAL'] = fade(norm(x), 0.2, 0.3)
# 4 WARM PAD: detuned saws through a slow low-pass, Fmaj9
x = np.zeros(N)
for m in [41, 53, 57, 60, 64, 67]:
    for d in (-0.07, 0, 0.07): x += saw(mtof(m)*2**(d/12), rng.random())
x = lp(lp(x, 900), 1400) * np.minimum(1, t/0.6)
S['WARM PAD'] = fade(norm(x), 0.05, 0.3)
# 5 BREATHY FLUTE: sine melody + breath noise
f = np.interp(t, [0, .5, .55, 1.1, 1.15, 1.7, 1.75, 2.4], [mtof(74)]*2 + [mtof(76)]*2 + [mtof(79)]*2 + [mtof(77)]*2)
f *= 1 + 0.008*np.sin(2*np.pi*5*t) * np.minimum(1, t)
ph = np.cumsum(f/SR); tone = np.sin(2*np.pi*ph) + 0.12*np.sin(4*np.pi*ph)
air = reson(rng.standard_normal(N), 2500, 1800) * 0.08
S['FLUTE'] = fade(norm(tone*np.minimum(1, t/0.15) + air), 0.08, 0.3)
# 6 LOW CELLO: bowed saw with formant body
f = mtof(43) * (1 + 0.005*np.sin(2*np.pi*5.5*t))
src = saw(f) + 0.05*rng.standard_normal(N)
x = reson(src, 280, 120) + 0.7*reson(src, 600, 200) + 0.3*reson(src, 1400, 400)
S['LOW CELLO'] = fade(norm(x*np.minimum(1, t/0.3)), 0.1, 0.3)
# 7 GONG: dense inharmonic decay with shimmer
x = np.zeros(N)
for k in range(30):
    fk = 90 * (1 + k*0.61 + rng.random()*0.3)**1.18
    x += np.sin(2*np.pi*fk*t + rng.random()*6) * np.exp(-t*(0.6 + 0.08*k)) * (1 - np.exp(-t*(3 + 0.4*k))) / (1 + 0.15*k)
S['GONG'] = fade(norm(x), 0.002, 0.2)
# 8 RAIN: drops on noise bed
x = lp(hp(rng.standard_normal(N), 800), 6000) * 0.15
for _ in range(220):
    s0 = rng.uniform(0, 2.35); f0 = rng.uniform(1500, 4500); n = int(0.03*SR); tt = np.arange(n)/SR
    at(x, s0, np.sin(2*np.pi*f0*(1 + 8*tt)*tt) * np.exp(-tt*160) * rng.uniform(0.2, 1))
S['RAIN'] = fade(norm(x), 0.2, 0.3)
# 9 OCEAN: noise swells
n = rng.standard_normal(N)
sw = (0.5 - 0.5*np.cos(2*np.pi*t/1.2))**1.5           # two waves
x = lp_var(lp_var(n, 250 + 1600*sw), 300 + 2000*sw) * (0.15 + 0.85*sw) + 0.04*hp(n, 5000)*sw**2
S['OCEAN'] = fade(norm(x), 0.3, 0.3)
# 10 BIRDS: FM chirps
x = np.zeros(N)
for _ in range(14):
    s0 = rng.uniform(0, 2.2); n = int(rng.uniform(0.05, 0.16)*SR); tt = np.arange(n)/SR
    f0 = rng.uniform(2500, 5000); sweep = f0 * (1 + rng.uniform(-0.4, 0.6)*tt/tt[-1])
    ph = np.cumsum(sweep/SR) + 0.02*np.sin(2*np.pi*rng.uniform(30, 80)*tt)
    at(x, s0, np.sin(2*np.pi*ph) * np.sin(np.pi*np.arange(n)/n)**2 * rng.uniform(0.4, 1))
x += 0.03*lp(rng.standard_normal(N), 1500)
S['BIRDS'] = fade(norm(x), 0.05, 0.2)
# 11 TAPE KEYS: warbling electric-piano chords
x = np.zeros(N); wob = 1 + 0.004*np.sin(2*np.pi*0.7*t) + 0.002*np.sin(2*np.pi*5.3*t)
for i, chord in enumerate([[50, 57, 62, 65, 69], [48, 55, 60, 64, 67], [45, 52, 57, 60, 64]]):
    s0 = int(i*0.8*SR); tt = np.arange(N - s0)/SR
    for m in chord:
        f = mtof(m)*wob[s0:]; ph = np.cumsum(f/SR)
        x[s0:] += (np.sin(2*np.pi*ph + 1.2*np.exp(-tt*4)*np.sin(2*np.pi*ph)) * np.exp(-tt*1.8))
S['TAPE KEYS'] = fade(norm(lp(x, 5000)), 0.002, 0.15)
# 12 ORGAN: additive drawbars, C minor 7
x = np.zeros(N)
for m in [48, 55, 58, 63, 67]:
    f = mtof(m)
    for h, a in [(0.5, .5), (1, 1), (2, .6), (3, .4), (4, .3), (6, .15), (8, .12)]: x += a*np.sin(2*np.pi*f*h*t + h)
x *= 1 + 0.15*np.sin(2*np.pi*6.4*t)
S['ORGAN'] = fade(norm(x), 0.02, 0.2)
# 13 HARP GLISS
x = np.zeros(N)
for i, m in enumerate([55, 57, 59, 62, 64, 67, 69, 71, 74, 76, 79, 81, 83, 86]):
    at(x, 0.05 + i*0.07, ks(mtof(m), 2.2, 0.9985, 0.35))
S['HARP'] = fade(norm(x), 0.001)
# 14 BASS PLUCK
x = np.zeros(N)
for i, m in enumerate([33, 33, 40, 36, 38, 31]):
    at(x, i*0.4, lp(ks(mtof(m), 0.6, 0.994, 0.2), 1800))
S['BASS PLUCK'] = fade(norm(x), 0.001)
# 15 GUITAR STRUM
x = np.zeros(N)
for s, (start, chord) in enumerate([(0.0, [40, 47, 52, 56, 59, 64]), (1.2, [45, 52, 57, 61, 64, 69])]):
    for i, m in enumerate(chord): at(x, start + i*0.018, ks(mtof(m), 1.3, 0.997, 0.6))
S['GUITAR'] = fade(norm(x), 0.001)
# 16 BELL TREE: descending sparkles
x = np.zeros(N)
for i in range(28):
    f = 5200 * 0.94**i; n = int(0.9*SR); tt = np.arange(n)/SR
    upper = 0.5*np.sin(2*np.pi*f*2.7*tt) if f*2.7 < 14000 else 0
    at(x, i*0.055, (np.sin(2*np.pi*f*tt) + upper) * np.exp(-tt*5))
S['BELL TREE'] = fade(norm(x), 0.001, 0.4)
# 17 RADIO: static, tuning whistles, a far-off tone
n = rng.standard_normal(N)
x = reson(n, 1800, 2500)*0.3 * (0.5 + 0.5*np.abs(np.sin(2*np.pi*1.3*t)))
x += 0.3*np.sin(2*np.pi*np.cumsum(np.interp(t, [0, 1, 2.4], [3000, 900, 1400]))/SR)
x += 0.25*np.sign(np.sin(2*np.pi*mtof(69)*t)) * (t > 1.4)
S['RADIO'] = fade(norm(lp(x, 4500)), 0.02, 0.2)
# 18 DRUM LOOP: kick, snare, hats at 100 BPM
x = np.zeros(N); beat = 0.6
def kick():
    n = int(0.35*SR); tt = np.arange(n)/SR; return np.sin(2*np.pi*np.cumsum(50 + 120*np.exp(-tt*30))/SR) * np.exp(-tt*9)
def snare():
    n = int(0.25*SR); tt = np.arange(n)/SR
    return 0.6*hp(rng.standard_normal(n), 1500)*np.exp(-tt*18) + 0.4*np.sin(2*np.pi*190*tt)*np.exp(-tt*25)
def hat():
    n = int(0.06*SR); tt = np.arange(n)/SR; return 0.25*hp(rng.standard_normal(n), 7000)*np.exp(-tt*70)
for b in range(4):
    at(x, b*beat, kick() if b % 2 == 0 else snare())
    for h in range(2): at(x, b*beat + h*beat/2, hat())
at(x, 1.5*beat, 0.6*kick())
S['DRUM LOOP'] = fade(norm(x), 0.001, 0.05)
# 19 PULSE: sub heartbeat with a filtered tick
x = np.zeros(N)
for b in [0, 0.28, 1.2, 1.48]:
    n = int(0.3*SR); tt = np.arange(n)/SR
    at(x, b, np.sin(2*np.pi*55*tt)*np.exp(-tt*12) + 0.2*hp(rng.standard_normal(n), 3000)*np.exp(-tt*200))
x += 0.15*np.sin(2*np.pi*41*t)*(0.5 + 0.5*np.sin(2*np.pi*0.42*t))
S['PULSE'] = fade(norm(x), 0.001, 0.2)
# 20 OOH CHOIR: low "oo" vowel, C major
x = np.zeros(N)
for m in [48, 52, 55, 60]:
    for d in (-0.1, 0.1):
        f = mtof(m)*2**(d/12)*(1 + 0.005*np.sin(2*np.pi*5*t + rng.random()*6)); x += saw(f, rng.random())
x = reson(x, 300, 80) + 0.6*reson(x, 870, 100) + 0.2*reson(x, 2250, 200)
S['OOH CHOIR'] = fade(norm(lp(x, 3000)), 0.3, 0.4)

def finish(x):
    """Remove DC, then bring every sound to a similar loudness without clipping."""
    x = hp(x, 25.0)
    x = x / (np.sqrt(np.mean(x**2)) + 1e-9) * 0.2
    peak = np.max(np.abs(x))
    if peak > 0.9: x = np.tanh(x / peak * 1.4) / np.tanh(1.4) * 0.9
    return x
S = {k: finish(v) for k, v in S.items()}
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
for name, x in S.items():
    assert len(x) == N and np.all(np.isfinite(x)), name
    (np.clip(x, -1, 1)*30000).astype('<i2').tofile(os.path.join(out, name.replace(' ', '_') + '.raw'))
print(len(S), 'sounds:', ', '.join(S))
