<script>
  import { tick } from "svelte";
  import Icon from "./Icon.svelte";

  let { x = 0, y = 0, items = [], onclose = () => {} } = $props();

  let el = $state();
  let px = $state(x);
  let py = $state(y);

  $effect(() => {
    (async () => {
      await tick();
      if (!el) return;
      const r = el.getBoundingClientRect();
      px = Math.min(x, window.innerWidth - r.width - 8);
      py = Math.min(y, window.innerHeight - r.height - 8);
    })();
  });

  function choose(it) {
    if (it.disabled) return;
    onclose();
    it.run?.();
  }
</script>

<svelte:window
  onkeydown={(e) => e.key === "Escape" && onclose()}
  onresize={onclose}
/>
<div class="cm-scrim" role="button" tabindex="-1" onpointerdown={onclose} oncontextmenu={(e) => { e.preventDefault(); onclose(); }} onkeydown={() => {}}></div>
<div class="ctxmenu" bind:this={el} style:left="{px}px" style:top="{py}px" role="menu">
  {#each items as it, i (i)}
    {#if it.sep}
      <div class="sep"></div>
    {:else}
      <button
        class="mi"
        class:danger={it.danger}
        role="menuitem"
        disabled={it.disabled}
        onmousedown={(e) => e.preventDefault()}
        onclick={() => choose(it)}
      >
        <span class="mi-ic">{#if it.icon}<Icon name={it.icon} size={14} />{/if}</span>
        <span class="mi-label">{it.label}</span>
        {#if it.kbd}<span class="mi-kbd">{it.kbd}</span>{/if}
      </button>
    {/if}
  {/each}
</div>

<style>
  .cm-scrim {
    position: fixed;
    inset: 0;
    z-index: 130;
  }
  .ctxmenu {
    position: fixed;
    z-index: 131;
    min-width: 190px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 10px;
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.32);
    padding: 5px;
    animation: cm-in 0.09s ease;
  }
  @keyframes cm-in {
    from {
      opacity: 0;
      transform: scale(0.97);
    }
  }
  .mi {
    display: flex;
    align-items: center;
    gap: 9px;
    width: 100%;
    border: none;
    background: none;
    border-radius: 6px;
    text-align: left;
    padding: 5px 9px;
    font-size: 12.5px;
    color: var(--ink-2);
  }
  .mi:hover:not(:disabled) {
    background: color-mix(in srgb, var(--accent) 18%, transparent);
    color: var(--ink);
  }
  .mi:disabled {
    opacity: 0.42;
    cursor: default;
  }
  .mi.danger:hover {
    background: color-mix(in srgb, var(--crit) 20%, transparent);
    color: var(--crit);
  }
  .mi-ic {
    display: inline-flex;
    width: 14px;
    color: var(--muted);
  }
  .mi:hover .mi-ic {
    color: inherit;
  }
  .mi-label {
    flex: 1;
  }
  .mi-kbd {
    font: 11px var(--mono);
    color: var(--muted);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0 5px;
  }
  .sep {
    height: 1px;
    background: var(--grid);
    margin: 4px 6px;
  }
</style>
