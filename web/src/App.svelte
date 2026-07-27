<script>
  import { app, init } from "./store.svelte.js";
  import "./lib/settings.svelte.js";
  import Workbench from "./pages/Workbench.svelte";
  import Menu from "./lib/Menu.svelte";

  init();

  const themeParam = new URLSearchParams(location.search).get("theme");
  let theme = $state(themeParam || localStorage.getItem("theme") || "");
  $effect(() => {
    if (theme) {
      document.documentElement.dataset.theme = theme;
      localStorage.setItem("theme", theme);
    } else {
      delete document.documentElement.dataset.theme;
    }
  });
  const isDark = $derived(
    theme ? theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches,
  );
  function toggleTheme() {
    theme = isDark ? "light" : "dark";
  }

  let ctx = $state(null);
  let ctxTarget = null;
  const isField = (el) => el?.tagName === "INPUT" || el?.tagName === "TEXTAREA";
  const isEditable = (el) =>
    isField(el) || el?.isContentEditable || !!el?.closest?.(".cm-content");

  function openCtx(e) {
    if (e.defaultPrevented) return;
    ctxTarget = e.target;
    const el = e.target;
    const editable = isEditable(el);
    const sel = window.getSelection?.();
    const hasSel =
      (isField(el) && el.selectionStart !== el.selectionEnd) ||
      !!(sel && !sel.isCollapsed && sel.toString());
    if (!editable && !hasSel) return;
    e.preventDefault();

    const items = [];
    if (hasSel)
      items.push({ label: "Copy", icon: "copy", kbd: "Ctrl C", run: () => exec("copy") });
    if (hasSel && editable)
      items.push({ label: "Cut", icon: "scissors", kbd: "Ctrl X", run: () => exec("cut") });
    if (editable) items.push({ label: "Paste", icon: "clipboard", kbd: "Ctrl V", run: paste });
    if (editable) items.push({ label: "Select all", kbd: "Ctrl A", run: selectAll });
    items.push({ sep: true });
    items.push({
      label: isDark ? "Light theme" : "Dark theme",
      icon: isDark ? "sun" : "moon",
      run: toggleTheme,
    });
    ctx = { x: e.clientX, y: e.clientY, items };
  }

  function exec(command) {
    try { document.execCommand(command); } catch { /* nothing selected */ }
  }
  function selectAll() {
    const el = ctxTarget;
    if (isField(el)) el.select?.();
    else { el?.focus?.(); try { document.execCommand("selectAll"); } catch { /* ignore */ } }
  }
  async function paste() {
    let text = "";
    try { text = await navigator.clipboard.readText(); } catch { return; }
    const el = ctxTarget;
    if (isField(el)) {
      const s = el.selectionStart ?? el.value.length;
      const e = el.selectionEnd ?? el.value.length;
      el.value = el.value.slice(0, s) + text + el.value.slice(e);
      el.selectionStart = el.selectionEnd = s + text.length;
      el.dispatchEvent(new Event("input", { bubbles: true }));
      el.focus();
    } else {
      el?.focus?.();
      try { document.execCommand("insertText", false, text); } catch { /* ignore */ }
    }
  }
</script>

<div class="shell" oncontextmenu={openCtx}>
  {#if app.offline}<div class="offline">backend offline — retrying…</div>{/if}
  <Workbench {isDark} onToggleTheme={toggleTheme} />
</div>

{#if ctx}
  <Menu x={ctx.x} y={ctx.y} items={ctx.items} onclose={() => (ctx = null)} />
{/if}

<style>
  .shell {
    height: 100vh;
    display: flex;
    flex-direction: column;
    background: var(--page);
  }
  .offline {
    flex: none;
    text-align: center;
    font-size: 12px;
    padding: 3px;
    color: var(--crit);
    background: color-mix(in srgb, var(--crit) 12%, transparent);
    border-bottom: 1px solid color-mix(in srgb, var(--crit) 40%, transparent);
  }
</style>
