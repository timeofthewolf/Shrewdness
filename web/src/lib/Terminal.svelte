<script>
  let {
    log = [],
    status = "running",
    onsubmit = () => {},
    oneof = () => {},
    onstop = () => {},
    onclear = () => {},
    oncontext = null,
  } = $props();

  let pending = $state("");
  let focused = $state(false);
  let el = $state();
  let history = [];
  let histIdx = -1;

  const live = $derived(status !== "done");

  $effect(() => {
    log.length;
    pending;
    status;
    if (el) el.scrollTop = el.scrollHeight;
  });

  export function focus() {
    el?.focus();
  }

  function submit() {
    const line = pending;
    if (line.trim() !== "" || history[history.length - 1] !== line)
      if (line !== "") history.push(line);
    histIdx = -1;
    pending = "";
    onsubmit(line);
  }

  function onKey(e) {
    if (!live) return;
    const mod = e.ctrlKey || e.metaKey;

    if (mod && (e.key === "c" || e.key === "C")) {
      if (window.getSelection()?.toString()) return;
      e.preventDefault();
      onstop();
      return;
    }
    if (mod && (e.key === "d" || e.key === "D")) {
      e.preventDefault();
      if (pending === "") oneof();
      return;
    }
    if (mod && (e.key === "l" || e.key === "L")) {
      e.preventDefault();
      onclear();
      return;
    }
    if (mod && (e.key === "u" || e.key === "U")) {
      e.preventDefault();
      pending = "";
      return;
    }
    if (mod || e.altKey) return;

    if (e.key === "Enter") {
      e.preventDefault();
      submit();
    } else if (e.key === "Backspace") {
      e.preventDefault();
      pending = pending.slice(0, -1);
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      if (history.length) {
        histIdx = histIdx < 0 ? history.length - 1 : Math.max(0, histIdx - 1);
        pending = history[histIdx];
      }
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      if (histIdx >= 0) {
        histIdx++;
        if (histIdx >= history.length) {
          histIdx = -1;
          pending = "";
        } else pending = history[histIdx];
      }
    } else if (e.key.length === 1) {
      e.preventDefault();
      pending += e.key;
    } else if (e.key === "Tab") {
      e.preventDefault();
    }
  }

  function onPaste(e) {
    if (!live) return;
    e.preventDefault();
    const text = (e.clipboardData || window.clipboardData).getData("text");
    if (!text) return;
    const parts = text.split("\n");
    for (let i = 0; i < parts.length - 1; i++) {
      pending += parts[i];
      submit();
    }
    pending += parts[parts.length - 1];
  }
</script>

<div
  class="term"
  class:done={!live}
  bind:this={el}
  tabindex="0"
  role="textbox"
  aria-label="program console"
  aria-multiline="true"
  onkeydown={onKey}
  onpaste={onPaste}
  oncontextmenu={(e) => {
    if (!oncontext) return;
    e.preventDefault();
    e.stopPropagation();
    oncontext(e);
  }}
  onfocus={() => (focused = true)}
  onblur={() => (focused = false)}
>
  {#each log as seg, i (i)}<span class={"t-" + seg.t}>{seg.s}</span>{/each}{#if live}<span
      class="t-in">{pending}</span
    ><span class="caret" class:hollow={!focused}></span>{/if}
  {#if live && !focused && log.length === 0 && pending === ""}
    <span class="hint">click here and type — the program is listening</span>
  {/if}
</div>

<style>
  .term {
    height: 100%;
    min-height: 0;
    overflow: auto;
    background: var(--page);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 10px 12px;
    font: 12.5px/1.6 var(--mono);
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    cursor: text;
    transition: box-shadow 0.15s, border-color 0.15s;
  }
  .term:focus {
    outline: none;
    border-color: var(--accent);
    box-shadow: 0 0 0 1px var(--accent);
  }
  .term.done {
    opacity: 0.92;
  }
  .t-out {
    color: var(--ink);
  }
  .t-in {
    color: var(--accent);
  }
  :global(.t-sys) {
    color: var(--muted);
    font-style: italic;
  }
  .caret {
    display: inline-block;
    width: 8px;
    height: 15px;
    transform: translateY(3px);
    background: var(--accent);
    margin-left: 1px;
    animation: term-blink 1.1s steps(1) infinite;
  }
  .caret.hollow {
    background: transparent;
    box-shadow: inset 0 0 0 1.5px var(--accent);
    animation: none;
  }
  @keyframes term-blink {
    50% {
      opacity: 0;
    }
  }
  .hint {
    color: var(--muted);
    font-style: italic;
  }
</style>
