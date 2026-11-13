// Copies the converter (tools/gallery/tgy_convert.py, the tested one) and every module it imports (found by reading the import lines, so a new dependency of the converter is picked up) into src-tauri/resources, where the app finds it.
import { copyFileSync, existsSync, mkdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const from = join(here, '..', '..', 'gallery'), to = join(here, '..', 'src-tauri', 'resources');
mkdirSync(to, { recursive: true });
const want = ['tgy_convert'], seen = new Set();
while (want.length) {
  const m = want.pop();
  if (seen.has(m)) continue;
  seen.add(m);
  const text = readFileSync(join(from, m + '.py'), 'utf8');
  for (const raw of text.split('\n')) {
    const line = raw.replace(/#.*$/, '');
    const f = /^\s*from\s+([A-Za-z0-9_]+)\s+import\b/.exec(line), im = /^\s*import\s+(.+)$/.exec(line);
    const names = f ? [f[1]] : im ? im[1].split(',').map((x) => x.trim().split(/\s+/)[0]) : [];      // "import a, b as c, d" gives a, b and d
    for (const name of names) if (name && existsSync(join(from, name + '.py'))) want.push(name);
  }
}
for (const m of seen) copyFileSync(join(from, m + '.py'), join(to, m + '.py'));
console.log('converter synced to', to, '(' + [...seen].sort().join(', ') + ')');
