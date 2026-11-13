// The Tauri calls, with a stand-in for a plain browser (npm run dev in a tab) so the page can be looked at and clicked without the app.
import { invoke as tauriInvoke } from '@tauri-apps/api/core';
import { listen as tauriListen } from '@tauri-apps/api/event';
import { open as tauriOpen } from '@tauri-apps/plugin-dialog';
import { getCurrentWebview } from '@tauri-apps/api/webview';

export const inTauri = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
export type Ev = { event: string; [k: string]: unknown };
export type PyStatus = { ok: boolean; path: string; version: string; pillow: string; numpy: string; message: string };
export type Scan = { kind: 'file' | 'folder' | 'unsupported' | 'missing'; name: string; pictures: number };
export type Volume = { name: string; path: string; free_gb: number };
export type Summary = { pictures: number; converted: number; skipped: number; out: string };

const handlers: ((e: Ev) => void)[] = [];
let mockRun = 0;
async function mockInvoke(cmd: string, args: Record<string, unknown> = {}): Promise<unknown> {
  if (cmd === 'python_status') return { ok: true, path: '/usr/bin/python3', version: '3.11.5', pillow: '12.3.0', numpy: '2.4.6', message: '' } satisfies PyStatus;
  if (cmd === 'list_volumes') return [{ name: 'TauV', path: '/Volumes/TauV', free_gb: 59.8 }, { name: 'BACKUP', path: '/Volumes/BACKUP', free_gb: 410.2 }] satisfies Volume[];
  if (cmd === 'normalized') { const p = String(args.path).replace(/\/+$/, ''); const i = p.indexOf('/Assets/tau_gallery'); return i >= 0 ? p.slice(0, i) : p; }
  if (cmd === 'default_output') return '/Users/you/Documents/Tau Gallery';
  if (cmd === 'scan') { const p = String(args.path); return { kind: p.endsWith('/') || !p.includes('.') ? 'folder' : 'file', name: p.split('/').filter(Boolean).pop() ?? p, pictures: p.includes('.') ? 1 : 12 } satisfies Scan; }
  if (cmd === 'convert') {
    const run = ++mockRun, n = String(args.input).includes('.') ? 1 : 12;
    const emit = (e: Ev) => handlers.forEach((h) => h(e));
    emit({ event: 'plan', files: n, kept: args.merge ? 3 : 0 });
    for (let i = 0; i < n; i++) await new Promise((r) => setTimeout(r, 350)), run === mockRun && emit(i === 4 ? { event: 'skip', file: 'broken.jpg', error: 'cannot identify image file' } : { event: 'converted', file: `IMG_${1000 + i}.jpg`, tgy: `IMG_${1000 + i}.tgy`, size: 1748992 + i * 1000, width: 800, height: 600 });
    return { pictures: n + 3, converted: n - (n > 4 ? 1 : 0), skipped: n > 4 ? 1 : 0, out: String(args.output) } satisfies Summary;
  }
  return null;
}
export async function invoke<T>(cmd: string, args: Record<string, unknown> = {}): Promise<T> { return (inTauri ? tauriInvoke(cmd, args) : mockInvoke(cmd, args)) as Promise<T>; }
export async function onConvertEvent(cb: (e: Ev) => void): Promise<() => void> {
  if (inTauri) return tauriListen<Ev>('convert-event', (e) => cb(e.payload));
  handlers.push(cb); return () => { const i = handlers.indexOf(cb); if (i >= 0) handlers.splice(i, 1); };
}
export async function pick(directory: boolean): Promise<string | null> {
  if (!inTauri) return directory ? '/Users/you/Pictures/Holiday' : '/Users/you/Pictures/IMG_2001.jpg';
  const r = await tauriOpen({ directory, multiple: false, title: directory ? 'Choose a folder of pictures' : 'Choose a picture',
    filters: directory ? undefined : [{ name: 'Pictures', extensions: ['jpg', 'jpeg', 'png', 'bmp', 'gif', 'tif', 'tiff', 'webp'] }] });
  return typeof r === 'string' ? r : null;
}
export async function pickOutput(): Promise<string | null> {
  if (!inTauri) return '/Users/you/Documents/Tau Gallery';
  const r = await tauriOpen({ directory: true, multiple: false, title: 'Choose the output folder' });
  return typeof r === 'string' ? r : null;
}
export async function onDrop(cb: (paths: string[]) => void, over: (on: boolean) => void): Promise<() => void> {
  if (!inTauri) return () => {};
  return getCurrentWebview().onDragDropEvent((e) => {
    if (e.payload.type === 'over' || e.payload.type === 'enter') over(true);
    else if (e.payload.type === 'leave') over(false);
    else if (e.payload.type === 'drop') { over(false); cb(e.payload.paths); }
  });
}
