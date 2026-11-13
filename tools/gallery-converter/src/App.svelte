<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke, onConvertEvent, onDrop, pick, pickOutput, type Ev, type PyStatus, type Scan, type Summary, type Volume } from './tauri';

  const KEY = 'tau-gallery-converter';
  const saved = (() => { try { return JSON.parse(localStorage.getItem(KEY) ?? '{}'); } catch { return {}; } })();

  let source = $state('');
  let scanInfo = $state<Scan | null>(null);
  let output = $state<string>(saved.output ?? '');
  let merge = $state<boolean>(saved.merge ?? true);
  let levels = $state<boolean>(saved.levels ?? true);
  let format = $state<'v0' | 'v1'>(saved.format === 'v0' ? 'v0' : 'v1');
  let quality = $state<number>(Number(saved.quality) >= 1 && Number(saved.quality) <= 100 ? Number(saved.quality) : 92);
  let fitQuality = $state<number>(Number(saved.fitQuality) >= 1 && Number(saved.fitQuality) <= 100 ? Number(saved.fitQuality) : 90);
  let upscale = $state<boolean>(saved.upscale ?? false);
  let pythonPath = $state<string>(saved.python ?? '');
  let py = $state<PyStatus | null>(null);
  let over = $state(false);
  let volumes = $state<Volume[]>([]);
  let usedRoot = $state('');          // set when a folder inside the card's tree was changed to the card's root

  let busy = $state(false);
  let total = $state(0);
  let kept = $state(0);
  let done = $state(0);
  let log = $state<{ ok: boolean; name: string; detail: string }[]>([]);
  let summary = $state<Summary | null>(null);
  let error = $state('');
  let stopping = $state(false);

  $effect(() => { try { localStorage.setItem(KEY, JSON.stringify({ output, merge, levels, upscale, format, quality, fitQuality, python: pythonPath })); } catch { /* no storage: fine */ } });

  const needsNumpy = $derived(format === 'v1' && !!py?.ok && !py.numpy);
  const canConvert = $derived(!busy && !!py?.ok && !needsNumpy && !!output && !!scanInfo && (scanInfo.kind === 'file' || scanInfo.kind === 'folder') && scanInfo.pictures > 0);

  async function setSource(path: string | null) {
    if (!path) return;
    source = path; summary = null; error = '';
    scanInfo = await invoke<Scan>('scan', { path });
  }
  async function setOutput(path: string) {
    const root = await invoke<string>('normalized', { path });
    usedRoot = root !== path.replace(/\/+$/, '') ? root : '';
    output = root;
  }
  async function refreshVolumes() {
    volumes = await invoke<Volume[]>('list_volumes');
    const here = volumes.find((v) => output === v.path);
    if (!here && output.startsWith('/Volumes/') && !volumes.some((v) => output.startsWith(v.path))) output = '';      // the card that was chosen is gone
    if (!output && volumes.length === 1) await setOutput(volumes[0].path);                                              // one card in: it is the one
  }
  async function checkPython() { py = await invoke<PyStatus>('python_status', { user: pythonPath || null }); }

  onMount(() => {
    void (async () => {
      await refreshVolumes();
      if (!output) output = await invoke<string>('default_output');
      await checkPython();
    })();
    let offDrop = () => {}; let offEv = () => {};
    void onDrop((paths) => { if (!busy && paths.length) void setSource(paths[0]); }, (on) => (over = on && !busy)).then((f) => (offDrop = f));
    void onConvertEvent(onEvent).then((f) => (offEv = f));
    const onFocus = () => { if (!busy) void refreshVolumes(); };
    window.addEventListener('focus', onFocus);
    return () => { offDrop(); offEv(); window.removeEventListener('focus', onFocus); };
  });

  function mb(n: unknown) { return (Number(n) / 1048576).toFixed(1) + ' MB'; }
  function onEvent(e: Ev) {
    if (e.event === 'plan') { total = Number(e.files); kept = Number(e.kept); }
    else if (e.event === 'converted') { done++; log.push({ ok: true, name: String(e.file), detail: `${e.width} × ${e.height} · ${mb(e.size)}` }); }
    else if (e.event === 'skip') { done++; log.push({ ok: false, name: String(e.file), detail: String(e.error) }); }
    else if (e.event === 'warning') { log.push({ ok: false, name: 'Note', detail: String(e.error) }); }
  }

  async function convert() {
    busy = true; stopping = false; error = ''; summary = null; log = []; done = 0; total = scanInfo?.pictures ?? 0; kept = 0;
    try { summary = await invoke<Summary>('convert', { input: source, output, levels, upscale, merge, format, quality, fitQuality, python: pythonPath || null }); }
    catch (e) { error = String(e); }
    finally { busy = false; stopping = false; }
  }
  async function stop() { stopping = true; await invoke('cancel'); }
  async function showOutput() { try { await invoke('reveal', { path: output }); } catch (e) { error = String(e); } }
