// Copies the converter (tools/gallery/tgy.py and tgy_convert.py, the tested one) into src-tauri/resources, where the app finds it.
import { copyFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const from = join(here, '..', '..', 'gallery'), to = join(here, '..', 'src-tauri', 'resources');
mkdirSync(to, { recursive: true });
for (const f of ['tgy.py', 'tgy_convert.py']) copyFileSync(join(from, f), join(to, f));
console.log('converter synced to', to);
