// Headless virtual screen for scripted UI checks (no browser needed).
//   node tools/headless.mjs script.txt [outdir] [fw3|fw1]   (default fw3)
// Script commands (one per line, # comments):
//   size W H          display resolution (must come first; default = the target board's)
//   wait MS           advance simulated time
//   tap X Y           press + release
//   drag X1 Y1 X2 Y2  press, move, release
//   boot click|lock|reset   BOOT button actions
//   signal LEVEL MS   feed a synthetic voice-band signal (0..1 amplitude)
//   battery PCT       simulated battery % (fw3i)
//   tilt X Y          simulated tilt in degrees (fw3i)
//   temp C            simulated board temperature in Celsius (fw3i)
//   shot NAME         write NAME.png
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { createSim } from '../web/vstorm-core.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const [scriptFile, outDir = '.', target = 'fw3'] = process.argv.slice(2);
const lines = fs.readFileSync(scriptFile, 'utf8').split('\n').map(l => l.replace(/#.*/, '').trim()).filter(Boolean);
fs.mkdirSync(outDir, { recursive: true });

let clock = 0;
let size = [];
if (lines[0]?.startsWith('size')) size = lines.shift().split(/\s+/).slice(1).map(Number);
const sim = await createSim(fs.readFileSync(path.join(here, `../web/vstorm-${target}.wasm`)),
  { width: size[0], height: size[1], now: () => clock });

let phase = 0;
function step(ms, sig = 0) {
  for (let t = 0; t < ms; t += 5) {
    clock += 5;
    if (sig > 0) {        // 5 ms of a buzzy 140 Hz voice-like signal at 16 kHz
      const blk = new Float32Array(80);
      for (let i = 0; i < 80; i++) { phase += 140 / 16000; blk[i] = sig * (((phase % 1) * 2 - 1) * 0.6 + Math.sin(phase * 2 * Math.PI * 7) * 0.4); }
      sim.audioProcess(blk);
    } else sim.audioProcess(new Float32Array(80));
    sim.loop();
  }
}
function png(file) {
  const { width: w, height: h } = sim;
  const px = sim.pixels();
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) Buffer.from(px.buffer, px.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = b => { let c = ~0; for (const v of b) c = crcT[(c ^ v) & 255] ^ (c >>> 8); return (~c) >>> 0; };
  const chunk = (t, d) => { const len = Buffer.alloc(4); len.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

step(200);
for (const l of lines) {
  const [cmd, ...a] = l.split(/\s+/);
  const n = a.map(Number);
  switch (cmd) {
    case 'wait': step(n[0]); break;
    case 'tap': sim.touch(n[0], n[1], 1); step(60); sim.touch(n[0], n[1], 0); step(60); break;
    case 'drag': {
      sim.touch(n[0], n[1], 1); step(40);
      for (let i = 1; i <= 10; i++) { sim.touch(n[0] + (n[2] - n[0]) * i / 10, n[1] + (n[3] - n[1]) * i / 10, 1); step(30); }
      sim.touch(n[2], n[3], 0); step(60); break;
    }
    case 'boot': ({ click: sim.bootClick, lock: sim.bootLock, reset: sim.bootReset })[a[0]](); step(60); break;
    case 'signal': step(n[1], n[0]); break;
    case 'battery': sim.exports.sim_set_battery?.(n[0]); step(100); break;
    case 'temp': sim.exports.sim_set_temp?.(n[0]); step(800); break;
    case 'tilt': sim.exports.sim_set_tilt?.(n[0], n[1]); step(600); break;
    case 'waitrev': {   // step until the first main-screen ring reverses (max 30 s), then a further n ms
      const sg = Math.sign(sim.exports.ui_dbg_ring_spd(0));
      for (let t = 0; t < 30000 && Math.sign(sim.exports.ui_dbg_ring_spd(0)) === sg; t += 50) step(50);
      step(n[0] || 0); break;
    }
    case 'capcheck': {  // worst key-label overflow past the 7 px margin on the shown screen
      const o = sim.exports.ui_dbg_cap_overflow();
      const ptr = sim.exports.ui_dbg_text(); const mem = new Uint8Array(sim.exports.memory.buffer, ptr, 64);
      const txt = new TextDecoder().decode(mem.slice(0, mem.indexOf(0))).replace(/\n/g, '|');
      console.log('capcheck', a[0] || '', o > 0 ? 'OVERFLOW ' + o + ' px: ' + txt : 'ok (' + o + ' px spare)'); break;
    }
    case 'shot': png(path.join(outDir, a[0] + '.png')); console.log('shot', a[0], 'lvgl heap used', sim.memUsed(), 'B'); break;
    default: throw new Error('unknown command: ' + l);
  }
}
