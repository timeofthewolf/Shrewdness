function watch(query) {
  const m = matchMedia(query);
  const box = $state({ on: m.matches });
  m.addEventListener("change", (e) => (box.on = e.matches));
  return box;
}

const narrow = watch("(max-width: 820px)");
const phone = watch("(max-width: 560px)");
const touch = watch("(hover: none)");

export const ui = {
  get narrow() { return narrow.on; },
  get phone() { return phone.on; },
  get touch() { return touch.on; },
};

export const isNarrow = () => narrow.on;
