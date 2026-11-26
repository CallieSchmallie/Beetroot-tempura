// A mock FM-1 behind a fake Web MIDI, speaking M-VAVE's update protocol as
// WebUpdate.js expects it. Installed with page.addInitScript before the page
// loads. The test drives it through window.__fm1.
(() => {
  const OPEN = 'FM1_OFFICIAL_OPEN';
  const T = window.__fm1 = {
    log: [], wake: [], devices: [], image: null, buildId: null,
    opts: { failLoader: false, stopAt: -1 },
  };
  // Synthetic package: the page's real updater runs unchanged; only the
  // parsing of the embedded .fwsc (M-VAVE's code, checked in CI) is replaced.
  let real;
  Object.defineProperty(globalThis, 'FM1Web', {
    configurable: true,
    get() { return real; },
    set(v) {
      real = v;
      v.inspectPackage = () => ({ product: OPEN, loader: new Uint8Array(16) });
      v.prepare = async meta => {
        const n = 453064, img = new Uint8Array(n);
        let x = 12345; for (let i = 0; i < n; i++) { x = (x * 1103515245 + 12345) >>> 0; img[i] = x >>> 24; }
        T.image = img; T.buildId = meta.build_id;
        return { ...meta, product: OPEN, image: img, loaderIdentity: 'ota-FM1C_' + meta.build_id.slice(0, 16) };
      };
    },
  });
  // Screen wake lock, recorded.
  Object.defineProperty(navigator, 'wakeLock', { configurable: true, value: {
    request: async () => { T.wake.push('on'); const s = new EventTarget(); s.release = async () => { T.wake.push('off'); s.dispatchEvent(new Event('release')); }; return s; },
  } });

  class Port extends EventTarget {
    constructor(dev, type) { super(); this.dev = dev; this.type = type; this.manufacturer = 'M-VAVE'; this.connection = 'closed'; this.id = dev.id + type; }
    get name() { return this.dev.portName; }
    get state() { return this.dev.plugged ? 'connected' : 'disconnected'; }
    async open() { if (!this.dev.plugged) throw new DOMException('gone', 'InvalidStateError'); this.connection = 'open'; return this; }
    async close() { this.connection = 'closed'; return this; }
    send(bytes) { if (!this.dev.plugged) throw new DOMException('gone', 'InvalidStateError'); const b = Uint8Array.from(bytes); setTimeout(() => this.dev.receive(b), 0); }
  }
  const access = new EventTarget();
  access.inputs = new Map(); access.outputs = new Map();
  const refresh = () => {
    access.inputs.clear(); access.outputs.clear();
    for (const d of T.devices) { access.inputs.set(d.in.id, d.in); access.outputs.set(d.out.id, d.out); }
  };
  navigator.requestMIDIAccess = async () => { refresh(); return access; };

  class Device {
    constructor(id, opts = {}) {
      this.id = id; this.plugged = true; this.mode = opts.mode || 'app';
      this.portName = opts.portName || 'FM-1'; this.identityOverride = opts.identity || null; this.silent = !!opts.silent;
      this.build = opts.build || '11'.repeat(32); this.written = null; this.got = 0; this.armed = null; this.seq = 0;
      this.in = new Port(this, 'input'); this.out = new Port(this, 'output');
    }
    identity() {
      if (this.identityOverride) return this.identityOverride;
      return this.mode === 'loader' ? 'ota-FM1C_' + (this.armed ? this.armed.tag : T.buildId.slice(0, 16)) : OPEN;
    }
    reply(cmd, data) { if (!this.plugged) return; const f = real.frame(cmd, Array.from(data)); this.in.dispatchEvent(Object.assign(new Event('midimessage'), { data: f })); }
    request(addr, n) { this.seq = (this.seq + 1) & 255; this.reply(48, [this.seq, addr & 255, (addr >>> 8) & 255, (addr >>> 16) & 255, (addr >>> 24) & 255, n & 255, (n >>> 8) & 255, (n >>> 16) & 255]); }
    restart(mode, delay = 800) {
      this.plugged = false; this.in.dispatchEvent(new Event('statechange'));
      T.log.push('restart->' + mode);
      setTimeout(() => { this.mode = mode; this.plugged = true; refresh(); }, delay);
    }
    receive(b) {
      if (!this.plugged || this.silent) return;
      if (b.length === 6 && b[1] === 34 && b[2] === 36 && b[3] === 53) return this.go();
      const p = real.parse(b); if (!p) return;
      if (p.command === 17) { const id = new TextEncoder().encode(this.identity()), r = new Uint8Array(27); r.set(id); return this.reply(17, r); }
      if (p.command === 49 && this.mode === 'app') {
        const r = new Uint8Array(47); r.set(new TextEncoder().encode('FMC1')); r[4] = 1; r[5] = 7; r[6] = 0; r[7] = 1;
        r[8] = 0; r[9] = 2; r[10] = 0; r[11] = 0; r[12] = 16; r[13] = 0;
        for (let i = 0; i < 32; i++) r[14 + i] = parseInt(this.build.substr(i * 2, 2), 16);
        return this.reply(49, r);
      }
      if (p.command === 50 && this.mode === 'app') {
        const d = p.data, n = d[2] | d[3] << 8 | d[4] << 16 | d[5] << 24;
        const tag = Array.from(d.subarray(6, 38), x => x.toString(16).padStart(2, '0')).join('');
        const ok = d[0] === 1 && d[1] === 7 && n === T.image.length && tag === T.buildId;
        this.armed = ok ? { n, tag: tag.slice(0, 16), full: tag } : null;
        T.log.push('arm ' + (ok ? 'ok' : 'refused'));
        return this.reply(50, [ok ? 0 : 1]);
      }
      if (p.command === 48) return this.onData(p.data);
    }
    go() {
      if (this.mode === 'app') {
        if (!this.armed) return;
        this.phase = 'prepare'; this.request(0xe0000000, 8);
      } else if (this.mode === 'loader') {
        this.phase = 'write'; this.got = 0; this.written = new Uint8Array(T.image.length); this.next();
      }
    }
    next() {
      if (!this.plugged) return;
      if (T.opts.stopAt >= 0 && this.got >= T.opts.stopAt * T.image.length) { T.opts.stopAt = -1; this.restart('loader', 1500); T.log.push('cable pulled at ' + this.got); return; }
      if (this.got >= T.image.length) return this.request(0xf0000000, 8);
      this.request(this.got, Math.min(512, T.image.length - this.got));
    }
    onData(d) {
      const addr = d[1] | d[2] << 8 | d[3] << 16 | (d[4] << 24 >>> 0), n = d[5] | d[6] << 8 | d[7] << 16;
      if (addr === 0xe0000000 >>> 0 || addr === (0xe0000000 | 0)) {
        if (T.opts.failLoader) { this.armed = null; return this.restart('app'); }   // loader did not start: old firmware again
        return this.restart('loader');
      }
      if (addr === 0xf0000000 >>> 0 || addr === (0xf0000000 | 0)) {
        let same = this.written && this.written.length === T.image.length;
        for (let i = 0; same && i < T.image.length; i++) same = this.written[i] === T.image[i];
        T.log.push('write finished, image ' + (same ? 'matches' : 'DIFFERS'));
        if (same) { this.build = this.armed ? this.armed.full : T.buildId; this.armed = null; this.restart('app'); }
        else this.restart('loader');
        return;
      }
      const payload = d.subarray(8);
      if (addr !== this.got || payload.length !== n) { T.log.push('bad block at ' + addr); return; }
      this.written.set(payload, addr); this.got += n;
      setTimeout(() => this.next(), 0);
    }
  }
  T.add = (opts) => { const d = new Device('dev' + T.devices.length, opts); T.devices.push(d); refresh(); return T.devices.length - 1; };
  T.dev = i => T.devices[i];
})();
