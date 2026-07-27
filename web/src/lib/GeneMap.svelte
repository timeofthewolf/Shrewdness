<script>
  import { geneCells } from "./genes.js";

  let {
    genes = [],
    isa = [],
    diff = null,
    height = 20,
    onhover = null,
  } = $props();

  let hover = $state(null);
  const cells = $derived(geneCells(genes, isa));

  function enter(c) {
    hover = c;
    onhover?.(c);
  }
</script>

<div class="gm">
  <div
    class="strip"
    style:height="{height}px"
    role="img"
    aria-label="genome map, {genes.length} genes"
    onmouseleave={() => (hover = null)}
  >
    {#each cells as c (c.i)}
      <span
        class="cell {c.kind}"
        class:changed={diff?.has(c.i)}
        class:on={hover?.i === c.i}
        style:background={c.color}
        onmouseenter={() => enter(c)}
      ></span>
    {/each}
    {#if cells.length === 0}<span class="empty">no genes</span>{/if}
  </div>
  {#if hover}
    <div class="caption mono">
      <span class="idx">#{hover.i}</span>
      {#if hover.kind === "op"}
        <span class="op" style:color={hover.color}>{hover.name}</span>
        <span class="muted">{hover.family}</span>
      {:else}
        <span class="muted">literal</span>
      {/if}
      <span class="val">= {hover.g}</span>
    </div>
  {/if}
</div>

<style>
  .gm {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }
  .strip {
    display: flex;
    gap: 1px;
    overflow-x: auto;
    overflow-y: hidden;
    scrollbar-width: none;
    border-radius: 5px;
    background: var(--grid);
  }
  .strip::-webkit-scrollbar {
    height: 0;
  }
  .cell {
    flex: 1 1 0;
    min-width: 2px;
    transition: transform 0.08s, filter 0.08s;
  }
  .cell.lit {
    filter: saturate(0.4);
  }
  .cell.on {
    filter: brightness(1.3);
    transform: scaleY(1.15);
  }
  .cell.changed {
    box-shadow: inset 0 0 0 1.5px var(--crit);
  }
  .empty {
    color: var(--muted);
    font-size: 11px;
    padding: 0 6px;
  }
  .caption {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 11px;
    min-height: 14px;
  }
  .idx {
    color: var(--muted);
  }
  .op {
    font-weight: 600;
  }
  .val {
    color: var(--ink-2);
  }
</style>
