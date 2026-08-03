<script>
  import Pane from "./Pane.svelte";
  import Icon from "./Icon.svelte";
  import { L, moveKey, activate, closeKey, setRatio } from "./layout.svelte.js";
  import { ui } from "./media.svelte.js";

  let { node, tab, body } = $props();

  let dropZone = $state(null);

  function onTabDragStart(e, key) {
    L.drag = { key };
    try { e.dataTransfer.effectAllowed = "move"; e.dataTransfer.setData("application/x-shrewd-tab", key); } catch {}
  }
  const onTabDragEnd = () => { L.drag = null; dropZone = null; };

  function zoneAt(e) {
    const r = e.currentTarget.getBoundingClientRect();
    const x = (e.clientX - r.left) / r.width, y = (e.clientY - r.top) / r.height;
    const dL = x, dR = 1 - x, dT = y, dB = 1 - y, m = Math.min(dL, dR, dT, dB);
    if (m >= 0.25) return "center";
    return m === dL ? "left" : m === dR ? "right" : m === dT ? "top" : "bottom";
  }
  function onBodyOver(e) {
    if (!L.drag) return;
    e.preventDefault();
    const z = zoneAt(e);
    if (dropZone !== z) dropZone = z;
  }
  function onBodyDrop(e) {
    if (!L.drag) return;
    e.preventDefault();
    e.stopPropagation();
    moveKey(L.drag.key, node.id, zoneAt(e));
    L.drag = null; dropZone = null;
  }

  function onStripDrop(e) {
    if (!L.drag) return;
    e.preventDefault();
    e.stopPropagation();
    const tabs = [...e.currentTarget.querySelectorAll(".ptab")];
    let idx = tabs.length;
    for (let i = 0; i < tabs.length; i++) {
      const tr = tabs[i].getBoundingClientRect();
      if (e.clientX < tr.left + tr.width / 2) { idx = i; break; }
    }
    moveKey(L.drag.key, node.id, idx);
    L.drag = null; dropZone = null;
  }

  function startResize(e, sp) {
    e.preventDefault();
    const cont = e.currentTarget.parentElement.getBoundingClientRect();
    const move = (ev) => {
      let ratio = sp.dir === "row"
        ? (ev.clientX - cont.left) / cont.width
        : (ev.clientY - cont.top) / cont.height;
      setRatio(sp.id, Math.max(0.1, Math.min(0.9, ratio)));
    };
    const up = () => { removeEventListener("pointermove", move); removeEventListener("pointerup", up); };
    addEventListener("pointermove", move);
    addEventListener("pointerup", up);
  }
</script>

