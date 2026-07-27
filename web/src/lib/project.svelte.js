const KEY = "shrewdness-workspace-v1";
const OLD_KEY = "shrewdness-project-v1";

const DEFAULT_FILES = [
  {
    name: "main.savvy",
    src: `// A self-replicator with a heritable mutation rate.
// Its copy loop lives in a subfolder: include splices it in, once.
include "lib/copy";

copy_self(24);
spawn();
`,
  },
  {
    name: "lib/copy.savvy",
    src: `// Emit every gene of self, nudging one in \`rate\` on average.
fn copy_self(rate) {
    for (var i = 0; i < self.length; i = i + 1) {
        var g = self[i];
        if (rand() % rate == 0) { g = g + (rand() % 3) - 1; }
        emit(g);
    }
}
`,
  },
];
const DEFAULT_FOLDERS = ["lib"];

function newId() {
  return "p" + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
}

function blankProject(name) {
  return {
    id: newId(),
    name,
    files: structuredClone(DEFAULT_FILES),
    folders: [...DEFAULT_FOLDERS],
    entry: "main.savvy",
    active: "main.savvy",
    open: ["main.savvy", "lib/copy.savvy"],
  };
}

function normalizeProject(p, fallbackName = "project") {
  if (!p || !Array.isArray(p.files) || !p.files.length) return null;
  const files = p.files
    .filter((f) => f && typeof f.name === "string")
    .map((f) => ({ name: f.name, src: String(f.src ?? "") }));
  if (!files.length) return null;
  const names = files.map((f) => f.name);
  const entry = names.includes(p.entry) ? p.entry : names[0];
  let open = Array.isArray(p.open)
    ? p.open.filter((n) => names.includes(n))
    : [...names];
  if (!open.length) open = [entry];
  const active = open.includes(p.active) ? p.active : open[0];
  const folders = Array.isArray(p.folders)
    ? p.folders.filter((f) => typeof f === "string")
    : [];
  return {
    id: typeof p.id === "string" ? p.id : newId(),
    name: typeof p.name === "string" && p.name ? p.name : fallbackName,
    files,
    folders,
    entry,
    active,
    open,
  };
}

function loadWorkspace() {
  try {
    const w = JSON.parse(localStorage.getItem(KEY));
    if (w && Array.isArray(w.projects)) {
      const projects = w.projects.map((p) => normalizeProject(p)).filter(Boolean);
      if (projects.length) {
        const current = projects.some((p) => p.id === w.current)
          ? w.current
          : projects[0].id;
        return { projects, current };
      }
    }
  } catch {
  }
  try {
    const old = normalizeProject(JSON.parse(localStorage.getItem(OLD_KEY)), "default");
    if (old) return { projects: [old], current: old.id };
  } catch {
  }
  const p = blankProject("default");
  return { projects: [p], current: p.id };
}

const ws = loadWorkspace();

try {
  const q = new URLSearchParams(location.search).get("project");
  if (q) {
    const p = normalizeProject(JSON.parse(q), "shared");
    if (p) {
      p.id = newId();
      ws.projects.push(p);
      ws.current = p.id;
    }
  }
} catch {
}

const first = ws.projects.find((p) => p.id === ws.current);

export const project = $state({ ...structuredClone(first), diags: {} });
export const workspace = $state({
  list: ws.projects.map((p) => ({ id: p.id, name: p.name })),
  current: ws.current,
  rev: 0,
});

function snapshotCurrent() {
  return {
    id: project.id,
    name: project.name,
    files: project.files.map((f) => ({ name: f.name, src: f.src })),
    folders: [...project.folders],
    entry: project.entry,
    active: project.active,
    open: [...project.open],
  };
}

function persist() {
  const i = ws.projects.findIndex((p) => p.id === project.id);
  if (i >= 0) ws.projects[i] = snapshotCurrent();
  else ws.projects.push(snapshotCurrent());
  ws.current = project.id;
  localStorage.setItem(KEY, JSON.stringify({ current: ws.current, projects: ws.projects }));
}

export function save() {
  persist();
  workspace.list = ws.projects.map((p) => ({ id: p.id, name: p.name }));
  workspace.current = ws.current;
  workspace.rev++;
}

function loadIntoState(p) {
  project.id = p.id;
  project.name = p.name;
  project.files = structuredClone(p.files);
  project.folders = [...(p.folders || [])];
  project.entry = p.entry;
  project.active = p.active;
  project.open = [...p.open];
  project.diags = {};
}

function uniqueProjectName(base) {
  const names = new Set(ws.projects.map((p) => p.name));
  if (!names.has(base)) return base;
  for (let i = 2; ; i++) if (!names.has(`${base} ${i}`)) return `${base} ${i}`;
}

