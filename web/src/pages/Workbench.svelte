<script>
  import { tick, onDestroy, onMount } from "svelte";
  import { app, api } from "../store.svelte.js";
  import {
    project, fileNames, filesMap, uniqueName, openFile, addFile, addFolder,
    setSrc, setEntry, switchProject, resetProject, exportWorkspace, importWorkspace,
  } from "../lib/project.svelte.js";
  import { langForName, scrapeSymbols, setVimCommands, onVimMode } from "../lib/cm.js";
  import { settings, setSetting } from "../lib/settings.svelte.js";
  import { downloadText, downloadProjectZip } from "../lib/download.js";
  import {
    L as dock, allLeaves, activate, closeKey, openKey, toggleTool, splitActive,
    mergeAll, reconcileFiles, fileKey, fileName, isFile,
  } from "../lib/layout.svelte.js";
  import { ui } from "../lib/media.svelte.js";
  import CodeEditor from "../lib/CodeEditor.svelte";
  import Terminal from "../lib/Terminal.svelte";
  import Explorer from "../lib/Explorer.svelte";
  import Settings from "../lib/Settings.svelte";
  import Palette from "../lib/Palette.svelte";
  import Menu from "../lib/Menu.svelte";
  import Icon from "../lib/Icon.svelte";
  import RightDock from "../lib/RightDock.svelte";
  import Pane from "../lib/Pane.svelte";
  import { compact } from "../lib/format.js";

  const basename = (p) => (p ? p.slice(p.lastIndexOf("/") + 1) : p);

  const SAVED_KEY = "shrewdness-ide-chrome";
  const SAVED = (() => { try { return JSON.parse(localStorage.getItem(SAVED_KEY)) || {}; } catch { return {}; } })();
  let sideW = $state(SAVED.side ?? 250);
  let sideCollapsed = $state(SAVED.collapsed ?? ui.narrow);
  function saveChrome() {
    localStorage.setItem(SAVED_KEY, JSON.stringify({ side: sideW, collapsed: sideCollapsed }));
  }
  function toggleSidebar() {
    sideCollapsed = !sideCollapsed;
    if (!ui.narrow) saveChrome();
  }
  const closeDrawer = () => { if (ui.narrow) sideCollapsed = true; };

  let wasWide = !ui.narrow;
  $effect(() => {
    if (ui.narrow && wasWide) sideCollapsed = true;
    wasWide = !ui.narrow;
  });

  let cursor = $state({ line: 1, col: 1, sels: 1 });
  let err = $state("");
  let toast = $state("");
  let showSettings = $state(false);
  let palette = $state(null);
  let vimMode = $state("");
  let ctxMenu = $state(null);
  let seed = $state(0);
  let steps = $state(200000000);

  const activeName = $derived(dock.activeFile);
  const activeSrc = $derived(
    activeName ? project.files.find((f) => f.name === activeName)?.src ?? "" : "",
  );
  const lang = $derived(activeName ? langForName(activeName) : "savvy");
  const standalone = $derived(!!activeName && lang !== "savvy");
  const runTarget = $derived(activeName ? (standalone ? activeName : project.entry) : "");
  const problems = $derived(Object.entries(project.diags).map(([file, e]) => ({ file, ...e })));
  const paneCount = $derived(allLeaves().length);

  $effect(() => { void project.open; void project.id; reconcileFiles(); });

  const TOOLMETA = {
    terminal: ["terminal", "console"], compiler: ["build", "compiler"],
    inspector: ["dna", "genome"], debug: ["step", "debug"],
  };
  const focusedLeaf = () => allLeaves().find((l) => l.id === dock.focused) ?? allLeaves()[0];

  function activateFile(name) {
    openFile(name);
    openKey(fileKey(name), true);
  }
  function openProjectFile(id, name) {
    if (id && id !== project.id) { switchProject(id); reconcileFiles(); }
    activateFile(name);
    closeDrawer();
  }
  function loadExample(name) {
    const ex = app.examples.find((e) => e.name === name);
    if (ex) activateFile(addFile(uniqueName(ex.name, ".savvy"), ex.src));
    closeDrawer();
  }

  let unsubVim;
  onMount(() => {
    setVimCommands({
      write: () => flash("saved"), run: doRun, build: doBuild,
      quit: () => { const lf = focusedLeaf(); if (lf?.active) closeKey(lf.active); },
    });
    unsubVim = onVimMode((m) => (vimMode = m));
  });
  onDestroy(() => {
    unsubVim?.();
    clearTimeout(pollTimer);
    if (session && session.state !== "done") api("/api/console/kill", { id: session.id }).catch(() => {});
  });

  function flash(m, isErr = false) {
    toast = m; err = isErr ? m : err;
    setTimeout(() => (toast = ""), 2600);
  }

  function editExec(cmd) { try { document.execCommand(cmd); } catch { /* */ } }
  async function editPaste() {
    let text = ""; try { text = await navigator.clipboard.readText(); } catch { return; }
    try { document.execCommand("insertText", false, text); } catch { /* */ }
  }
  function openEditorMenu(e) {
    if (!activeName) return;
    e.preventDefault(); e.stopPropagation();
    const hasSel = !!window.getSelection()?.toString();
    const items = [];
    if (hasSel) items.push({ label: "Cut", icon: "scissors", kbd: "Ctrl X", run: () => editExec("cut") },
      { label: "Copy", icon: "copy", kbd: "Ctrl C", run: () => editExec("copy") });
    items.push({ label: "Paste", icon: "clipboard", kbd: "Ctrl V", run: editPaste }, { sep: true });
    items.push(
      { label: "Command palette", icon: "menu", kbd: "Ctrl+Shift+P", run: openCommandPalette },
      { label: "Go to file", icon: "search", kbd: "Ctrl+P", run: openFilePalette },
      { sep: true },
      { label: "Run", icon: "play", kbd: "Ctrl+Enter", run: doRun },
      { label: "Split editor", icon: "panel", run: () => splitActive("right") },
      { sep: true }, ...convertItems(), { sep: true },
      { label: "Download file", icon: "download", run: downloadActive },
    );
    ctxMenu = { x: e.clientX, y: e.clientY, items };
  }
  async function termPaste() {
    let text = ""; try { text = await navigator.clipboard.readText(); } catch { return; }
    for (const line of text.split("\n")) if (line !== "") sendLine(line);
  }
  function openTermMenu(e) {
    const sel = window.getSelection()?.toString();
    const live = session && session.state !== "done";
    const items = [];
    if (sel) items.push({ label: "Copy", icon: "copy", run: () => navigator.clipboard?.writeText(sel) });
    if (live) items.push({ label: "Paste", icon: "clipboard", run: termPaste });
    items.push({ sep: true }, { label: "Clear", icon: "trash", kbd: "Ctrl+L", run: clearConsole });
    if (live) items.push(
      { label: "Send EOF", icon: "check", kbd: "Ctrl+D", run: sendEof },
      { label: "Stop", icon: "square", kbd: "Ctrl+C", danger: true, run: stopRun });
    ctxMenu = { x: e.clientX, y: e.clientY, items };
  }

  function activePayload(extra = {}) {
    if (!activeName) return { src: "", fmt: "savvy", ...extra };
    if (standalone) return { files: { [activeName]: activeSrc }, entry: activeName, ...extra };
    const files = filesMap(); files[activeName] = activeSrc;
    return { files, entry: project.entry, ...extra };
  }
  async function lintFile(name, text) {
    let body;
    if (langForName(name) !== "savvy") body = { files: { [name]: text }, entry: name };
    else { const files = filesMap(); files[name] = text; body = { files, entry: project.entry }; }
    const r = await api("/api/build", body);
    if (r.ok) { project.diags = {}; return null; }
    const file = r.file || (body.entry === name ? name : body.entry);
    project.diags = { [file]: r };
    return file === name ? r : null;
  }
  function doBuild() { if (activeName) openKey("compiler", true); }

  const EXT = { savvy: ".savvy", asm: ".asm", genes: ".shrewd" };
  const FMT_LABEL = { savvy: "Savvy (.savvy)", asm: "Assembly (.asm)", genes: "Genes (.shrewd)" };
  const docBase = () => (activeName ? basename(activeName).replace(/\.(savvy|asm|shrewd)$/, "") : "genome");
  async function convertTo(fmt) {
    if (!activeName) return;
    const r = await api("/api/build", activePayload());
    if (!r.ok) return void flash(r.error, true);
    activateFile(addFile(uniqueName(docBase(), EXT[fmt]), r[fmt]));
    flash(`created ${basename(docBase() + EXT[fmt])}`);
  }
  const convertItems = () => ["savvy", "asm", "genes"].filter((f) => f !== lang)
    .map((f) => ({ label: `Convert to ${FMT_LABEL[f]}`, icon: f === "asm" ? "build" : "dna", run: () => convertTo(f) }));
  function downloadActive() {
    if (activeName) downloadText(basename(activeName), activeSrc);
  }

  let session = $state(null);
  let pollTimer = null;
  function pushLog(t, s) {
    if (!session || !s) return;
    const last = session.log[session.log.length - 1];
    if (last && last.t === t) last.s += s; else session.log.push({ t, s });
  }
  async function pollSession() {
    clearTimeout(pollTimer);
    if (!session) return;
    const id = session.id;
    let r;
    try { r = await api(`/api/console/poll?id=${id}&off=${session.off}`); }
    catch { pollTimer = setTimeout(pollSession, 500); return; }
    if (!session || session.id !== id) return;
    if (!r.found) { session.state = "done"; pushLog("sys", "\n— session expired\n"); return; }
    pushLog("out", r.output);
    session.off = r.off; session.state = r.state;
    if (r.state === "done") {
      session.result = r;
      pushLog("sys", `\n— ${r.halt} · ${compact(r.steps)} steps` +
        (r.offspring.length ? ` · ${r.offspring.length} offspring` : "") + "\n");
      return;
    }
    pollTimer = setTimeout(pollSession, 170);
  }
  async function doRun() {
    if (!activeName) return;
    err = "";
    clearTimeout(pollTimer);
    if (session && session.state !== "done") await api("/api/console/kill", { id: session.id }).catch(() => {});
    const r = await api("/api/console/start", activePayload({ seed: +seed, steps: +steps }));
    openKey("terminal", true);
    if (!r.ok) { err = r.error; session = null; return; }
    session = { id: r.id, off: 0, state: "running", log: [], result: null };
    pollSession();
  }
  function sendLine(line) {
    if (!session || session.state === "done") return;
    pushLog("in", line + "\n");
    api("/api/console/input", { id: session.id, text: line + "\n" });
    clearTimeout(pollTimer); pollTimer = setTimeout(pollSession, 50);
  }
  function sendEof() {
    if (!session || session.state === "done") return;
    pushLog("sys", "⏎ eof\n");
    api("/api/console/input", { id: session.id, text: "", eof: true });
    clearTimeout(pollTimer); pollTimer = setTimeout(pollSession, 50);
  }
  function stopRun() {
    if (!session || session.state === "done") return;
    api("/api/console/kill", { id: session.id });
    pushLog("sys", "■ stopped\n");
    clearTimeout(pollTimer); pollTimer = setTimeout(pollSession, 100);
  }
  function clearConsole() { if (session) session.log = []; }
  function saveChild(o, i) {
    activateFile(addFile(uniqueName(docBase() + "-child-" + i, ".shrewd"), o.genes));
  }

  function openFilePalette() {
    const items = project.files.map((f) => ({
      label: basename(f.name), sub: f.name.includes("/") ? f.name : "", icon: "file",
      run: () => activateFile(f.name),
    }));
    palette = { items, placeholder: "Go to file…" };
  }
  function triggerImport() {
    const input = document.createElement("input");
    input.type = "file"; input.accept = ".json,application/json";
    input.onchange = () => {
      const file = input.files?.[0]; if (!file) return;
      const reader = new FileReader();
      reader.onload = () => { const n = importWorkspace(String(reader.result)); reconcileFiles();
        flash(n ? `imported ${n} project${n === 1 ? "" : "s"}` : "nothing to import", !n); };
      reader.readAsText(file);
    };
    input.click();
  }
  function newFile(ext) { activateFile(addFile(uniqueName("untitled", ext), "")); }
  function openCommandPalette() {
    const km = (m) => () => setSetting("keymap", m);
    palette = { placeholder: "Run a command…", items: [
      { label: "Run program", kbd: "Ctrl+Enter", icon: "play", run: doRun },
      { label: "Open compiler", icon: "build", run: () => openKey("compiler", true) },
      { label: "Open genome inspector", icon: "dna", run: () => openKey("inspector", true) },
      { label: "Open debugger", icon: "step", run: () => openKey("debug", true) },
      { label: "Open terminal", icon: "terminal", run: () => openKey("terminal", true) },
      { label: "Split editor right", icon: "panel", run: () => splitActive("right") },
      { label: "Split editor down", icon: "panel", run: () => splitActive("bottom") },
      { label: "Convert to Savvy file (.savvy)", icon: "dna", run: () => convertTo("savvy") },
      { label: "Convert to Assembly file (.asm)", icon: "build", run: () => convertTo("asm") },
      { label: "Convert to Gene-list file (.shrewd)", icon: "dna", run: () => convertTo("genes") },
      { label: "New Savvy file", icon: "newFile", run: () => newFile(".savvy") },
      { label: "New assembly file", icon: "newFile", run: () => newFile(".asm") },
      { label: "New gene file", icon: "dna", run: () => newFile(".shrewd") },
      { label: "New folder", icon: "newFolder", run: () => addFolder("new-folder") },
      { label: "Download current file", icon: "download", run: downloadActive },
      { label: "Download project (.zip)", icon: "download", run: () => downloadProjectZip(project.name, project.files) },
      { label: "Export workspace (.json)", icon: "download", run: () => downloadText("shrewdness-workspace.json", exportWorkspace()) },
      { label: "Import workspace…", icon: "upload", run: triggerImport },
      { label: "Set entry to current file", icon: "play", run: () => activeName && setEntry(activeName) },
      { label: "Open settings", icon: "gear", run: () => (showSettings = true) },
      { label: "Keymap: Standard", icon: "menu", run: km("standard") },
      { label: "Keymap: Vim", icon: "menu", run: km("vim") },
      { label: "Keymap: Emacs", icon: "menu", run: km("emacs") },
      { label: "Toggle minimap", icon: "map", run: () => setSetting("minimap", !settings.minimap) },
      { label: "Reset project to demo", icon: "refresh", run: () => { resetProject(); reconcileFiles(); } },
    ] };
  }

  function stepTab(delta) {
    const lf = focusedLeaf(); if (!lf?.keys.length) return;
    const i = lf.keys.indexOf(lf.active);
    activate(lf.id, lf.keys[(((i < 0 ? 0 : i) + delta) % lf.keys.length + lf.keys.length) % lf.keys.length]);
  }
  function gotoTab(n) { const lf = focusedLeaf(); const k = lf?.keys[n]; if (k) activate(lf.id, k); }
  function onKeydown(e) {
    const mod = e.ctrlKey || e.metaKey;
    const k = e.key, lk = k.toLowerCase();
    const inEditor = !!e.target?.closest?.(".cm-editor");
    if (mod && e.shiftKey && lk === "p") return void (e.preventDefault(), openCommandPalette());
    if (mod && !e.shiftKey && lk === "p") return void (e.preventDefault(), openFilePalette());
    if ((mod && k === "Enter") || k === "F5") return void (e.preventDefault(), doRun());
    if (mod && e.shiftKey && lk === "b") return void (e.preventDefault(), doBuild());
    if (mod && e.shiftKey && lk === "c") return void (e.preventDefault(), stopRun());
    if (mod && e.shiftKey && lk === "\\") return void (e.preventDefault(), splitActive("right"));
    if (mod && !e.shiftKey && lk === "s") return void (e.preventDefault(), flash("saved"));
    if (mod && k === ",") return void (e.preventDefault(), (showSettings = true));
    if (mod && !e.shiftKey && lk === "b" && !inEditor) return void (e.preventDefault(), toggleSidebar());
    if (mod && k === "`") return void (e.preventDefault(), toggleTool("terminal"));
    if (mod && k === "PageDown") return void (e.preventDefault(), stepTab(1));
    if (mod && k === "PageUp") return void (e.preventDefault(), stepTab(-1));
    if (e.altKey && !mod && /^[1-9]$/.test(k)) return void (e.preventDefault(), gotoTab(+k - 1));
  }

  function dragSide(e) {
    e.preventDefault();
    const p0 = e.clientX, v0 = sideW;
    const move = (ev) => (sideW = Math.max(190, Math.min(520, v0 + ev.clientX - p0)));
    const up = () => { removeEventListener("pointermove", move); removeEventListener("pointerup", up); saveChrome(); };
    addEventListener("pointermove", move); addEventListener("pointerup", up);
  }
  function cycleKeymap() {
    const order = ["standard", "vim", "emacs"];
    setSetting("keymap", order[(order.indexOf(settings.keymap) + 1) % order.length]);
  }
