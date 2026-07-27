<script>
  let { name, size = 16, stroke = 1.8, title = "" } = $props();

  const P = {
    chevron: "M9 5l7 7-7 7",
    folder:
      "M3 6.5A1.5 1.5 0 0 1 4.5 5H9l2 2h8.5A1.5 1.5 0 0 1 21 8.5v9A1.5 1.5 0 0 1 19.5 19h-15A1.5 1.5 0 0 1 3 17.5z",
    folderOpen:
      "M4 19.5V7a1.5 1.5 0 0 1 1.5-1.5H9l2 2h6.5A1.5 1.5 0 0 1 19 9v1.5 M3.4 19.5l2.3-8h15.9l-2.3 8z",
    file: "M13 3H7a1.5 1.5 0 0 0-1.5 1.5v15A1.5 1.5 0 0 0 7 21h10a1.5 1.5 0 0 0 1.5-1.5V8.5z M13 3v5.5h5.5",
    build:
      "M12 3l8 4.5v9L12 21l-8-4.5v-9z M12 3v18 M4 7.5l8 4.5 8-4.5",
    terminal:
      "M4 4h16a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1z M6.5 8.5l3.5 3.5-3.5 3.5 M12.5 15.5H17",
    sun: "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z M12 2.5v2 M12 19.5v2 M4.6 4.6l1.4 1.4 M18 18l1.4 1.4 M2.5 12h2 M19.5 12h2 M4.6 19.4l1.4-1.4 M18 6l1.4-1.4",
    moon: "M20 14.5A8 8 0 1 1 9.5 4 6.3 6.3 0 0 0 20 14.5z",
    gear: "M4 7h2.5 M11.5 7h8.5 M4 12h8.5 M17.5 12h2.5 M4 17h2.5 M11.5 17h8.5 M9 4.6a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 0 0 0-4.8z M15 9.6a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 0 0 0-4.8z M9 14.6a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 0 0 0-4.8z",
    plus: "M12 5.5v13 M5.5 12h13",
    trash: "M4.5 7h15 M9 7V4.5h6V7 M6.6 7l.8 12.5h9.2L17.4 7",
    pencil: "M4 20l1-4L15.5 5.5l3 3L8 19z M13.5 7l3 3",
    copy: "M9.5 9.5h9v9h-9z M6 14.5H5.5V5.5h9V6",
    close: "M6.5 6.5l11 11 M17.5 6.5l-11 11",
    download: "M12 4v10.5 M7.5 11L12 15.5 16.5 11 M5 19.5h14",
    upload: "M12 15V4 M7.5 8.5L12 3.5 16.5 8.5 M5 19.5h14",
    search:
      "M11 4.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13z M20 20l-4.2-4.2",
    flask:
      "M9.5 3.5h5 M10.5 3.5v5.2l-4.3 7.8A1.5 1.5 0 0 0 7.5 19h9a1.5 1.5 0 0 0 1.3-2.5l-4.3-7.8V3.5",
    refresh: "M19.5 12a7.5 7.5 0 1 1-2.1-5.2 M19.5 3.8v4h-4",
    menu: "M5 7.5h14 M5 12h14 M5 16.5h10",
    newFile:
      "M13 3H7a1.5 1.5 0 0 0-1.5 1.5V13 M13 3v5.5h5.5 M18.5 8.5V12 M16 16.5h5 M18.5 14v5",
    newFolder:
      "M12 19H4.5A1.5 1.5 0 0 1 3 17.5V6.5A1.5 1.5 0 0 1 4.5 5H9l2 2h8.5A1.5 1.5 0 0 1 21 8.5V12 M16 16.5h5 M18.5 14v5",
    map: "M4 6l5-2 6 2 5-2v14l-5 2-6-2-5 2z M9 4v14 M15 6v14",
    panel:
      "M4 5h16a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z M3 14.5h18",
    sidebar:
      "M4 5h16a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z M9 5v14",
    check: "M5 12.5l4.5 4.5L19.5 7",
    warn: "M12 4l9.2 15.5H2.8z M12 10v4 M12 17.2h.01",
    chevronBar: "M8 9l4 4 4-4",
    x2: "M7 7l10 10 M17 7L7 17",
    dna: "M8 3c0 5 8 5 8 10s-8 5-8 10 M16 3c0 5-8 5-8 10s8 5 8 10 M9 7h6 M9 17h6",
    scissors:
      "M7 4.5a2.25 2.25 0 1 0 0 4.5 2.25 2.25 0 0 0 0-4.5z M7 15a2.25 2.25 0 1 0 0 4.5 2.25 2.25 0 0 0 0-4.5z M9 8l11 8.5 M9 16L20 7.5",
    clipboard:
      "M8.5 5.5H7A1.5 1.5 0 0 0 5.5 7v11.5A1.5 1.5 0 0 0 7 20h10a1.5 1.5 0 0 0 1.5-1.5V7A1.5 1.5 0 0 0 17 5.5h-1.5 M9 3.5h6v3H9z",
  };

  const FILL = {
    play: "M8 5.5v13l11-6.5z",
    pause: "M8.5 5h2.6v14H8.5z M12.9 5h2.6v14h-2.6z",
    step: "M7 5.5v13l9-6.5z M16.6 5h2.4v14h-2.4z",
    square: "M7 7h10v10H7z",
    star:
      "M12 4.2l2.1 4.7 5.1.5-3.85 3.4 1.15 5L12 15.5 7.5 18.2l1.15-5L4.8 9.4l5.1-.5z",
    dot: "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z",
    dots: "M6 10.5a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3z M12 10.5a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3z M18 10.5a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3z",
  };

  const filled = $derived(name in FILL);
  const d = $derived(FILL[name] ?? P[name] ?? "");
</script>

<svg
  class="icon"
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill={filled ? "currentColor" : "none"}
  stroke={filled ? "none" : "currentColor"}
  stroke-width={stroke}
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden={title ? undefined : "true"}
  role={title ? "img" : undefined}
  aria-label={title || undefined}
>
  <path {d} />
</svg>

<style>
  .icon {
    display: inline-block;
    flex: none;
    vertical-align: -0.15em;
  }
</style>