export function switchProject(id) {
  if (id === project.id) return;
  save();
  const p = ws.projects.find((p) => p.id === id);
  if (!p) return;
  loadIntoState(p);
  save();
}
export function createProject() {
  save();
  const p = blankProject(uniqueProjectName("untitled"));
  ws.projects.push(p);
  loadIntoState(p);
  save();
}
export function duplicateProject() {
  save();
  const p = snapshotCurrent();
  p.id = newId();
  p.name = uniqueProjectName(project.name);
  ws.projects.push(p);
  loadIntoState(p);
  save();
}
export function renameProject(name) {
  name = name.trim();
  if (name) project.name = name;
  save();
}
export function deleteProject() {
  const i = ws.projects.findIndex((p) => p.id === project.id);
  if (i >= 0) ws.projects.splice(i, 1);
  if (!ws.projects.length) ws.projects.push(blankProject("default"));
  loadIntoState(ws.projects[Math.min(i, ws.projects.length - 1)]);
  save();
}

export const fileNames = () => project.files.map((f) => f.name);
export const filesMap = () =>
  Object.fromEntries(project.files.map((f) => [f.name, f.src]));

export function projectFileNames(id) {
  if (id === project.id) return project.files.map((f) => f.name);
  const p = ws.projects.find((p) => p.id === id);
  return p ? p.files.map((f) => f.name) : [];
}
export function projectFolders(id) {
  if (id === project.id) return project.folders;
  const p = ws.projects.find((p) => p.id === id);
  return p ? p.folders || [] : [];
}
export function projectEntry(id) {
  if (id === project.id) return project.entry;
  const p = ws.projects.find((p) => p.id === id);
  return p ? p.entry : "";
}

export function uniqueName(base, ext, dir = "") {
  const prefix = dir ? dir.replace(/\/$/, "") + "/" : "";
  const names = new Set(fileNames());
  if (!names.has(prefix + base + ext)) return prefix + base + ext;
  for (let i = 2; ; i++)
    if (!names.has(`${prefix}${base}-${i}${ext}`)) return `${prefix}${base}-${i}${ext}`;
}

export function openFile(name) {
  if (!project.files.some((f) => f.name === name)) return;
  if (!project.open.includes(name)) project.open.push(name);
  project.active = name;
  save();
}
export function closeTab(name) {
  const i = project.open.indexOf(name);
  if (i < 0) return;
  project.open.splice(i, 1);
  if (project.active === name)
    project.active = project.open[Math.min(i, project.open.length - 1)] ?? "";
  save();
}
export function addFile(name, src = "") {
  if (project.files.some((f) => f.name === name)) {
    openFile(name);
    return name;
  }
  project.files.push({ name, src });
  if (project.files.length === 1) project.entry = name;
  openFile(name);
  return name;
}
export function setSrc(name, src) {
  const f = project.files.find((f) => f.name === name);
  if (f && f.src !== src) {
    f.src = src;
    persist();
  }
}
export function setEntry(name) {
  if (project.files.some((f) => f.name === name)) {
    project.entry = name;
    save();
  }
}

export function addFolder(path) {
  path = path.replace(/^\/|\/$/g, "");
  if (path && !project.folders.includes(path)) {
    project.folders.push(path);
    save();
  }
  return path;
}
export function removeFolder(path) {
  const pre = path.replace(/\/$/, "") + "/";
  project.files = project.files.filter((f) => !f.name.startsWith(pre));
  project.folders = project.folders.filter((f) => f !== path && !(f + "/").startsWith(pre));
  project.open = project.open.filter((n) => !n.startsWith(pre));
  if (project.active.startsWith(pre)) project.active = project.open[0] ?? "";
  if (project.entry.startsWith(pre) && project.files.length)
    project.entry = project.files[0].name;
  save();
}
export function renameFolder(oldPath, newPath) {
  newPath = newPath.replace(/^\/|\/$/g, "");
  if (!newPath || newPath === oldPath) return oldPath;
  const oldPre = oldPath + "/";
  const move = (n) => (n === oldPath || n.startsWith(oldPre) ? newPath + n.slice(oldPath.length) : n);
  project.files.forEach((f) => (f.name = move(f.name)));
  project.folders = project.folders.map(move);
  project.open = project.open.map(move);
  project.active = move(project.active);
  project.entry = move(project.entry);
  save();
  return newPath;
}

