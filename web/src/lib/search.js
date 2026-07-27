import { EditorView } from "@codemirror/view";
import {
  SearchQuery,
  setSearchQuery,
  getSearchQuery,
  findNext,
  findPrevious,
  selectMatches,
  replaceNext,
  replaceAll,
  closeSearchPanel,
} from "@codemirror/search";

function el(tag, props = {}, ...kids) {
  const n = document.createElement(tag);
  Object.assign(n, props);
  for (const k of kids) n.append(k);
  return n;
}

function icon(label, title, cls = "") {
  const b = el("button", { className: "cm-sp-btn " + cls, title, type: "button" });
  b.textContent = label;
  return b;
}

function countMatches(query, view) {
  if (!query.search) return { total: 0, index: 0 };
  let re;
  try {
    let pat = query.regexp
      ? query.search
      : query.search.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    if (query.wholeWord) pat = `\\b(?:${pat})\\b`;
    re = new RegExp(pat, "g" + (query.caseSensitive ? "" : "i"));
  } catch {
    return { total: -1, index: 0 };
  }
  const text = view.state.doc.toString();
  const head = view.state.selection.main.from;
  let total = 0;
  let index = 0;
  let m;
  re.lastIndex = 0;
  while ((m = re.exec(text)) && total < 20000) {
    total++;
    if (index === 0 && m.index >= head) index = total;
    if (m.index === re.lastIndex) re.lastIndex++;
  }
  if (index === 0 && total > 0) index = 1;
  return { total, index };
}

export function makeSearchPanel(view) {
  const seeded = getSearchQuery(view.state);
  let initial = seeded.search;
  if (!initial) {
    const sel = view.state.selection.main;
    if (!sel.empty) {
      const s = view.state.sliceDoc(sel.from, sel.to);
      if (!s.includes("\n")) initial = s;
    }
  }

  const findInput = el("input", {
    className: "cm-sp-input",
    placeholder: "Find",
    value: initial,
    spellcheck: false,
  });
  const replaceInput = el("input", {
    className: "cm-sp-input",
    placeholder: "Replace",
    value: seeded.replace || "",
    spellcheck: false,
  });
  const count = el("span", { className: "cm-sp-count" });

  const caseBtn = icon("Aa", "Match case (Alt-C)", "cm-sp-toggle");
  const wordBtn = icon("⌗", "Whole word (Alt-W)", "cm-sp-toggle");
  const reBtn = icon(".*", "Use regular expression (Alt-R)", "cm-sp-toggle");
  if (seeded.caseSensitive) caseBtn.classList.add("on");
  if (seeded.wholeWord) wordBtn.classList.add("on");
  if (seeded.regexp) reBtn.classList.add("on");

  const prevBtn = icon("↑", "Previous match (Shift-Enter)");
  const nextBtn = icon("↓", "Next match (Enter)");
  const allBtn = icon("≡", "Select all matches (Alt-Enter)");
  const closeBtn = icon("✕", "Close (Esc)", "cm-sp-close");

  const replaceBtn = icon("Replace", "Replace next");
  replaceBtn.classList.add("cm-sp-wide");
  const replaceAllBtn = icon("All", "Replace all");
  replaceAllBtn.classList.add("cm-sp-wide");

  function query() {
    return new SearchQuery({
      search: findInput.value,
      replace: replaceInput.value,
      caseSensitive: caseBtn.classList.contains("on"),
      wholeWord: wordBtn.classList.contains("on"),
      regexp: reBtn.classList.contains("on"),
    });
  }

  function refreshCount() {
    const { total, index } = countMatches(query(), view);
    reBtn.classList.toggle("bad", total === -1);
    if (total === -1) count.textContent = "bad regex";
    else if (!findInput.value) count.textContent = "";
    else if (total === 0) count.textContent = "No results";
    else count.textContent = `${index} of ${total}`;
  }

  function commit(run) {
    const q = query();
    view.dispatch({ effects: setSearchQuery.of(q) });
    if (run) run(view);
    refreshCount();
  }

  findInput.addEventListener("input", () => commit());
  replaceInput.addEventListener("input", () => commit());
  for (const b of [caseBtn, wordBtn, reBtn])
    b.addEventListener("click", () => {
      b.classList.toggle("on");
      commit();
      findInput.focus();
    });

  prevBtn.addEventListener("click", () => commit(findPrevious));
  nextBtn.addEventListener("click", () => commit(findNext));
  allBtn.addEventListener("click", () => commit(selectMatches));
  replaceBtn.addEventListener("click", () => commit(replaceNext));
  replaceAllBtn.addEventListener("click", () => commit(replaceAll));
  closeBtn.addEventListener("click", () => {
    closeSearchPanel(view);
    view.focus();
  });

  function onKey(e) {
    if (e.key === "Enter") {
      e.preventDefault();
      if (e.target === replaceInput) commit(replaceNext);
      else if (e.altKey) commit(selectMatches);
      else commit(e.shiftKey ? findPrevious : findNext);
    } else if (e.key === "Escape") {
      e.preventDefault();
      closeSearchPanel(view);
      view.focus();
    } else if (e.altKey && (e.key === "c" || e.key === "C")) {
      e.preventDefault();
      caseBtn.classList.toggle("on");
      commit();
    } else if (e.altKey && (e.key === "w" || e.key === "W")) {
      e.preventDefault();
      wordBtn.classList.toggle("on");
      commit();
    } else if (e.altKey && (e.key === "r" || e.key === "R")) {
      e.preventDefault();
      reBtn.classList.toggle("on");
      commit();
    }
  }

  const findRow = el(
    "div",
    { className: "cm-sp-row" },
    findInput,
    count,
    el("div", { className: "cm-sp-tgls" }, caseBtn, wordBtn, reBtn),
    el("div", { className: "cm-sp-nav" }, prevBtn, nextBtn, allBtn),
    closeBtn,
  );
  const replaceRow = el(
    "div",
    { className: "cm-sp-row" },
    replaceInput,
    el("div", { className: "cm-sp-nav" }, replaceBtn, replaceAllBtn),
  );

  const dom = el("div", { className: "cm-search-panel" }, findRow, replaceRow);
  dom.addEventListener("keydown", onKey);

  return {
    dom,
    top: true,
    mount() {
      if (initial) commit();
      refreshCount();
      findInput.focus();
      findInput.select();
    },
    update(u) {
      if (u.docChanged || u.selectionSet) refreshCount();
    },
  };
}