</script>

<svelte:head><title>Tau Gallery Converter</title></svelte:head>
<main>
  <header>
    <img class="mark" src="/tau-gallery-converter.svg" alt="" width="46" height="46" />
    <div><p class="eyebrow">PHOTOS FOR THE POCKET</p><h1>Tau Gallery Converter</h1></div>
  </header>

  <section aria-label="Source">
    <h2>1 · Pictures</h2>
    <div class="pick">
      <input type="text" value={source} readonly placeholder="Choose a picture or a folder of pictures" aria-label="Source" />
      <button type="button" disabled={busy} onclick={async () => setSource(await pick(false))}>Picture…</button>
      <button type="button" disabled={busy} onclick={async () => setSource(await pick(true))}>Folder…</button>
    </div>
    <div class="drop" class:over role="note">or drop a picture or a folder here</div>
    {#if scanInfo}
      <p class="info" aria-live="polite">
        {#if scanInfo.kind === 'folder'}
          {#if scanInfo.pictures > 0}<b>{scanInfo.name}</b> · folder with <b>{scanInfo.pictures}</b> {scanInfo.pictures === 1 ? 'picture' : 'pictures'} <span class="note">(JPEG, PNG, BMP, GIF, TIFF, WebP; subfolders are not read)</span>
          {:else}<span class="warn"><b>{scanInfo.name}</b> has no pictures this tool reads.</span>{/if}
        {:else if scanInfo.kind === 'file'}<b>{scanInfo.name}</b> · 1 picture
        {:else if scanInfo.kind === 'unsupported'}<span class="warn"><b>{scanInfo.name}</b> is not a picture this tool reads (JPEG, PNG, BMP, GIF, TIFF, WebP).</span>
        {:else}<span class="bad">That path does not exist.</span>{/if}
      </p>
    {/if}
  </section>

  <section aria-label="Output">
    <h2>2 · Where to put it</h2>
    <div class="pick">
      <select aria-label="Card or disk" disabled={busy} value={volumes.find((v) => v.path === output)?.path ?? ''} onchange={(e) => { const v = (e.currentTarget as HTMLSelectElement).value; if (v) void setOutput(v); }}>
        <option value="">{volumes.length ? 'Choose the card…' : 'No card found'}</option>
        {#each volumes as v}<option value={v.path}>{v.name}{v.free_gb >= 0 ? ` · ${v.free_gb} GB free` : ''}</option>{/each}
      </select>
      <button type="button" disabled={busy} title="Look for the card again" aria-label="Look for the card again" onclick={refreshVolumes}>↻</button>
    </div>
    <div class="pick" style="margin-top:8px">
      <input type="text" value={output} onchange={(e) => void setOutput((e.currentTarget as HTMLInputElement).value)} placeholder="or any folder" aria-label="Output folder" disabled={busy} />
      <button type="button" disabled={busy} onclick={async () => { const r = await pickOutput(); if (r) await setOutput(r); }}>Folder…</button>
    </div>
    {#if output}<p class="note">The pictures go to <b>{output}/Assets/tau_gallery/common/</b>{usedRoot ? ' (the folder you chose was inside that tree, so its root is used)' : ''}. For a card that is all there is to do: eject it and put it in the Pocket. For another folder, copy its <b>Assets</b> folder to the root of the card.</p>{/if}
    <label class="opt"><input type="checkbox" bind:checked={merge} disabled={busy} /><span>Add to the library already in this folder<small>Keeps the pictures converted before; a picture with the same name is replaced. Turn off to start a new library.</small></span></label>
    <fieldset class="fmt" disabled={busy}>
      <legend>Format</legend>
      <label class="opt"><input type="radio" name="format" value="v1" bind:group={format} /><span>Hardware decoder<small>For the Tau Gallery core with the hardware decoder (DEV 013 and later). Pictures open quickly and zoom to their full size, whatever the size; the files are small. Needs numpy.</small></span></label>
      <label class="opt"><input type="radio" name="format" value="v0" bind:group={format} /><span>Plain (every Tau Gallery core)<small>Works on every build. The pictures are stored as raw tiles, so the files are large.</small></span></label>
    </fieldset>
    {#if format === 'v0'}
    <label class="opt"><input type="checkbox" bind:checked={levels} disabled={busy} /><span>Zoom levels (pan and zoom)<small>Large pictures keep their detail for zooming. About 45 MB more on the card for each large picture; small ones add nothing.</small></span></label>
    {:else}
    <p class="note">Zoom comes with the picture: a picture larger than the screen also gets its full-size level for panning and zooming, no setting needed. A very large picture takes a long time to convert (10 to 20 minutes for 100 megapixels on the test machine, so far one picture tried).</p>
    <details class="adv"><summary>Quality ({fitQuality} fit view, {quality} zoom level)</summary>
      <label class="opt"><span>Fit view quality: {fitQuality}<small>The picture as first shown (JPEG-style quality, 1 to 100).</small><input type="range" min="50" max="100" step="1" bind:value={fitQuality} disabled={busy} aria-label="Fit view quality" /></span></label>
      <label class="opt"><span>Zoom level quality: {quality}<small>Only for pictures that are not JPEG (a JPEG's own data is kept as it is, with no loss).</small><input type="range" min="50" max="100" step="1" bind:value={quality} disabled={busy} aria-label="Zoom level quality" /></span></label>
    </details>
    {/if}
    {#if needsNumpy}<p class="info bad" role="alert">The hardware-decoder format needs numpy. Install it with <b>python3 -m pip install numpy</b>, or choose the plain format.</p>{/if}
    <label class="opt"><input type="checkbox" bind:checked={upscale} disabled={busy} /><span>Enlarge small pictures to fill the screen<small>Off: pictures smaller than the screen show at their own size.</small></span></label>
  </section>

  <button class="primary" type="button" onclick={convert} disabled={!canConvert}>{busy ? 'Converting…' : 'Convert'}</button>

  {#if busy || log.length || summary || error}
    <section aria-label="Progress" style="margin-top:16px">
      {#if busy}
        <div class="row"><strong>{stopping ? 'Stopping…' : 'Converting'}</strong><span>{done} of {total}</span></div>
        <progress aria-label="Conversion progress" value={done} max={Math.max(total, 1)}></progress>
        <div class="actions"><button type="button" onclick={stop} disabled={stopping}>Stop</button></div>
      {/if}
      {#if log.length}<div class="log" role="log" aria-label="Converted pictures">{#each log as l}<div class:e={!l.ok}><span>{l.ok ? '✓' : '✕'} {l.name}</span><span>{l.detail}</span></div>{/each}</div>{/if}
      {#if summary}
        <p class="info ok" role="status"><b>Done.</b> {summary.converted} converted{summary.skipped ? `, ${summary.skipped} skipped` : ''}. The library now has <b>{summary.pictures}</b> {summary.pictures === 1 ? 'picture' : 'pictures'}.</p>
        <div class="actions"><button type="button" onclick={showOutput}>Show output folder</button></div>
      {/if}
      {#if error}<p class="info bad" role="alert">{error}</p>{/if}
    </section>
  {/if}

  <details open={!py?.ok}>
    <summary>{py?.ok ? `Python ${py.version} · Pillow ${py.pillow}${py.numpy ? ' · numpy ' + py.numpy : ' · no numpy'}` : 'Python 3 with Pillow is needed'}</summary>
    {#if py && !py.ok}<p class="info bad">{py.message}</p>{/if}
    <div class="pick"><input type="text" bind:value={pythonPath} placeholder="Full path of python3 (optional)" aria-label="Python path" /><button type="button" onclick={checkPython}>Check</button></div>
  </details>
</main>