</script>

<svelte:window onkeydown={onKeydown} />

{#snippet tab(key)}
  {#if isFile(key)}
    <span class="tlead ic-{langForName(fileName(key))}"><Icon name="file" size={13} /></span>
    <span class="tabname">{basename(fileName(key))}</span>
    {#if project.diags[fileName(key)]}<span class="errdot"></span>{/if}
  {:else}
    <span class="tlead accent"><Icon name={TOOLMETA[key][0]} size={13} /></span>
    <span class="tabname">{TOOLMETA[key][1]}</span>
  {/if}
{/snippet}

{#snippet body(key)}
  {#if isFile(key)}
    {@const name = fileName(key)}
    <div class="edhost" oncontextmenu={openEditorMenu} role="presentation">
      <CodeEditor
        value={project.files.find((f) => f.name === name)?.src ?? ""}
        lang={langForName(name)} fill
        onchange={(v) => setSrc(name, v)}
        lint={(text) => lintFile(name, text)}
        symbols={() => scrapeSymbols(project.files, name)}
        files={fileNames} isa={() => app.isa}
        oncursor={(c) => (cursor = c)}
      />
    </div>
  {:else if key === "terminal"}
    <div class="termhost" oncontextmenu={openTermMenu} role="presentation">
      {#if session}
        {#key session.id}
          <Terminal log={session.log} status={session.state} onsubmit={sendLine}
            oneof={sendEof} onstop={stopRun} onclear={clearConsole} oncontext={openTermMenu} />
        {/key}
        {#if session.result?.offspring?.length}
          <div class="kids"><span class="kidlabel">offspring →</span>
            {#each session.result.offspring as o, i (i)}
              <button class="kid" title="save this child's genes as a file" onclick={() => saveChild(o, i)}
                >child {i} · {o.len}g ↓</button>
            {/each}
          </div>
        {/if}
      {:else}
        <div class="termph">
          {#if err}<pre class="err">{err}</pre>{/if}
          {#if ui.touch}
            <strong>▶ run {runTarget}</strong> runs the program here.
            <span class="mono">input()</span> reads what you type into the box below the
            output; <em>eof</em> ends input and <em>stop</em> halts the run.
          {:else}
            <strong>▶ run {runTarget}</strong> (or <kbd>Ctrl-Enter</kbd>) runs the program here.
            <span class="mono">input()</span> reads what you type; <kbd>Enter</kbd> sends a line,
            <kbd>Ctrl-D</kbd> ends input, <kbd>Ctrl-C</kbd> stops.
          {/if}
        </div>
      {/if}
    </div>
  {:else}
    <RightDock embed view={key} source={activeSrc} name={activeName || "main.savvy"}
      files={filesMap()} entry={project.entry} isa={app.isa} openFiles={project.files} />
  {/if}
{/snippet}

<div class="ide">
  <div class="body">
    {#if !sideCollapsed}
      {#if ui.narrow}
        <button class="scrim" aria-label="close the file explorer" onclick={toggleSidebar}></button>
      {/if}
      <aside class="side" class:drawer={ui.narrow} style:--sw="{sideW}px">
        <Explorer activeKey={activeName ? fileKey(activeName) : null}
          onFile={openProjectFile} onExample={loadExample} onSettings={() => (showSettings = true)} />
      </aside>
      {#if !ui.narrow}
        <div class="vsplit" role="separator" aria-orientation="vertical" onpointerdown={dragSide}></div>
      {/if}
    {/if}

    <div class="editorcol">
      <div class="topbar">
        {#if ui.narrow}
          <button class="tool sq" title="files (Ctrl-B)" aria-label="toggle the file explorer"
            onclick={toggleSidebar}><Icon name="sidebar" size={15} /></button>
        {/if}
        <button class="tool sq" title="command palette (Ctrl-Shift-P)" onclick={openCommandPalette}><Icon name="menu" size={15} /></button>
        {#if !ui.phone}
          <button class="tool sq" title="new file" onclick={() => newFile(".savvy")}><Icon name="newFile" size={15} /></button>
          <button class="tool sq" title="download current file" onclick={downloadActive}><Icon name="download" size={15} /></button>
        {/if}
        {#if ui.narrow}
          {#if paneCount > 1}
            <button class="tool sq" title="merge the split panes back together" aria-label="merge split panes"
              onclick={mergeAll}><Icon name="panel" size={15} /></button>
          {/if}
        {:else}
          <button class="tool sq" title="split editor right (Ctrl-Shift-\)" onclick={() => splitActive("right")}><Icon name="panel" size={15} /></button>
        {/if}
        <span class="sep"></span>
        <button class="tool sq" title="compiler" onclick={() => toggleTool("compiler")}><Icon name="build" size={15} /></button>
        <button class="tool sq" title="genome inspector" onclick={() => toggleTool("inspector")}><Icon name="dna" size={15} /></button>
        <button class="tool sq" title="visual debugger" onclick={() => toggleTool("debug")}><Icon name="step" size={15} /></button>
        <button class="tool sq" title="terminal (Ctrl-`)" onclick={() => toggleTool("terminal")}><Icon name="terminal" size={15} /></button>
        <span class="grow"></span>
        <button class="run" onclick={doRun} title="run (Ctrl-Enter)"><Icon name="play" size={13} /> run</button>
      </div>

      <div class="paneroot"><Pane node={dock.root} {tab} {body} /></div>
    </div>
  </div>

  <div class="statusbar">
    <span class="sb ico" class:alert={problems.length > 0}><Icon name={problems.length ? "warn" : "check"} size={13} /> {problems.length}</span>
    <span class="sb ellip">{project.name} · ▶ {basename(project.entry)}</span>
    {#if session && session.state !== "done"}
      <button class="sb live" onclick={() => openKey("terminal", true)}>{session.state === "waiting" ? "waiting for input" : "running"}</button>
    {/if}
    {#if settings.keymap === "vim" && vimMode}<span class="sb mode">-- {vimMode.toUpperCase()} --</span>{/if}
    <span class="grow"></span>
    {#if activeName && !ui.phone}<span class="sb num">Ln {cursor.line}, Col {cursor.col}{cursor.sels > 1 ? ` · ${cursor.sels} cursors` : ""}</span>{/if}
    {#if !ui.narrow}
      <button class="sb ico" class:on={!sideCollapsed} title="toggle sidebar (Ctrl-B)" onclick={toggleSidebar}><Icon name="sidebar" size={13} /></button>
    {/if}
    {#if !ui.phone}
      <button class="sb ico" class:on={settings.minimap} title="toggle minimap" onclick={() => setSetting("minimap", !settings.minimap)}><Icon name="map" size={13} /></button>
      <button class="sb" title="cycle keybindings" onclick={cycleKeymap}>{settings.keymap === "standard" ? "standard keys" : settings.keymap}</button>
    {/if}
    <button class="sb ico" title="settings" onclick={() => (showSettings = true)}><Icon name="gear" size={14} /></button>
    <button class="sb ico" title="terminal (Ctrl-`)" onclick={() => toggleTool("terminal")}><Icon name="terminal" size={13} /></button>
  </div>

  {#if toast}<div class="toast">{toast}</div>{/if}
</div>

{#if showSettings}<Settings onclose={() => (showSettings = false)} />{/if}
{#if palette}<Palette items={palette.items} placeholder={palette.placeholder} onclose={() => (palette = null)} />{/if}
{#if ctxMenu}<Menu x={ctxMenu.x} y={ctxMenu.y} items={ctxMenu.items} onclose={() => (ctxMenu = null)} />{/if}

<style>
  .ide { height: 100%; display: flex; flex-direction: column; background: var(--page); }
  .body { flex: 1; display: flex; min-height: 0; position: relative; }
  .side { flex: none; width: var(--sw); min-width: 190px; background: var(--surface); border-right: 1px solid var(--border); overflow: hidden; }
  .vsplit { flex: none; width: 5px; margin: 0 -2px; z-index: 5; cursor: col-resize; background: transparent; transition: background 0.15s; touch-action: none; }
  .vsplit:hover { background: color-mix(in srgb, var(--accent) 40%, transparent); }

  .side.drawer {
    position: absolute; z-index: 30; top: 0; bottom: 0; left: 0;
    width: min(86vw, 320px); min-width: 0;
    padding-left: env(safe-area-inset-left);
    box-shadow: 0 0 44px rgba(0, 0, 0, 0.4);
    animation: slidein 0.16s cubic-bezier(0.2, 0.9, 0.3, 1);
  }
  @keyframes slidein { from { transform: translateX(-100%); } }
  .scrim {
    position: absolute; inset: 0; z-index: 25;
    border: none; border-radius: 0; padding: 0;
    background: color-mix(in srgb, #000 38%, transparent);
    animation: scrimin 0.16s ease;
  }
  @keyframes scrimin { from { opacity: 0; } }

  .editorcol { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .topbar { flex: none; display: flex; align-items: center; gap: 5px; padding: 4px 10px; background: var(--surface); border-bottom: 1px solid var(--border); min-height: 37px; }
  .topbar .sep { width: 1px; height: 18px; background: var(--grid); margin: 0 3px; }
  .grow { flex: 1; }
  .tool { font-size: 12px; padding: 4px 10px; background: transparent; display: inline-flex; align-items: center; gap: 5px; }
  .tool.sq { padding: 5px 7px; color: var(--ink-2); }
  .tool.sq:hover { color: var(--ink); background: color-mix(in srgb, var(--ink) 6%, transparent); }
  .run { background: transparent; border: 1px solid color-mix(in srgb, var(--delta-good) 45%, transparent); color: var(--delta-good); font-weight: 600; font-size: 12.5px; padding: 4px 12px; display: inline-flex; align-items: center; gap: 5px; }
  .run:hover { background: color-mix(in srgb, var(--delta-good) 12%, transparent); border-color: var(--delta-good); }

  .paneroot { flex: 1; min-height: 0; display: flex; }
  .edhost { position: absolute; inset: 0; }
  .termhost { position: absolute; inset: 0; display: flex; flex-direction: column; overflow: auto; background: var(--surface); }
  .termph { color: var(--muted); font-size: 12.5px; line-height: 1.6; padding: 12px; }
  .termph kbd { font: 11px var(--mono); border: 1px solid var(--border); border-bottom-width: 2px; border-radius: 4px; padding: 0 4px; }
  .err { margin: 0 0 8px; color: var(--crit); font: 12px var(--mono); white-space: pre-wrap; }
  .kids { flex: none; display: flex; flex-wrap: wrap; align-items: center; gap: 6px; padding: 6px 8px; border-top: 1px solid var(--grid); }
  .kidlabel { font-size: 11px; color: var(--muted); }
  .kid { font-size: 11.5px; padding: 3px 10px; background: transparent; }

  .tlead { display: inline-flex; color: var(--muted); }
  .tlead.ic-savvy { color: var(--s1); } .tlead.ic-asm { color: var(--s4); } .tlead.ic-genes { color: var(--s5); }
  .tlead.accent { color: var(--accent); }
  .tabname { overflow: hidden; text-overflow: ellipsis; }
  .errdot { width: 7px; height: 7px; border-radius: 50%; background: var(--crit); }

  .statusbar {
    flex: none; display: flex; align-items: center; gap: 2px;
    background: var(--accent); color: var(--accent-ink);
    padding: 0 6px; min-height: 24px; font-size: 11.5px;
    padding-bottom: env(safe-area-inset-bottom);
    padding-left: max(6px, env(safe-area-inset-left));
    padding-right: max(6px, env(safe-area-inset-right));
  }
  .sb { flex: none; border: none; background: none; border-radius: 4px; color: var(--accent-ink); padding: 1px 8px; font-size: 11.5px; }
  .sb.ellip { flex: 0 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  button.sb:hover { background: rgba(255, 255, 255, 0.18); }
  .sb.alert { font-weight: 700; }
  .sb.live { background: rgba(255, 255, 255, 0.16); font-weight: 600; }
  .sb.mode { font-family: var(--mono); font-weight: 700; letter-spacing: 0.04em; }
  .sb.ico { display: inline-flex; align-items: center; gap: 4px; }
  .sb.ico.on { background: rgba(255, 255, 255, 0.22); }

  .toast {
    position: absolute; right: 18px; bottom: calc(34px + env(safe-area-inset-bottom));
    background: var(--ink); color: var(--page); padding: 7px 14px; border-radius: 8px;
    font-size: 12.5px; box-shadow: 0 8px 24px rgba(0, 0, 0, 0.3); z-index: 40;
  }

  @media (max-width: 820px) {
    .topbar { overflow-x: auto; scrollbar-width: none; }
    .topbar::-webkit-scrollbar { height: 0; }
    .statusbar { overflow-x: auto; scrollbar-width: none; }
    .statusbar::-webkit-scrollbar { height: 0; }
  }

  @media (max-width: 560px) {
    .topbar { gap: 2px; padding: 3px 6px; }
    .tool.sq { padding: 5px 6px; }
    .run { padding: 4px 10px; }
    .toast { left: 12px; right: 12px; text-align: center; }
  }

  @media (hover: none) {
    .tool.sq { padding: 8px 9px; }
    .run { padding: 7px 14px; }
    .kid { padding: 6px 12px; }
    .statusbar { min-height: 38px; }
    .sb { padding: 9px 10px; }
  }
</style>
