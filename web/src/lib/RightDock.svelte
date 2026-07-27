<script>
  import CodeEditor from "./CodeEditor.svelte";
  import GeneMap from "./GeneMap.svelte";
  import Icon from "./Icon.svelte";
  import { parseGenes, geneDiff, GENE_LEGEND } from "./genes.js";
  import { langForName } from "./cm.js";
  import { api } from "../store.svelte.js";

  let {
    source = "",
    name = "main.savvy",
    files = {},
    entry = "",
    isa = [],
    openFiles = [],
    view = "compiler",
    embed = false,
    onview = () => {},
    onclose = () => {},
  } = $props();

  const lang = $derived(langForName(name));
  function payload(src) {
    if (lang !== "savvy") return { files: { [name]: src }, entry: name };
    return { files: { ...files, [name]: src }, entry: entry || name };
  }

  let build = $state(null);
  let err = $state("");
  let fmt = $state("asm");
  let ct;
  $effect(() => {
    const src = source;
    void name;
    clearTimeout(ct);
    const p = payload(src);
    ct = setTimeout(async () => {
      try {
        const r = await api("/api/build", p);
        if (r.ok) { build = r; err = ""; } else { build = null; err = r.error; }
      } catch { /* offline */ }
    }, 250);
    return () => clearTimeout(ct);
  });
  const genes = $derived(build ? parseGenes(build.genes) : []);

  let showLegend = $state(false);
  let diffName = $state("");
  let diffGenes = $state([]);
  $effect(() => {
    const target = openFiles.find((f) => f.name === diffName);
    if (!target) { diffGenes = []; return; }
    let cancelled = false;
    (async () => {
      try {
        const r = await api("/api/build",
          langForName(target.name) !== "savvy"
            ? { files: { [target.name]: target.src }, entry: target.name }
            : { files: { ...files, [target.name]: target.src }, entry: target.name });
        if (!cancelled && r.ok) diffGenes = parseGenes(r.genes);
      } catch { /* ignore */ }
    })();
    return () => { cancelled = true; };
  });
  const diff = $derived(diffName && diffGenes.length ? geneDiff(genes, diffGenes) : null);

  let trace = $state(null);
  let step = $state(0);
  let dbgInput = $state("");
  let dbgSeed = $state(0);
  let running = $state(false);
  let breakpoints = $state(new Set());
  let codeEl = $state();

  async function runTrace() {
    running = true;
    try {
      const r = await api("/api/trace", { ...payload(source), input: dbgInput, seed: +dbgSeed, cap: 4000 });
      if (r.ok) { trace = r; step = 0; err = ""; }
      else { trace = null; err = r.error; }
    } catch { /* offline */ }
    running = false;
  }
  const cur = $derived(trace && trace.trace[step] ? trace.trace[step] : null);
  const curCode = $derived.by(() => {
    if (!cur || !trace) return null;
    return trace.code.find((c) => c.pc === cur.pc) ?? null;
  });
  const mem = $derived.by(() => {
    if (!trace) return [];
    const m = new Map();
    for (let j = 0; j < step; j++) {
      const w = trace.trace[j];
      if (w.wa !== undefined) m.set(w.wa, w.wv);
    }
    return [...m.entries()].sort((a, b) => a[0] - b[0]);
  });
  const opName = (op) => isa[op]?.name ?? "·";

  function stepBy(d) {
    if (!trace) return;
    step = Math.max(0, Math.min(trace.trace.length - 1, step + d));
    scrollCur();
  }
  function toBreakpoint(dir) {
    if (!trace) return;
    for (let i = step + dir; i >= 0 && i < trace.trace.length; i += dir)
      if (breakpoints.has(trace.trace[i].pc)) { step = i; scrollCur(); return; }
    step = dir > 0 ? trace.trace.length - 1 : 0;
    scrollCur();
  }
  function toggleBp(pc) {
    const s = new Set(breakpoints);
    s.has(pc) ? s.delete(pc) : s.add(pc);
    breakpoints = s;
  }
  function scrollCur() {
    requestAnimationFrame(() => codeEl?.querySelector(".dline.cur")?.scrollIntoView({ block: "nearest" }));
  }
  const asChar = (v) => {
    const b = ((v % 256) + 256) % 256;
    return b >= 32 && b < 127 ? String.fromCharCode(b) : "·";
  };
</script>

