/* SPDX-License-Identifier: MIT
 * SEA5 chunk decoder for in-browser preview, ported from web/synth/sea_decode.c
 * (SEA codec (c) 2025 Dani Biro; FM-1 chunk format (c) 2026 M-VAVE). */
globalThis.SEA5 = (() => {
  const FRAMES = 320, GROUP = 20, BITS = 5, CHUNK = 228;
  const c16 = v => v > 32767 ? 32767 : v < -32768 ? -32768 : v;
  function decodeChunk(c, dqt, out, at) {
    if (c[0] !== 1 || c[1] !== (0x40 | BITS) || c[2] !== GROUP || c[3] !== 0x5a) throw Error('Not a SEA5 chunk');
    const st = new Array(8);
    for (let i = 0; i < 8; i++) { const v = c[4 + i * 2] | (c[5 + i * 2] << 8); st[i] = v < 32768 ? v : v - 65536; }
    for (let g = 0; g < FRAMES / GROUP; g++) {
      const scale = (c[20 + (g >> 1)] >> ((g & 1) ? 0 : 4)) & 15;
      for (let i = 0; i < GROUP; i++) {
        const bit = (g * GROUP + i) * BITS; let code = 0;
        for (let b = 0; b < BITS; b++) code = (code << 1) | ((c[28 + ((bit + b) >> 3)] >> (7 - ((bit + b) & 7))) & 1);
        let sum = 0; for (let j = 0; j < 4; j++) sum += st[j] * st[4 + j];
        const residual = dqt[(scale << BITS) + code];
        const value = c16(Math.floor(sum / 8192) + residual);
        const delta = residual >> 4;
        for (let j = 0; j < 4; j++) st[4 + j] = c16(st[4 + j] + (st[j] < 0 ? -delta : delta));
        st[0] = st[1]; st[1] = st[2]; st[2] = st[3]; st[3] = value;
        out[at + g * GROUP + i] = value;
      }
    }
  }
  /* data: Uint8Array of whole chunks; returns Float32Array of `frames` samples. */
  function decode(data, frames, dqt) {
    const pcm = new Int16Array(Math.ceil(frames / FRAMES) * FRAMES);
    for (let k = 0; k * CHUNK < data.length; k++) decodeChunk(data.subarray(k * CHUNK, (k + 1) * CHUNK), dqt, pcm, k * FRAMES);
    const f = new Float32Array(frames); for (let i = 0; i < frames; i++) f[i] = pcm[i] / 32768; return f;
  }
  return { decode, RATE: 32000 };
})();
