import {
  EditorView,
  keymap,
  lineNumbers,
  drawSelection,
  highlightActiveLine,
  highlightActiveLineGutter,
  rectangularSelection,
  crosshairCursor,
  hoverTooltip,
} from "@codemirror/view";
import { EditorState, Compartment, Prec } from "@codemirror/state";
import {
  defaultKeymap,
  history,
  historyKeymap,
  indentWithTab,
} from "@codemirror/commands";
import {
  StreamLanguage,
  syntaxHighlighting,
  HighlightStyle,
  bracketMatching,
  foldGutter,
  foldKeymap,
  foldService,
  indentService,
  indentUnit,
} from "@codemirror/language";
import {
  autocompletion,
  completionKeymap,
  closeBrackets,
  closeBracketsKeymap,
  snippetCompletion,
} from "@codemirror/autocomplete";
import { linter, lintGutter, lintKeymap } from "@codemirror/lint";
import {
  search,
  searchKeymap,
  highlightSelectionMatches,
  selectNextOccurrence,
} from "@codemirror/search";
import { vim, Vim, getCM } from "@replit/codemirror-vim";
import { emacs } from "@replit/codemirror-emacs";
import { showMinimap } from "@replit/codemirror-minimap";
import { indentationMarkers } from "@replit/codemirror-indentation-markers";
import { tags as t } from "@lezer/highlight";
import { makeSearchPanel } from "./search.js";

const KEYWORDS = [
  "var",
  "fn",
  "if",
  "else",
  "while",
  "do",
  "for",
  "break",
  "return",
  "include",
];
const ATOMS = ["self", "child", "mem", "r0", "r1", "r2", "r3"];

export const BUILTIN_DOCS = {
  input: "input() — next input value, -1 at end of input",
  rand: "rand() — pseudorandom value in [0, 2^31), from the run's seed",
  print: "print(e) — emit as a decimal number, with a trailing space",
  putchar: "putchar(e) — emit as a character",
  puts: "puts(e) — emit the zero-terminated string at address e",
  emit: "emit(e) — append a gene to the child",
  spawn: "spawn() — commit the child as offspring, start a new one",
  self: "self.length — my genome's length · self[i] — my gene at i",
  child: "child.length — genes emitted so far · child[i] — read one back",
  mem: "mem[i] — the tape · mem.length — how many cells I was given",
  include: 'include "file"; — splice another file in, once (top level only)',
  var: "var x = e; / var a[10]; — a tape cell (or run of cells) in scope",
  fn: "fn name(a, b) { ... } — a function; called by index, recursion works",
};
const BUILTINS = ["input", "rand", "print", "putchar", "puts", "emit", "spawn"];

const kwSet = new Set(KEYWORDS);
const builtinSet = new Set(BUILTINS);
const atomSet = new Set(ATOMS);

