<script>
  import { tick } from "svelte";
  import Icon from "./Icon.svelte";

  let { items = [], placeholder = "", onclose = () => {} } = $props();

  let query = $state("");
  let sel = $state(0);
  let inputEl = $state();

  function score(label, q) {
    if (!q) return 0;
    label = label.toLowerCase();
    q = q.toLowerCase();
    let i = 0, s = 0, streak = 0;
    for (const ch of label) {
      if (i < q.length && ch === q[i]) {
        i++;
        streak++;
        s += 1 + streak;
      } else streak = 0;
    }
    return i === q.length ? s : -1;
  }

  const filtered = $derived.by(() => {
    if (!query) return items.slice(0, 60);
    return items
      .map((it) => ({ it, sc: score(it.label + " " + (it.sub || ""), query) }))
      .filter((x) => x.sc >= 0)
      .sort((a, b) => b.sc - a.sc)
      .slice(0, 60)
      .map((x) => x.it);
  });

  $effect(() => {
    filtered;
    if (sel >= filtered.length) sel = Math.max(0, filtered.length - 1);
  });

  async function mount(node) {
    await tick();
    node.focus();
  }
  function pick(it) {
    if (!it) return;
    onclose();
    it.run();
  }
  function onKey(e) {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      sel = Math.min(filtered.length - 1, sel + 1);
      scrollSel();
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      sel = Math.max(0, sel - 1);
      scrollSel();
    } else if (e.key === "Enter") {
      e.preventDefault();
      pick(filtered[sel]);
    } else if (e.key === "Escape") {
      e.preventDefault();
      onclose();
    }
  }
  function scrollSel() {
    document.querySelector(".pal-item.sel")?.scrollIntoView({ block: "nearest" });
  }
</script>

<div class="pal-backdrop" role="button" tabindex="-1"
  onclick={(e) => e.target === e.currentTarget && onclose()} onkeydown={() => {}}>
  <div class="pal" role="dialog" aria-label="command palette">
    <div class="pal-search">
      <Icon name="search" size={15} />
      <input
        bind:this={inputEl}
        use:mount
        {placeholder}
        bind:value={query}
        onkeydown={onKey}
        oninput={() => (sel = 0)}
        spellcheck="false"
      />
    </div>
    <div class="pal-list">
      {#each filtered as it, i (it.label + i)}
        <button class="pal-item" class:sel={i === sel}
          onmousemove={() => (sel = i)} onclick={() => pick(it)}>
          {#if it.icon}<span class="pal-ic"><Icon name={it.icon} size={15} /></span>{/if}
          <span class="pal-label">{it.label}</span>
          {#if it.sub}<span class="pal-sub">{it.sub}</span>{/if}
          {#if it.kbd}<span class="pal-kbd">{it.kbd}</span>{/if}
        </button>
      {:else}
        <div class="pal-empty">no matches</div>
      {/each}
    </div>
  </div>
</div>

<style>
  .pal-backdrop {
    position: fixed;
    inset: 0;
    z-index: 120;
    background: color-mix(in srgb, #000 32%, transparent);
    backdrop-filter: blur(2px);
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding-top: 12vh;
    animation: pf 0.1s ease;
  }
  @keyframes pf {
    from { opacity: 0; }
  }
  .pal {
    width: min(620px, 92vw);
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    box-shadow: 0 24px 70px rgba(0, 0, 0, 0.4);
    overflow: hidden;
    animation: pp 0.12s cubic-bezier(0.2, 0.9, 0.3, 1.1);
  }
  @keyframes pp {
    from { transform: translateY(-6px); opacity: 0.6; }
  }
  .pal-search {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 14px;
    border-bottom: 1px solid var(--grid);
    color: var(--muted);
  }
  .pal-search input {
    flex: 1;
    border: none;
    background: none;
    padding: 2px 0;
    font-size: 14px;
    color: var(--ink);
  }
  .pal-search input:focus-visible {
    outline: none;
  }
  .pal-list {
    max-height: 52vh;
    overflow: auto;
    padding: 6px;
  }
  .pal-item {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
    border: none;
    background: none;
    border-radius: 8px;
    text-align: left;
    padding: 7px 10px;
    color: var(--ink-2);
    font-size: 13px;
  }
  .pal-item.sel {
    background: color-mix(in srgb, var(--accent) 18%, transparent);
    color: var(--ink);
  }
  .pal-ic {
    display: inline-flex;
    color: var(--muted);
  }
  .pal-item.sel .pal-ic {
    color: var(--accent);
  }
  .pal-label {
    flex: none;
  }
  .pal-sub {
    color: var(--muted);
    font-size: 11.5px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .pal-kbd {
    margin-left: auto;
    font: 11px var(--mono);
    color: var(--muted);
    border: 1px solid var(--border);
    border-radius: 5px;
    padding: 1px 6px;
  }
  .pal-empty {
    padding: 16px;
    text-align: center;
    color: var(--muted);
    font-size: 13px;
  }
</style>
