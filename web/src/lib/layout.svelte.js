import { project, closeTab } from "./project.svelte.js";
import { isNarrow } from "./media.svelte.js";

const KEY = "shrewdness-ide-dock";
let nid = 1;
const leaf = (keys = [], active = null) => ({
  id: nid++, type: "leaf", keys: [...keys], active: active ?? keys[0] ?? null,
});
const split = (dir, a, b, ratio = 0.5) => ({ id: nid++, type: "split", dir, ratio, a, b });

export const isFile = (k) => typeof k === "string" && k.startsWith("file:");
export const fileName = (k) => (isFile(k) ? k.slice(5) : null);
export const fileKey = (n) => "file:" + n;
export const TOOLS = ["terminal", "compiler", "inspector", "debug"];

function reid(node) {
  node.id = nid++;
  if (node.type === "split") { reid(node.a); reid(node.b); }
  return node;
}
function restore() {
  try {
    const s = JSON.parse(localStorage.getItem(KEY));
    if (s && s.root) return reid(s.root);
  } catch {}
  return null;
}
function fresh() {
  const files = project.open.map(fileKey);
  return leaf(files, project.active ? fileKey(project.active) : files[0] ?? null);
}

const initial = restore() || fresh();
export const L = $state({
  root: initial,
  focused: firstLeaf(initial).id,
  activeFile: project.active || null,
  drag: null,
});

export function save() {
  try {
    localStorage.setItem(KEY, JSON.stringify({ root: strip(L.root) }));
  } catch {}
}
function strip(n) {
  return n.type === "leaf"
    ? { type: "leaf", keys: n.keys, active: n.active }
    : { type: "split", dir: n.dir, ratio: n.ratio, a: strip(n.a), b: strip(n.b) };
}

function leavesOf(node, out = []) {
  if (node.type === "leaf") out.push(node);
  else { leavesOf(node.a, out); leavesOf(node.b, out); }
  return out;
}
export const allLeaves = () => leavesOf(L.root);
export function firstLeaf(node) { return node.type === "leaf" ? node : firstLeaf(node.a); }
function leafById(id, n = L.root) {
  if (n.type === "leaf") return n.id === id ? n : null;
  return leafById(id, n.a) || leafById(id, n.b);
}
function leafOf(key) { return allLeaves().find((l) => l.keys.includes(key)) || null; }
function parentOf(id, n = L.root, parent = null, side = null) {
  if (n.id === id) return { parent, side };
  if (n.type === "split") return parentOf(id, n.a, n, "a") || parentOf(id, n.b, n, "b");
  return null;
}
function replaceNode(id, node) {
  if (L.root.id === id) { L.root = node; return; }
  const p = parentOf(id);
  if (p && p.parent) p.parent[p.side] = node;
}
function focusedLeaf() { return leafById(L.focused) || firstLeaf(L.root); }

export function activate(leafId, key) {
  const lf = leafById(leafId);
  if (!lf) return;
  lf.active = key;
  L.focused = leafId;
  if (isFile(key)) { L.activeFile = fileName(key); project.active = fileName(key); }
  save();
}

function removeFromLeaf(lf, key) {
  const i = lf.keys.indexOf(key);
  if (i < 0) return;
  lf.keys.splice(i, 1);
  if (lf.active === key) lf.active = lf.keys[Math.min(i, lf.keys.length - 1)] ?? null;
}
function collapseIfEmpty(lf) {
  if (lf.keys.length > 0) return;
  const p = parentOf(lf.id);
  if (!p || !p.parent) return;
  const sib = p.side === "a" ? p.parent.b : p.parent.a;
  const pid = p.parent.id;
  replaceNode(pid, sib);
  if (!leafById(L.focused)) L.focused = firstLeaf(sib).id;
}

export function closeKey(key) {
  const lf = leafOf(key);
  if (!lf) return;
  removeFromLeaf(lf, key);
  if (isFile(key)) closeTab(fileName(key));
  else if (lf.active && isFile(lf.active) && L.focused === lf.id)
    project.active = fileName(lf.active);
  collapseIfEmpty(lf);
  save();
}

export function openKey(key, focus = true) {
  let lf = leafOf(key);
  if (!lf) { lf = focusedLeaf(); lf.keys.push(key); }
  if (focus) activate(lf.id, key);
  else save();
  return lf;
}

export function moveKey(key, targetLeafId, pos) {
  const src = leafOf(key);
  const target = leafById(targetLeafId);
  if (!target) return;
  if (src === target && src.keys.length === 1 && typeof pos !== "number") return;

  if (src) removeFromLeaf(src, key);

  if (typeof pos === "number" || pos === "center") {
    const idx = typeof pos === "number" ? Math.min(pos, target.keys.length) : target.keys.length;
    target.keys.splice(idx, 0, key);
    target.active = key;
    L.focused = target.id;
  } else {
    const nl = leaf([key], key);
    const dir = pos === "left" || pos === "right" ? "row" : "col";
    const before = pos === "left" || pos === "top";
    replaceNode(target.id, split(dir, before ? nl : target, before ? target : nl, before ? 0.5 : 0.5));
    L.focused = nl.id;
  }
  if (src && src !== target) collapseIfEmpty(src);
  if (isFile(key)) { L.activeFile = fileName(key); project.active = fileName(key); }
  save();
}

export function setRatio(splitId, ratio) {
  const walk = (node) => {
    if (node.id === splitId && node.type === "split") { node.ratio = ratio; return true; }
    if (node.type === "split") return walk(node.a) || walk(node.b);
    return false;
  };
  walk(L.root);
  save();
}

export function toggleTool(kind) {
  const existing = leafOf(kind);
  if (existing) {
    if (existing.active === kind && allLeaves().length > 1) closeKey(kind);
    else activate(existing.id, kind);
    return;
  }
  const f = focusedLeaf();
  const pos = isNarrow() ? "center" : kind === "terminal" ? "bottom" : "right";
  moveKey(kind, f.id, pos);
}

export function mergeAll() {
  const leaves = allLeaves();
  if (leaves.length < 2) return;
  const keys = [];
  for (const lf of leaves) for (const k of lf.keys) if (!keys.includes(k)) keys.push(k);
  const wanted = leafById(L.focused)?.active;
  L.root = leaf(keys, keys.includes(wanted) ? wanted : keys[0] ?? null);
  L.focused = L.root.id;
  if (isFile(L.root.active)) { L.activeFile = fileName(L.root.active); project.active = L.activeFile; }
  save();
}

export function splitActive(dir = "right") {
  const f = focusedLeaf();
  const key = f.active;
  if (!key) return;
  moveKey(key, f.id, dir);
}

export function reconcileFiles() {
  const open = new Set(project.open.map(fileKey));
  for (const lf of allLeaves())
    for (const k of [...lf.keys])
      if (isFile(k) && !open.has(k)) { removeFromLeaf(lf, k); }
  for (const lf of allLeaves()) collapseIfEmpty(lf);
  for (const n of project.open) if (!leafOf(fileKey(n))) openKey(fileKey(n), false);
  save();
}
