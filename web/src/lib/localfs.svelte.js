import { project, save as saveProject, setPersistHook } from "./project.svelte.js";
import { isDesktop } from "./desktop.js";

const api = isDesktop ? window.shrewdness.files : null;

export const localFolder = $state({ id: null, root: "", name: "" });

const wanted = (() => {
  try {
    const d = new URLSearchParams(location.search).get("detach");
    return d && d.startsWith("file:") ? d.slice(5) : null;
  } catch {
    return null;
  }
})();

let firstAdopt = true;
let onDisk = new Map();
let writing = 0;
let flushTimer = null;

export const isLocal = () => !!localFolder.id;

function preferred(files) {
  return (
    files.find((f) => f.name === "main.savvy") ??
    files.find((f) => f.name.endsWith(".savvy") && !f.name.includes("/")) ??
    files.find((f) => f.name.endsWith(".savvy")) ??
    files[0]
  )?.name;
}

function adopt(tree) {
  onDisk = new Map(tree.files.map((f) => [f.name, f.src]));
  project.files = tree.files.map((f) => ({ ...f }));
  project.folders = [...tree.folders];
  const names = new Set(project.files.map((f) => f.name));
  const first = (firstAdopt && wanted && names.has(wanted) ? wanted : null)
    ?? preferred(project.files);
  if (firstAdopt && wanted && names.has(wanted)) project.open = [];
  firstAdopt = false;
  project.open = project.open.filter((n) => names.has(n));
  if (!project.open.length && first) project.open = [first];
  if (!names.has(project.entry)) project.entry = first ?? "";
  if (!names.has(project.active)) project.active = project.open[0] ?? "";
}

export async function openLocalFolder() {
  if (!api) return null;
  const tree = await api.open();
  if (!tree) return null;
  localFolder.id = tree.id;
  localFolder.root = tree.root;
  localFolder.name = tree.name;
  project.name = tree.name;
  project.open = [];
  adopt(tree);
  saveProject();
  return tree;
}

function forget() {
  localFolder.id = null;
  localFolder.root = "";
  localFolder.name = "";
  onDisk = new Map();
}

export async function closeLocalFolder() {
  if (!api || !localFolder.id) return;
  await flush();
  await api.close(localFolder.id);
  forget();
}

export async function flush() {
  clearTimeout(flushTimer);
  if (!api || !localFolder.id) return;
  const id = localFolder.id;
  const live = new Map(project.files.map((f) => [f.name, f.src]));
  writing++;
  try {
    for (const dir of project.folders)
      if (!onDisk.has(dir)) await api.mkdir(id, dir).catch(() => {});
    for (const [name, src] of live)
      if (onDisk.get(name) !== src) {
        await api.write(id, name, src);
        onDisk.set(name, src);
      }
    for (const name of [...onDisk.keys()])
      if (!live.has(name)) {
        await api.remove(id, name);
        onDisk.delete(name);
      }
  } finally {
    setTimeout(() => writing--, 600);
  }
}

export function scheduleFlush() {
  if (!api || !localFolder.id) return;
  clearTimeout(flushTimer);
  flushTimer = setTimeout(flush, 350);
}

if (api) {
  setPersistHook(() => {
    if (!localFolder.id) return false;
    scheduleFlush();
    return true;
  });
  api.onClosed((id) => {
    if (id === localFolder.id) forget();
  });
  api.onChanged(async (id) => {
    if (id !== localFolder.id || writing > 0) return;
    const tree = await api.reload(id).catch(() => null);
    if (tree) adopt(tree);
  });
  api.initial().then((tree) => {
    if (!tree) return;
    localFolder.id = tree.id;
    localFolder.root = tree.root;
    localFolder.name = tree.name;
    project.name = tree.name;
    project.open = [];
    adopt(tree);
    saveProject();
  }).catch(() => {});
  addEventListener("beforeunload", () => {
    if (localFolder.id) flush();
  });
}
