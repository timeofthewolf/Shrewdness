<script>
  import { onMount } from "svelte";
  import { createEditor, setDoc, setLang, setReadonly } from "./cm.js";

  let {
    value = "",
    lang = "savvy",
    readonly = false,
    onchange = null,
    minHeight = "10rem",
    maxHeight = "60vh",
    fill = false,
    lint = null,
    symbols = null,
    files = null,
    isa = null,
    oncursor = null,
  } = $props();

  let host;
  let view = $state(null);

  onMount(() => {
    view = createEditor(host, value, lang, {
      readonly,
      onchange,
      lint,
      symbols,
      files,
      isa,
      oncursor,
    });
    return () => view.destroy();
  });

  $effect(() => {
    if (view) setDoc(view, value);
  });
  $effect(() => {
    if (view) setLang(view, lang);
  });
  $effect(() => {
    if (view) setReadonly(view, readonly);
  });

  export function current() {
    return view ? view.state.doc.toString() : value;
  }
  export function focus() {
    view?.focus();
  }
</script>

<div
  class="cm-host"
  class:cm-fill={fill}
  bind:this={host}
  style:--cm-min-height={minHeight}
  style:--cm-max-height={maxHeight}
></div>