const savvyLang = StreamLanguage.define({
  name: "savvy",
  languageData: {
    commentTokens: { line: "//", block: { open: "/*", close: "*/" } },
    closeBrackets: { brackets: ["(", "[", "{", "'", '"'] },
  },
  tokenTable: { builtin: t.function(t.variableName) },
  startState: () => ({ comment: false }),
  token(stream, state) {
    if (state.comment) {
      if (stream.match(/^.*?\*\//)) state.comment = false;
      else stream.skipToEnd();
      return "comment";
    }
    if (stream.eatSpace()) return null;
    if (stream.match("//")) {
      stream.skipToEnd();
      return "comment";
    }
    if (stream.match("/*")) {
      state.comment = !stream.match(/^.*?\*\//);
      if (state.comment) stream.skipToEnd();
      return "comment";
    }
    if (stream.match(/^'(\\.|[^'\\])'/)) return "string";
    if (stream.match(/^"(\\.|[^"\\])*"/)) return "string";
    if (stream.match(/^-?\d+/)) return "number";
    if (stream.match(/^[A-Za-z_][A-Za-z0-9_]*/)) {
      const w = stream.current();
      if (kwSet.has(w)) return "keyword";
      if (builtinSet.has(w)) return "builtin";
      if (atomSet.has(w)) return "atom";
      return "variableName";
    }
    if (stream.match(/^(==|!=|<=|>=|&&|\|\|)/)) return "operator";
    if (stream.match(/^[-+*/%<>=!]/)) return "operator";
    stream.next();
    return null;
  },
});

const asmLang = StreamLanguage.define({
  name: "shrewd-asm",
  languageData: { commentTokens: { line: ";" } },
  token(stream) {
    if (stream.eatSpace()) return null;
    if (stream.match(";")) {
      stream.skipToEnd();
      return "comment";
    }
    if (stream.match(/^[A-Z][A-Z0-9]*/)) return "keyword";
    if (stream.match(/^-?\d+/)) return "number";
    stream.next();
    return null;
  },
});

const genesLang = StreamLanguage.define({
  name: "genes",
  token(stream) {
    if (stream.eatSpace()) return null;
    if (stream.match(/^-?\d+/)) return "number";
    stream.next();
    return null;
  },
});

export const langs = { savvy: savvyLang, asm: asmLang, genes: genesLang };

export function langForName(name) {
  if (name.endsWith(".asm")) return "asm";
  if (name.endsWith(".shrewd")) return "genes";
  return "savvy";
}

const savvyStatic = [
  ...KEYWORDS.filter((k) => !["fn", "if", "for", "while", "do", "include"].includes(k)).map(
    (k) => ({ label: k, type: "keyword", info: BUILTIN_DOCS[k] }),
  ),
  ...BUILTINS.map((b) => ({
    label: b,
    type: "function",
    apply: b === "spawn" || b === "input" || b === "rand" ? b + "()" : undefined,
    info: BUILTIN_DOCS[b],
  })),
  { label: "self.length", type: "property", info: BUILTIN_DOCS.self },
  { label: "child.length", type: "property", info: BUILTIN_DOCS.child },
  { label: "mem.length", type: "property", info: BUILTIN_DOCS.mem },
  ...["r0", "r1", "r2", "r3"].map((r) => ({
    label: r,
    type: "variable",
    info: r + " — a register: 1 gene, 1 step (cheaper than any var)",
  })),
  snippetCompletion("fn ${name}(${args}) {\n    ${}\n}", {
    label: "fn",
    detail: "function",
    type: "keyword",
  }),
  snippetCompletion("if (${cond}) {\n    ${}\n}", {
    label: "if",
    detail: "statement",
    type: "keyword",
  }),
  snippetCompletion("if (${cond}) {\n    ${}\n} else {\n    \n}", {
    label: "ifelse",
    detail: "if / else",
    type: "keyword",
  }),
  snippetCompletion("for (var ${i} = 0; ${i} < ${n}; ${i} = ${i} + 1) {\n    ${}\n}", {
    label: "for",
    detail: "loop",
    type: "keyword",
  }),
  snippetCompletion("while (${cond}) {\n    ${}\n}", {
    label: "while",
    detail: "loop",
    type: "keyword",
  }),
  snippetCompletion("do {\n    ${}\n} while (${cond});", {
    label: "do",
    detail: "do / while",
    type: "keyword",
  }),
  snippetCompletion('include "${file}";', {
    label: "include",
    detail: "splice a file in",
    type: "keyword",
  }),
];

export function scrapeSymbols(files, exceptName) {
  const out = [];
  const seen = new Set();
  for (const f of files) {
    if (!f.name.endsWith(".savvy")) continue;
    const from = f.name === exceptName ? "" : ` — ${f.name}`;
    for (const m of f.src.matchAll(/^\s*fn\s+([A-Za-z_]\w*)\s*\(([^)]*)\)/gm)) {
      if (seen.has(m[1])) continue;
      seen.add(m[1]);
      out.push({
        label: m[1],
        type: "function",
        detail: `(${m[2].trim()})${from}`,
        apply: m[1] + "(",
        boost: 2,
      });
    }
    for (const m of f.src.matchAll(/^\s*var\s+([A-Za-z_]\w*)/gm)) {
      if (seen.has(m[1])) continue;
      seen.add(m[1]);
      out.push({ label: m[1], type: "variable", detail: from.slice(3), boost: 1 });
    }
  }
  return out;
}

function savvyCompletionSource(opts) {
  return (ctx) => {
    const line = ctx.state.doc.lineAt(ctx.pos);
    const before = line.text.slice(0, ctx.pos - line.from);

    const inc = /include\s*"([^"]*)$/.exec(before);
    if (inc) {
      const names = (opts.files ? opts.files() : [])
        .filter((n) => n.endsWith(".savvy"))
        .map((n) => n.replace(/\.savvy$/, ""));
      return {
        from: ctx.pos - inc[1].length,
        options: names.map((n) => ({ label: n, type: "namespace" })),
        validFor: /^[\w./-]*$/,
      };
    }

    if (/\/\//.test(before.split('"').filter((_, i) => i % 2 === 0).join("")))
      return null;
    if ((before.match(/"/g) || []).length % 2 === 1) return null;

    const word = ctx.matchBefore(/[\w.]+/);
    if (!word && !ctx.explicit) return null;
    return {
      from: word ? word.from : ctx.pos,
      options: [...savvyStatic, ...(opts.symbols ? opts.symbols() : [])],
      validFor: /^[\w.]*$/,
    };
  };
}

function asmCompletionSource(opts) {
  return (ctx) => {
    const word = ctx.matchBefore(/[A-Za-z]+/);
    if (!word && !ctx.explicit) return null;
    const isa = opts.isa ? opts.isa() : [];
    return {
      from: word ? word.from : ctx.pos,
      options: isa.map((op) => ({
        label: op.name,
        type: "keyword",
        detail: op.size === 2 ? "op + literal" : "op",
        info: op.doc,
      })),
      validFor: /^[A-Za-z]*$/,
    };
  };
}

function hoverDocs(opts) {
  return hoverTooltip((view, pos) => {
    const line = view.state.doc.lineAt(pos);
    const text = line.text;
    let a = pos - line.from;
    let b = a;
    const wordch = /[A-Za-z0-9_]/;
    while (a > 0 && wordch.test(text[a - 1])) a--;
    while (b < text.length && wordch.test(text[b])) b++;
    if (a === b) return null;
    const word = text.slice(a, b);

    let doc = BUILTIN_DOCS[word];
    if (!doc && opts.isa) {
      const op = opts.isa().find((o) => o.name === word);
      if (op) doc = `${op.name} — ${op.doc}`;
    }
    if (!doc && opts.symbols) {
      const sym = opts.symbols().find((s) => s.label === word);
      if (sym && sym.type === "function")
        doc = `fn ${word}${sym.detail || "()"}`;
    }
    if (!doc) return null;

    return {
      pos: line.from + a,
      end: line.from + b,
      above: true,
      create() {
        const dom = document.createElement("div");
        dom.className = "cm-hoverdoc";
        dom.textContent = doc;
        return { dom };
      },
    };
  });
}

const braceFolding = foldService.of((state, lineStart, lineEnd) => {
  const line = state.doc.lineAt(lineStart);
  const open = line.text.lastIndexOf("{");
  if (open < 0) return null;
  let depth = 0;
  for (let pos = line.from + open; pos < state.doc.length; ) {
    const l = state.doc.lineAt(pos);
    for (let i = pos - l.from; i < l.text.length; i++) {
      const ch = l.text[i];
      if (ch === "{") depth++;
      else if (ch === "}") {
        depth--;
        if (depth === 0) {
          const end = l.from + i;
          return end > lineEnd ? { from: line.from + open + 1, to: end } : null;
        }
      }
    }
    if (l.to >= state.doc.length) break;
    pos = l.to + 1;
  }
  return null;
});

const braceIndent = indentService.of((cx, pos) => {
  const line = cx.state.doc.lineAt(pos);
  let prev = null;
  for (let p = line.from - 1; p >= 0; ) {
    const l = cx.state.doc.lineAt(p);
    if (l.text.trim() !== "") {
      prev = l;
      break;
    }
    p = l.from - 1;
  }
  if (!prev) return 0;
  let ind = /^\s*/.exec(prev.text)[0].length;
  const trimmed = prev.text.replace(/\/\/.*$/, "").trimEnd();
  if (trimmed.endsWith("{")) ind += cx.unit;
  if (line.text.trimStart().startsWith("}")) ind = Math.max(0, ind - cx.unit);
  return ind;
});

const highlight = HighlightStyle.define([
  { tag: t.keyword, color: "var(--code-kw)", fontWeight: "600" },
  { tag: t.comment, color: "var(--muted)", fontStyle: "italic" },
  { tag: t.number, color: "var(--code-num)" },
  { tag: t.string, color: "var(--code-str)" },
  { tag: t.atom, color: "var(--code-atom)" },
  { tag: t.function(t.variableName), color: "var(--code-fn)" },
  { tag: t.operator, color: "var(--ink-2)" },
  { tag: t.variableName, color: "var(--ink)" },
]);

const theme = EditorView.theme({
  "&": { background: "transparent", color: "var(--ink)" },
  ".cm-content": { caretColor: "var(--ink)", padding: "8px 0" },
  ".cm-cursor": { borderLeftColor: "var(--ink)", borderLeftWidth: "2px" },
  "&.cm-focused": { outline: "none" },
  ".cm-selectionBackground, &.cm-focused .cm-selectionBackground": {
    background: "color-mix(in srgb, var(--accent) 24%, transparent)",
  },
  ".cm-activeLine": {
    background: "color-mix(in srgb, var(--ink) 4%, transparent)",
  },
  ".cm-gutters": {
    background: "transparent",
    color: "var(--muted)",
    border: "none",
    borderRight: "1px solid var(--grid)",
  },
  ".cm-lineNumbers .cm-gutterElement": { padding: "0 8px 0 12px" },
  ".cm-activeLineGutter": { background: "transparent", color: "var(--ink)" },
  ".cm-matchingBracket": {
    background: "color-mix(in srgb, var(--accent) 18%, transparent)",
    outline: "1px solid color-mix(in srgb, var(--accent) 45%, transparent)",
  },
  ".cm-foldGutter .cm-gutterElement": {
    cursor: "pointer",
    color: "var(--muted)",
  },
  ".cm-foldPlaceholder": {
    background: "color-mix(in srgb, var(--accent) 14%, transparent)",
    border: "1px solid var(--border)",
    borderRadius: "4px",
    color: "var(--ink-2)",
    margin: "0 2px",
    padding: "0 6px",
  },
  ".cm-panels": {
    background: "var(--surface)",
    color: "var(--ink)",
  },
  ".cm-searchMatch": {
    background: "color-mix(in srgb, var(--s4) 32%, transparent)",
    borderRadius: "2px",
    outline: "1px solid color-mix(in srgb, var(--s4) 55%, transparent)",
  },
  ".cm-searchMatch-selected": {
    background: "color-mix(in srgb, var(--s4) 62%, transparent)",
    outline: "1px solid var(--s4)",
  },
  ".cm-selectionMatch": {
    background: "color-mix(in srgb, var(--accent) 14%, transparent)",
    borderRadius: "2px",
  },
  ".cm-tooltip": {
    background: "var(--surface)",
    color: "var(--ink)",
    border: "1px solid var(--border)",
    borderRadius: "10px",
    boxShadow: "var(--shadow)",
    overflow: "hidden",
  },
  ".cm-tooltip.cm-tooltip-autocomplete > ul": {
    fontFamily: "var(--mono)",
    maxHeight: "16rem",
  },
  ".cm-tooltip.cm-tooltip-autocomplete > ul > li[aria-selected]": {
    background: "color-mix(in srgb, var(--accent) 20%, transparent)",
    color: "var(--ink)",
  },
  ".cm-tooltip.cm-tooltip-autocomplete > ul > li": { padding: "3px 8px" },
  ".cm-completionIcon": { paddingRight: "14px", opacity: "0.7" },
  ".cm-completionLabel": { color: "var(--ink)" },
  ".cm-completionDetail": { color: "var(--muted)", fontStyle: "normal" },
  ".cm-completionInfo": {
    background: "var(--surface)",
    border: "1px solid var(--border)",
    borderRadius: "8px",
    padding: "6px 9px",
    maxWidth: "26rem",
    marginLeft: "6px",
    font: "12px/1.5 var(--sans)",
  },
  ".cm-hoverdoc": {
    padding: "6px 10px",
    font: "12px/1.55 var(--sans)",
    maxWidth: "28rem",
  },
  ".cm-diagnostic": {
    borderLeft: "3px solid var(--crit)",
    padding: "4px 8px",
    background: "var(--surface)",
  },
  ".cm-diagnostic-error": { borderLeftColor: "var(--crit)" },
  ".cm-lintRange-error": {
    backgroundImage: "none",
    textDecoration: "underline wavy var(--crit) 1px",
    textUnderlineOffset: "3px",
  },
  ".cm-lint-marker-error": {
    content: '""',
    width: "0.8em",
    height: "0.8em",
  },
  ".cm-vim-panel": {
    background: "var(--page)",
    color: "var(--ink)",
    fontFamily: "var(--mono)",
    padding: "2px 8px",
  },
  ".cm-vim-panel input": { fontFamily: "var(--mono)", color: "var(--ink)" },
});

let vimCommands = {};
const vimModeListeners = new Set();

export function setVimCommands(cmds) {
  vimCommands = cmds || {};
}
export function onVimMode(cb) {
  vimModeListeners.add(cb);
  return () => vimModeListeners.delete(cb);
}
function notifyVimMode(mode) {
  for (const cb of vimModeListeners) cb(mode);
}

let vimExDefined = false;
function defineVimEx() {
  if (vimExDefined) return;
  vimExDefined = true;
  const run = (name) => () => vimCommands[name] && vimCommands[name]();
  Vim.defineEx("write", "w", run("write"));
  Vim.defineEx("run", "run", run("run"));
  Vim.defineEx("build", "build", run("build"));
  Vim.defineEx("quit", "q", run("quit"));
  Vim.defineEx("wq", "wq", () => {
    run("write")();
    run("quit")();
  });
  Vim.defineEx("xit", "x", () => {
    run("write")();
    run("quit")();
  });
}

let jkMapped = false;
function applyVimJk(on) {
  try {
    if (on && !jkMapped) {
      Vim.map("jk", "<Esc>", "insert");
      Vim.map("jj", "<Esc>", "insert");
      jkMapped = true;
    } else if (!on && jkMapped) {
      Vim.unmap("jk", "insert");
      Vim.unmap("jj", "insert");
      jkMapped = false;
    }
  } catch {
  }
}

function hookVimMode(view) {
  if (view._vimHooked) return;
  try {
    const cm = getCM(view);
    if (!cm) return;
    view._vimHooked = true;
    cm.on("vim-mode-change", (e) => {
      const m = (e.mode || "normal") + (e.subMode ? " " + e.subMode : "");
      notifyVimMode(m);
    });
    const insert = cm.state?.vim?.insertMode;
    notifyVimMode(insert ? "insert" : "normal");
  } catch {
  }
}

let currentSettings = {
  fontSize: 13,
  tabSize: 4,
  lineWrapping: false,
  keymap: "standard",
  lintOnType: true,
  activeLine: true,
  closeBrackets: true,
  completion: true,
  vimJk: false,
  minimap: false,
  indentGuides: true,
};

function modeExt(mode) {
  if (mode === "vim") return vim();
  if (mode === "emacs") return emacs();
  return [];
}

const minimapExt = showMinimap.compute([], () => ({
  create: () => ({ dom: document.createElement("div") }),
  displayText: "characters",
  showOverlay: "always",
}));

const indentGuidesExt = indentationMarkers({
  thickness: 1,
  colors: {
    light: "rgba(120,120,120,0.22)",
    dark: "rgba(150,150,150,0.20)",
    activeLight: "rgba(120,120,120,0.5)",
    activeDark: "rgba(160,160,160,0.5)",
  },
});

function settingsExts(s, opts) {
  const ext = [
    modeExt(s.keymap),
    EditorView.theme({
      "&": { fontSize: (s.fontSize || 13) + "px" },
    }),
    indentUnit.of(" ".repeat(Math.max(1, s.tabSize || 4))),
  ];
  if (s.lineWrapping) ext.push(EditorView.lineWrapping);
  if (s.activeLine)
    ext.push(highlightActiveLine(), highlightActiveLineGutter());
  if (s.closeBrackets) ext.push(closeBrackets());
  if (s.completion) ext.push(autocompletion());
  if (s.lintOnType && opts.lint)
    ext.push(
      lintGutter(),
      linter(
        async (view) => {
          const err = await opts.lint(view.state.doc.toString());
          return errorToDiagnostics(view.state, err);
        },
        { delay: 400 },
      ),
    );
  if (s.indentGuides) ext.push(indentGuidesExt);
  if (s.minimap && !s.lineWrapping) ext.push(minimapExt);
  return ext;
}

const live = new Set();

export function applyEditorSettings(s) {
  currentSettings = { ...currentSettings, ...s };
  defineVimEx();
  applyVimJk(currentSettings.keymap === "vim" && currentSettings.vimJk);
  for (const view of live) {
    view.dispatch({
      effects: view._settingsBox.reconfigure(
        settingsExts(currentSettings, view._opts),
      ),
    });
    if (currentSettings.keymap === "vim") hookVimMode(view);
  }
}

export function currentKeymapMode() {
  return currentSettings.keymap;
}

function langExts(lang, opts) {
  const l = langs[lang] ?? savvyLang;
  const exts = [l];
  if (lang === "savvy")
    exts.push(
      l.data.of({ autocomplete: savvyCompletionSource(opts) }),
      braceFolding,
      braceIndent,
    );
  if (lang === "asm")
    exts.push(l.data.of({ autocomplete: asmCompletionSource(opts) }));
  return exts;
}

export function createEditor(parent, doc, lang, opts = {}) {
  const { readonly = false } = opts;
  const langBox = new Compartment();
  const roBox = new Compartment();
  const settingsBox = new Compartment();

  const exts = [
    settingsBox.of(settingsExts(currentSettings, opts)),
    lineNumbers(),
    foldGutter(),
    history(),
    drawSelection(),
    EditorState.allowMultipleSelections.of(true),
    rectangularSelection(),
    crosshairCursor(),
    highlightSelectionMatches(),
    bracketMatching(),
    indentUnit.of("    "),
    syntaxHighlighting(highlight),
    theme,
    hoverDocs(opts),
    search({ top: true, createPanel: makeSearchPanel }),
    langBox.of(langExts(lang, opts)),
    roBox.of(EditorState.readOnly.of(readonly)),
    Prec.high(
      keymap.of([
        { key: "Mod-d", run: selectNextOccurrence, preventDefault: true },
      ]),
    ),
    keymap.of([
      ...closeBracketsKeymap,
      ...defaultKeymap,
      ...searchKeymap,
      ...historyKeymap,
      ...foldKeymap,
      ...completionKeymap,
      ...lintKeymap,
      indentWithTab,
    ]),
    EditorView.updateListener.of((u) => {
      if (u.docChanged && opts.onchange) opts.onchange(u.state.doc.toString());
      if (opts.oncursor && (u.selectionSet || u.docChanged)) {
        const head = u.state.selection.main.head;
        const line = u.state.doc.lineAt(head);
        opts.oncursor({
          line: line.number,
          col: head - line.from + 1,
          sels: u.state.selection.ranges.length,
        });
      }
    }),
  ];

  const view = new EditorView({
    parent,
    state: EditorState.create({ doc, extensions: exts }),
  });
  if (opts.oncursor) opts.oncursor({ line: 1, col: 1, sels: 1 });
  view._langBox = langBox;
  view._roBox = roBox;
  view._settingsBox = settingsBox;
  view._opts = opts;
  if (currentSettings.keymap === "vim") hookVimMode(view);
  live.add(view);
  const destroy = view.destroy.bind(view);
  view.destroy = () => {
    live.delete(view);
    destroy();
  };
  return view;
}

export function setDoc(view, text) {
  if (view.state.doc.toString() === text) return;
  view.dispatch({
    changes: { from: 0, to: view.state.doc.length, insert: text },
  });
}

export function setLang(view, lang) {
  view.dispatch({
    effects: view._langBox.reconfigure(langExts(lang, view._opts)),
  });
}

export function setReadonly(view, ro) {
  view.dispatch({
    effects: view._roBox.reconfigure(EditorState.readOnly.of(ro)),
  });
}

export function errorToDiagnostics(state, err) {
  if (!err) return [];
  const lineNo = Math.max(1, Math.min(state.doc.lines, err.line || 1));
  const line = state.doc.line(lineNo);
  const from = Math.min(line.from + Math.max(0, (err.col || 1) - 1), line.to);
  return [
    {
      from,
      to: line.to > from ? line.to : from,
      severity: "error",
      message: err.error || err.text || "error",
    },
  ];
}
