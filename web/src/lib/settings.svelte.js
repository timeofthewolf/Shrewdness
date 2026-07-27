import { applyEditorSettings } from "./cm.js";

const KEY = "shrewdness-settings-v1";

export const DEFAULTS = {
  fontSize: 13,
  tabSize: 4,
  lineWrapping: false,
  keymap: "standard",
  lintOnType: true,
  activeLine: true,
  closeBrackets: true,
  completion: true,
  minimap: false,
  indentGuides: true,
  vimJk: false,
  accent: "",
};

export const ACCENTS = [
  { slot: "", label: "shrewd green", css: "var(--s5)" },
  { slot: "s1", label: "blue", css: "var(--s1)" },
  { slot: "s7", label: "violet", css: "var(--s7)" },
  { slot: "s3", label: "magenta", css: "var(--s3)" },
  { slot: "s6", label: "orange", css: "var(--s6)" },
  { slot: "s8", label: "red", css: "var(--s8)" },
  { slot: "s2", label: "forest", css: "var(--s2)" },
];

function applyAccent(slot) {
  const root = document.documentElement.style;
  if (slot) root.setProperty("--accent", `var(--${slot})`);
  else root.removeProperty("--accent");
}

function load() {
  try {
    const saved = JSON.parse(localStorage.getItem(KEY));
    if (saved && typeof saved === "object") return { ...DEFAULTS, ...saved };
  } catch {
  }
  return { ...DEFAULTS, lineWrapping: matchMedia("(max-width: 820px)").matches };
}

export const settings = $state(load());

export function saveSettings() {
  localStorage.setItem(KEY, JSON.stringify({ ...settings }));
  applyEditorSettings({ ...settings });
  applyAccent(settings.accent);
}

export function setSetting(key, value) {
  settings[key] = value;
  saveSettings();
}

export function resetSettings() {
  Object.assign(settings, DEFAULTS);
  saveSettings();
}

applyEditorSettings({ ...settings });
applyAccent(settings.accent);