export function renameFile(oldName, newName) {
  newName = newName.trim().replace(/^\//, "");
  if (!newName || newName === oldName) return oldName;
  if (!/^[\w./-]+$/.test(newName)) return oldName;
  if (!/\.(savvy|asm|shrewd)$/.test(newName)) {
    const ext = oldName.match(/\.(savvy|asm|shrewd)$/);
    newName += ext ? ext[0] : ".savvy";
  }
  if (fileNames().includes(newName)) return oldName;
  const f = project.files.find((f) => f.name === oldName);
  if (!f) return oldName;
  f.name = newName;
  if (project.entry === oldName) project.entry = newName;
  if (project.active === oldName) project.active = newName;
  project.open = project.open.map((n) => (n === oldName ? newName : n));
  save();
  return newName;
}
export function removeFile(name) {
  const i = project.files.findIndex((f) => f.name === name);
  if (i < 0 || project.files.length === 1) return;
  project.files.splice(i, 1);
  const oi = project.open.indexOf(name);
  if (oi >= 0) project.open.splice(oi, 1);
  if (project.active === name)
    project.active = project.open[Math.min(oi < 0 ? 0 : oi, project.open.length - 1)] ?? "";
  if (project.entry === name) project.entry = project.files[0].name;
  save();
}

export function openInProject(id, name) {
  if (id !== project.id) switchProject(id);
  openFile(name);
}

function projObj(pid) {
  return pid === project.id ? project : ws.projects.find((p) => p.id === pid);
}

function cleanupRefs(p, pred) {
  p.open = (p.open || []).filter((n) => !pred(n));
  if (pred(p.active)) p.active = p.open[0] ?? p.files[0]?.name ?? "";
  if (pred(p.entry)) p.entry = p.files[0]?.name ?? "";
}

function uniqueInProj(p, path, isFolder) {
  const names = new Set(p.files.map((f) => f.name));
  const folders = new Set(p.folders || []);
  const taken = (c) =>
    isFolder
      ? folders.has(c) || [...names].some((n) => n === c || n.startsWith(c + "/"))
      : names.has(c);
  if (!taken(path)) return path;
  const slash = path.lastIndexOf("/");
  const dir = slash < 0 ? "" : path.slice(0, slash + 1);
  let bn = slash < 0 ? path : path.slice(slash + 1);
  let ext = "";
  if (!isFolder) {
    const dot = bn.lastIndexOf(".");
    if (dot > 0) {
      ext = bn.slice(dot);
      bn = bn.slice(0, dot);
    }
  }
  for (let i = 2; ; i++) if (!taken(`${dir}${bn}-${i}${ext}`)) return `${dir}${bn}-${i}${ext}`;
}

export function moveItem(srcPid, srcPath, type, dstPid, destDir) {
  const src = projObj(srcPid);
  const dst = projObj(dstPid);
  if (!src || !dst) return;
  destDir = (destDir || "").replace(/^\/+|\/+$/g, "");
  const bn = srcPath.slice(srcPath.lastIndexOf("/") + 1);
  let dest = destDir ? destDir + "/" + bn : bn;

  if (type === "folder") {
    if (srcPid === dstPid && (dest === srcPath || (dest + "/").startsWith(srcPath + "/")))
      return;
    const pre = srcPath + "/";
    const movingFiles = src.files.filter((f) => f.name.startsWith(pre));
    const movingFolders = (src.folders || []).filter(
      (fd) => fd === srcPath || fd.startsWith(pre),
    );
    if (srcPid !== dstPid && movingFiles.length && movingFiles.length === src.files.length)
      return;
    dest = uniqueInProj(dst, dest, true);
    const remap = (n) => dest + n.slice(srcPath.length);
    for (const f of movingFiles) dst.files.push({ name: remap(f.name), src: f.src });
    dst.folders = dst.folders || [];
    dst.folders.push(dest);
    for (const fd of movingFolders) if (fd !== srcPath) dst.folders.push(remap(fd));
    src.files = src.files.filter((f) => !f.name.startsWith(pre));
    src.folders = (src.folders || []).filter(
      (fd) => !(fd === srcPath || fd.startsWith(pre)),
    );
    cleanupRefs(src, (n) => n.startsWith(pre));
  } else {
    if (srcPid === dstPid && dest === srcPath) return;
    const f = src.files.find((x) => x.name === srcPath);
    if (!f) return;
    if (srcPid !== dstPid && src.files.length === 1) return;
    dest = uniqueInProj(dst, dest, false);
    dst.files.push({ name: dest, src: f.src });
    src.files = src.files.filter((x) => x.name !== srcPath);
    cleanupRefs(src, (n) => n === srcPath);
  }
  save();
}

export function exportWorkspace() {
  persist();
  return JSON.stringify({ version: 1, projects: ws.projects }, null, 2);
}

export function importWorkspace(json) {
  let data;
  try {
    data = JSON.parse(json);
  } catch {
    return 0;
  }
  const raw = Array.isArray(data)
    ? data
    : Array.isArray(data?.projects)
      ? data.projects
      : data && data.files
        ? [data]
        : [];
  const incoming = raw.map((p) => normalizeProject(p)).filter(Boolean);
  if (!incoming.length) return 0;
  for (const p of incoming) {
    p.id = newId();
    p.name = uniqueProjectName(p.name);
    ws.projects.push(p);
  }
  loadIntoState(incoming[0]);
  save();
  return incoming.length;
}

export function resetProject() {
  const keep = blankProject(project.name);
  keep.id = project.id;
  loadIntoState(keep);
  save();
}