<div class="dock" class:embed>
  {#if !embed}
    <div class="dhead">
      {#each [["compiler", "build", "compiler"], ["inspector", "dna", "genome"], ["debug", "play", "debug"]] as [id, icon, label] (id)}
        <button class="dtab" class:on={view === id} onclick={() => onview(id)}>
          <Icon name={icon} size={13} /> {label}
        </button>
      {/each}
      <span class="grow"></span>
      {#if build}<span class="len num">{build.len}g</span>{/if}
      <button class="x" title="close dock" onclick={onclose}><Icon name="close" size={14} /></button>
    </div>
  {/if}

  {#if err && view !== "debug"}
    <pre class="err">{err}</pre>
  {/if}

  {#if view === "compiler"}
    <div class="fmtseg">
      {#each ["savvy", "asm", "genes"] as f (f)}
        <button class:on={fmt === f} onclick={() => (fmt = f)}>{f === "asm" ? "assembly" : f}</button>
      {/each}
    </div>
    <div class="cmhost">
      {#if build}
        {#key fmt}
          <CodeEditor value={build[fmt]} lang={fmt} readonly fill isa={() => isa} />
        {/key}
      {:else if !err}
        <div class="hint">compiling…</div>
      {/if}
    </div>

  {:else if view === "inspector"}
    <div class="inspbody">
      <div class="irow">
        <span class="ilabel">genome</span>
        <div class="istrip"><GeneMap {genes} {isa} height={22} /></div>
        <button class="mini" class:on={showLegend} onclick={() => (showLegend = !showLegend)}>legend</button>
      </div>
      {#if showLegend}
        <div class="legend">
          {#each GENE_LEGEND as f (f.name)}
            <span class="lg"><span class="sw" style:background={f.color}></span>{f.name}</span>
          {/each}
        </div>
      {/if}
      <label class="diffsel">
        <span class="ilabel">diff vs</span>
        <select bind:value={diffName}>
          <option value="">— none —</option>
          {#each openFiles.filter((f) => f.name !== name) as f (f.name)}<option value={f.name}>{f.name}</option>{/each}
        </select>
        {#if diff}<span class="dcount" class:none={diff.size === 0}>{diff.size === 0 ? "identical" : diff.size + " changed"}</span>{/if}
      </label>
      {#if diffName && diffGenes.length}
        <div class="irow">
          <span class="ilabel">{diffName.replace(/^.*\//, "")}</span>
          <div class="istrip"><GeneMap genes={diffGenes} {isa} {diff} height={22} /></div>
        </div>
      {/if}
      <div class="disasm" role="list">
        {#each genes.map((g, i) => ({ g, i })) as { g, i } (i)}
          <div class="dline" role="listitem"><span class="pc num">{i}</span><span class="on-name">{opName(g % (isa.length || 1))}</span><span class="graw num">{g}</span></div>
        {/each}
      </div>
    </div>

  {:else}
    <div class="dbgbar">
      <button class="run" onclick={runTrace} disabled={running}><Icon name="play" size={12} /> {running ? "…" : "trace"}</button>
      <label class="di" title="stdin fed to input()">in <input type="text" bind:value={dbgInput} placeholder="input" /></label>
      <label class="di" title="rand() seed">seed <input class="num" type="number" bind:value={dbgSeed} /></label>
    </div>
    {#if err}<pre class="err">{err}</pre>{/if}
    {#if trace}
      <div class="dctl">
        <button class="sbtn" title="first" onclick={() => { step = 0; scrollCur(); }}>⏮</button>
        <button class="sbtn" title="prev breakpoint" onclick={() => toBreakpoint(-1)}>◂◂</button>
        <button class="sbtn" title="step back" onclick={() => stepBy(-1)}>◂</button>
        <input class="scrub" type="range" min="0" max={trace.trace.length - 1} bind:value={step} onchange={scrollCur} />
        <button class="sbtn" title="step" onclick={() => stepBy(1)}>▸</button>
        <button class="sbtn" title="next breakpoint" onclick={() => toBreakpoint(1)}>▸▸</button>
        <button class="sbtn" title="last" onclick={() => { step = trace.trace.length - 1; scrollCur(); }}>⏭</button>
        <span class="stepn num">{step + 1}/{trace.trace.length}</span>
      </div>
      <div class="dbgmain">
        <div class="code" bind:this={codeEl} role="list">
          {#each trace.code as c (c.pc)}
            <div class="dline" class:cur={cur && cur.pc === c.pc} class:bp={breakpoints.has(c.pc)} role="listitem"
              onclick={() => toggleBp(c.pc)}>
              <span class="bpdot"></span>
              <span class="pc num">{c.pc}</span>
              <span class="on-name">{c.name}</span>
              {#if c.arg !== undefined}<span class="arg num">{c.arg}</span>{/if}
            </div>
          {/each}
        </div>
        <div class="watch">
          <div class="wsec"><span class="wh">registers</span>
            <div class="regs">{#each (cur?.r ?? []) as v, i (i)}<span class="reg"><b>r{i}</b> {v}</span>{/each}</div>
          </div>
          <div class="wsec"><span class="wh">stack <span class="muted">({cur?.d ?? 0})</span></span>
            <div class="stack">
              {#if !cur || cur.s.length === 0}<span class="muted">empty</span>{/if}
              {#each (cur?.s ?? []).slice().reverse() as v, i (i)}<span class="cellv" class:top={i === 0}>{v}</span>{/each}
              {#if cur && cur.d > cur.s.length}<span class="muted more">+{cur.d - cur.s.length} more</span>{/if}
            </div>
          </div>
          <div class="wsec grow"><span class="wh">memory <span class="muted">(written)</span></span>
            <div class="memtab">
              {#if mem.length === 0}<span class="muted">— nothing written —</span>{/if}
              {#each mem as [a, v] (a)}<div class="memrow"><span class="ma num">[{a}]</span><span class="mv num">{v}</span></div>{/each}
            </div>
          </div>
          <div class="wsec"><span class="wh">output <span class="muted">({cur?.out ?? 0} bytes)</span></span>
            <div class="outp mono">{#if trace.output.length}{trace.output.slice(0, cur?.out ?? 0).map(asChar).join("")}{:else}<span class="muted">none</span>{/if}</div>
          </div>
        </div>
      </div>
      <div class="dfoot">halt: {trace.halt} · {trace.steps} steps{breakpoints.size ? ` · ${breakpoints.size} breakpoint${breakpoints.size === 1 ? "" : "s"}` : ""}</div>
    {:else if !running}
      <div class="hint">press <b>trace</b> to run this program step-by-step and watch the stack, registers, memory and output.</div>
    {/if}
  {/if}
</div>

<style>
  .dock { flex: 1; display: flex; flex-direction: column; height: 100%; min-width: 0; background: var(--surface); border-left: 1px solid var(--border); }
  .dock.embed { border-left: none; }
  .dhead { flex: none; display: flex; align-items: center; gap: 2px; padding: 3px 6px 0; border-bottom: 1px solid var(--grid); }
  .dtab { display: inline-flex; align-items: center; gap: 5px; border: none; background: none; color: var(--muted); font-size: 11.5px; padding: 5px 9px 6px; box-shadow: inset 0 -2px 0 transparent; }
  .dtab.on { color: var(--ink); box-shadow: inset 0 -2px 0 var(--accent); }
  .grow { flex: 1; }
  .len { font-size: 11px; color: var(--muted); padding-right: 4px; }
  .x { border: none; background: none; color: var(--muted); padding: 2px 5px; border-radius: 5px; }
  .x:hover { color: var(--ink); background: color-mix(in srgb, var(--ink) 8%, transparent); }
  .err { margin: 6px 8px; color: var(--crit); font: 11.5px var(--mono); white-space: pre-wrap; }
  .hint { color: var(--muted); font-size: 12px; padding: 12px; line-height: 1.6; }

  .fmtseg { flex: none; display: flex; gap: 3px; padding: 6px 8px; }
  .fmtseg button { border-radius: 99px; padding: 2px 11px; font-size: 12px; background: transparent; }
  .fmtseg button.on { background: var(--accent); border-color: var(--accent); color: var(--accent-ink); font-weight: 600; }
  .cmhost { flex: 1; min-height: 0; position: relative; }

  .inspbody { flex: 1; min-height: 0; overflow: auto; display: flex; flex-direction: column; gap: 8px; padding: 10px; }
  .irow { display: flex; align-items: center; gap: 8px; }
  .ilabel { flex: none; width: 66px; font-size: 10.5px; color: var(--muted); text-transform: uppercase; letter-spacing: 0.05em; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .istrip { flex: 1; min-width: 0; }
  .mini { flex: none; font-size: 11px; padding: 2px 8px; background: transparent; }
  .mini.on { color: var(--accent); border-color: var(--accent); }
  .legend { display: flex; flex-wrap: wrap; gap: 4px 12px; font-size: 11px; color: var(--ink-2); padding-left: 74px; }
  .lg { display: inline-flex; align-items: center; gap: 5px; }
  .sw { width: 10px; height: 10px; border-radius: 3px; }
  .diffsel { display: flex; align-items: center; gap: 8px; font-size: 12px; }
  .diffsel select { flex: 1; min-width: 0; padding: 3px 6px; font-size: 12px; }
  .dcount { flex: none; font-size: 11px; color: var(--crit); }
  .dcount.none { color: var(--muted); }
  .disasm { border: 1px solid var(--border); border-radius: 6px; overflow: auto; }
  .disasm .dline { display: flex; gap: 8px; padding: 1px 8px; font: 11.5px var(--mono); }
  .disasm .dline:nth-child(even) { background: color-mix(in srgb, var(--ink) 3%, transparent); }
  .disasm .pc { color: var(--muted); min-width: 34px; text-align: right; }
  .on-name { color: var(--ink); }
  .graw { margin-left: auto; color: var(--muted); }

  .dbgbar { flex: none; display: flex; align-items: center; gap: 8px; padding: 7px 8px; border-bottom: 1px solid var(--grid); }
  .dbgbar .run { display: inline-flex; align-items: center; gap: 5px; background: transparent; border: 1px solid color-mix(in srgb, var(--delta-good) 45%, transparent); color: var(--delta-good); font-weight: 600; font-size: 12px; padding: 3px 11px; }
  .di { display: inline-flex; align-items: center; gap: 4px; font-size: 11px; color: var(--muted); }
  .di input { width: 84px; padding: 2px 6px; font-size: 11.5px; }
  .di input.num { width: 56px; }
  .dctl { flex: none; display: flex; align-items: center; gap: 4px; padding: 5px 8px; }
  .sbtn { border: 1px solid var(--border); background: transparent; border-radius: 5px; padding: 2px 7px; font-size: 12px; color: var(--ink-2); }
  .sbtn:hover { color: var(--ink); border-color: var(--accent); }
  .scrub { flex: 1; min-width: 0; }
  .stepn { flex: none; font-size: 11px; color: var(--muted); min-width: 52px; text-align: right; }

  .dbgmain { flex: 1; min-height: 0; display: flex; }
  .code { flex: 1.1; min-width: 0; overflow: auto; border-right: 1px solid var(--grid); }
  .code .dline { display: flex; align-items: center; gap: 8px; padding: 0 8px 0 4px; font: 12px var(--mono); cursor: pointer; }
  .code .dline:hover { background: color-mix(in srgb, var(--ink) 5%, transparent); }
  .code .dline.cur { background: color-mix(in srgb, var(--accent) 22%, transparent); }
  .code .dline.bp .bpdot { background: var(--crit); }
  .bpdot { flex: none; width: 7px; height: 7px; border-radius: 50%; background: transparent; }
  .code .pc { color: var(--muted); min-width: 30px; text-align: right; }
  .code .arg { color: var(--s5); }
  .watch { flex: 1; min-width: 0; display: flex; flex-direction: column; overflow: auto; }
  .wsec { flex: none; padding: 6px 8px; border-bottom: 1px solid var(--grid); }
  .wsec.grow { flex: 1; min-height: 40px; display: flex; flex-direction: column; overflow: hidden; }
  .wsec.grow .memtab { flex: 1; overflow: auto; }
  .wh { font-size: 10px; color: var(--muted); text-transform: uppercase; letter-spacing: 0.06em; }
  .regs { display: flex; flex-wrap: wrap; gap: 4px 10px; margin-top: 3px; font: 12px var(--mono); }
  .reg b { color: var(--muted); font-weight: 600; margin-right: 2px; }
  .stack { display: flex; flex-wrap: wrap; gap: 3px; margin-top: 3px; }
  .cellv { font: 11.5px var(--mono); padding: 1px 6px; border: 1px solid var(--border); border-radius: 4px; }
  .cellv.top { border-color: var(--accent); color: var(--accent); }
  .more { align-self: center; }
  .memtab { margin-top: 3px; font: 11.5px var(--mono); }
  .memrow { display: flex; justify-content: space-between; padding: 0 2px; }
  .memrow .ma { color: var(--muted); }
  .outp { margin-top: 3px; font-size: 12px; white-space: pre-wrap; word-break: break-all; background: var(--page); border-radius: 5px; padding: 4px 6px; min-height: 18px; }
  .dfoot { flex: none; font-size: 11px; color: var(--muted); padding: 4px 8px; border-top: 1px solid var(--grid); }
  .muted { color: var(--muted); }
</style>
