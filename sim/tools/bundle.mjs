// Inline vstorm-core.js + both vstorm-*.wasm builds into one self-contained HTML file.
//   node tools/bundle.mjs  -> dist/vstorm-console.html
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
const core = fs.readFileSync(path.join(root, 'web/vstorm-core.js'), 'utf8').replace(/^export /m, '');
const b64 = Object.fromEntries(['fw3i', 'fw3', 'fw1'].map(t => [t, fs.readFileSync(path.join(root, `web/vstorm-${t}.wasm`)).toString('base64')]));
const importLine = /^import \{ createSim \} from '\.\/vstorm-core\.js';.*$/m;
if (!importLine.test(html)) throw new Error('import line not found in index.html');
const out = html.replace(importLine, () => `globalThis.VSTORM_WASM_B64 = ${JSON.stringify(b64)};\n${core}`);
fs.mkdirSync(path.join(root, 'dist'), { recursive: true });
fs.writeFileSync(path.join(root, 'dist/vstorm-console.html'), out);
console.log('dist/vstorm-console.html', (out.length / 1024).toFixed(0), 'KB');
