<script>
  let { title = "", onclose = () => {}, wide = false, children } = $props();

  function onKey(e) {
    if (e.key === "Escape") {
      e.stopPropagation();
      onclose();
    }
  }
</script>

<svelte:window onkeydown={onKey} />

<div
  class="backdrop"
  role="button"
  tabindex="-1"
  onclick={(e) => e.target === e.currentTarget && onclose()}
  onkeydown={() => {}}
>
  <div class="modal" class:wide role="dialog" aria-modal="true" aria-label={title}>
    <header>
      <h2>{title}</h2>
      <button class="x" onclick={onclose} title="close (Esc)">✕</button>
    </header>
    <div class="content">
      {@render children()}
    </div>
  </div>
</div>

<style>
  .backdrop {
    position: fixed;
    inset: 0;
    z-index: 100;
    background: color-mix(in srgb, #000 42%, transparent);
    backdrop-filter: blur(2px);
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    animation: fade 0.12s ease;
  }
  @keyframes fade {
    from {
      opacity: 0;
    }
  }
  .modal {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 14px;
    box-shadow: 0 24px 70px rgba(0, 0, 0, 0.4);
    width: min(560px, 100%);
    max-height: min(82vh, 720px);
    display: flex;
    flex-direction: column;
    animation: pop 0.14s cubic-bezier(0.2, 0.9, 0.3, 1.2);
  }
  .modal.wide {
    width: min(880px, 100%);
  }
  @keyframes pop {
    from {
      transform: translateY(8px) scale(0.98);
      opacity: 0;
    }
  }
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 18px;
    border-bottom: 1px solid var(--grid);
  }
  header h2 {
    font-size: 15px;
  }
  .x {
    border: none;
    background: none;
    color: var(--muted);
    font-size: 15px;
    padding: 2px 6px;
    border-radius: 6px;
  }
  .x:hover {
    color: var(--ink);
    background: color-mix(in srgb, var(--ink) 8%, transparent);
  }
  .content {
    overflow: auto;
    padding: 16px 18px 20px;
  }
</style>
