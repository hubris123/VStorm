// VStorm virtual screen core: loads vstorm.wasm and exposes a small API.
// Shared by the browser page (index.html) and the headless tools (tools/*.mjs).
export async function createSim(wasmBytes, opts = {}) {
  const w = opts.width ?? 320, h = opts.height ?? 172;
  const store = opts.store ?? new Map();       // settings blob storage
  const now = opts.now ?? (() => performance.now());
  const t0 = now();
  let inst, mem;
  const u8 = () => new Uint8Array(mem.buffer);
  const env = {
    cos: Math.cos, sin: Math.sin, acos: Math.acos, atan2: Math.atan2,
    pow: Math.pow, exp: Math.exp, log: Math.log,
    sim_tick_ms: () => (now() - t0) >>> 0,
    sim_abort: () => { throw new Error('LVGL assert / abort'); },
    js_random: () => (Math.random() * 0x100000000) >>> 0,
    js_settings_load: (p, len) => {
      const b = store.get('vstorm.settings');
      if (!b || b.length !== len) return 0;
      u8().set(b, p);
      return len;
    },
    js_settings_save: (p, len) => { store.set('vstorm.settings', u8().slice(p, p + len)); opts.onSave?.(); },
    js_factory_reset: () => { store.delete('vstorm.settings'); opts.onFactoryReset?.(); },
  };
  ({ instance: inst } = await WebAssembly.instantiate(wasmBytes, { env }));
  const x = inst.exports;
  mem = x.memory;
  x.sim_init(w, h);
  return {
    width: w, height: h, exports: x,
    loop: () => x.sim_loop(),
    touch: (px, py, down) => x.sim_touch(px | 0, py | 0, down ? 1 : 0),
    dirty: () => x.sim_dirty(),
    pixels: () => new Uint8ClampedArray(mem.buffer, x.sim_fb(), w * h * 4),
    brightness: () => x.sim_get_brightness(),
    memUsed: () => x.sim_mem_used(),
    bootClick: () => x.sim_boot_click(),
    bootLock: () => x.sim_boot_lock(),
    bootReset: () => x.sim_boot_reset(),
    audioSetRate: (hz) => x.sim_audio_set_rate(hz),
    // process a Float32Array block (-1..1) through the firmware DSP, returns output
    audioProcess: (input) => {
      const n = input.length;
      new Float32Array(mem.buffer, x.sim_audio_in_ptr(), n).set(input);
      x.sim_audio_process(n);
      return new Float32Array(mem.buffer, x.sim_audio_out_ptr(), n).slice();
    },
    keyed: () => !!x.sim_audio_keyed(),
  };
}