{#if node.type === "split"}
  <div class="psplit {node.dir}">
    <div class="pchild" style:flex-grow={node.ratio}><Pane node={node.a} {tab} {body} /></div>
    <div class="presize {node.dir}" role="separator" onpointerdown={(e) => startResize(e, node)}></div>
    <div class="pchild" style:flex-grow={1 - node.ratio}><Pane node={node.b} {tab} {body} /></div>
  </div>
{:else}
  <div class="pleaf" class:focused={L.focused === node.id}>
    <div
      class="pstrip"
      role="tablist"
      tabindex="-1"
      ondragover={(e) => L.drag && e.preventDefault()}
      ondrop={onStripDrop}
    >
      {#each node.keys as key (key)}
        <div
          class="ptab"
          class:cur={node.active === key}
          role="tab"
          tabindex="0"
          draggable={!ui.touch}
          aria-selected={node.active === key}
          ondragstart={(e) => onTabDragStart(e, key)}
          ondragend={onTabDragEnd}
          onclick={() => activate(node.id, key)}
          onkeydown={(e) => e.key === "Enter" && activate(node.id, key)}
          onauxclick={(e) => e.button === 1 && closeKey(key)}
        >
          {@render tab(key)}
          <button class="ptclose" title="close" onclick={(e) => { e.stopPropagation(); closeKey(key); }}
            ><Icon name="close" size={12} /></button>
        </div>
      {/each}
    </div>

    <div class="pbody" role="presentation" onpointerdowncapture={() => (L.focused = node.id)}>
      {#if node.active}
        {#key node.active}<div class="pitem">{@render body(node.active, node)}</div>{/key}
      {:else}
        <div class="pempty">drag a tab here</div>
      {/if}
      {#if L.drag}
        <div
          class="pdrop"
          role="presentation"
          ondragover={onBodyOver}
          ondragleave={() => (dropZone = null)}
          ondrop={onBodyDrop}
        >
          {#if dropZone}<div class="dropind z-{dropZone}"></div>{/if}
        </div>
      {/if}
    </div>
  </div>
{/if}

<style>
  .psplit { display: flex; height: 100%; width: 100%; min-width: 0; min-height: 0; }
  .psplit.row { flex-direction: row; }
  .psplit.col { flex-direction: column; }
  .pchild { flex-shrink: 1; flex-basis: 0; min-width: 0; min-height: 0; overflow: hidden; display: flex; }
  .presize { flex: none; background: transparent; z-index: 6; transition: background 0.15s; touch-action: none; }
  .presize.row { width: 6px; margin: 0 -3px; cursor: col-resize; }
  .presize.col { height: 6px; margin: -3px 0; cursor: row-resize; }
  .presize:hover { background: color-mix(in srgb, var(--accent) 45%, transparent); }
  @media (pointer: coarse) {
    .presize.row { width: 16px; margin: 0 -8px; }
    .presize.col { height: 16px; margin: -8px 0; }
  }

  .pleaf { flex: 1; display: flex; flex-direction: column; min-width: 0; min-height: 0; background: var(--page); }
  .pstrip {
    flex: none; display: flex; overflow-x: auto; scrollbar-width: none;
    background: var(--surface); border-bottom: 1px solid var(--border); min-height: 34px;
  }
  .pstrip::-webkit-scrollbar { height: 0; }
  .ptab {
    position: relative; display: inline-flex; align-items: center; gap: 6px;
    border-right: 1px solid var(--grid); padding: 0 6px 0 12px;
    font: 12px var(--mono); color: var(--muted); white-space: nowrap; cursor: pointer;
    box-shadow: inset 0 -2px 0 transparent;
  }
  .ptab:hover { color: var(--ink-2); background: color-mix(in srgb, var(--ink) 3%, transparent); }
  .ptab.cur { background: var(--page); color: var(--ink); }
  .pleaf.focused .ptab.cur { box-shadow: inset 0 -2px 0 var(--accent); }
  .ptclose { border: none; background: none; padding: 0 2px; color: transparent; border-radius: 4px; line-height: 1; }
  .ptab:hover .ptclose, .ptab.cur .ptclose { color: var(--muted); }
  .ptclose:hover { color: var(--ink); background: color-mix(in srgb, var(--ink) 10%, transparent); }

  @media (hover: none) {
    .pstrip { min-height: 42px; }
    .ptab { padding: 0 4px 0 14px; font-size: 13px; }
    .ptclose { color: var(--muted); padding: 8px 6px; }
  }

  .pbody { flex: 1; min-height: 0; position: relative; }
  .pitem { position: absolute; inset: 0; }
  .pempty { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; color: var(--muted); font-size: 12.5px; }
  .pdrop { position: absolute; inset: 0; z-index: 15; }

  .dropind {
    position: absolute; pointer-events: none; z-index: 20;
    background: color-mix(in srgb, var(--accent) 22%, transparent);
    border: 1.5px solid var(--accent); border-radius: 4px;
  }
  .dropind.z-center { inset: 3px; }
  .dropind.z-left { top: 3px; bottom: 3px; left: 3px; width: calc(50% - 3px); }
  .dropind.z-right { top: 3px; bottom: 3px; right: 3px; width: calc(50% - 3px); }
  .dropind.z-top { left: 3px; right: 3px; top: 3px; height: calc(50% - 3px); }
  .dropind.z-bottom { left: 3px; right: 3px; bottom: 3px; height: calc(50% - 3px); }
</style>
