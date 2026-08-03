const { app, dialog, ipcMain, BrowserWindow } = require("electron");
const fs = require("node:fs/promises");
const fsSync = require("node:fs");
const path = require("node:path");

const CODE = new Set([".savvy", ".asm", ".shrewd"]);
const SKIP = new Set([
  "node_modules", ".git", ".svn", ".hg", "build", "dist", "target",
  ".cache", ".venv", "__pycache__",
]);
const MAX_FILE = 1 << 20;
const MAX_FILES = 4000;

const roots = new Map();
const watchers = new Map();

function inside(root, rel) {
  if (typeof rel !== "string" || rel.length === 0) return null;
  const full = path.resolve(root, rel);
  const base = path.resolve(root);
  if (full !== base && !full.startsWith(base + path.sep)) return null;
  return full;
}

function claim(id) {
  const root = roots.get(id);
  if (!root) throw new Error("no such folder");
  return root;
}

async function readTree(root) {
  const files = [];
  const folders = [];
  let count = 0;

  async function walk(dir, rel) {
    let entries;
    try {
      entries = await fs.readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (e.name.startsWith(".") || SKIP.has(e.name)) continue;
      const child = path.join(dir, e.name);
      const childRel = rel ? `${rel}/${e.name}` : e.name;
      if (e.isDirectory()) {
        folders.push(childRel);
        await walk(child, childRel);
      } else if (e.isFile() && CODE.has(path.extname(e.name))) {
        if (++count > MAX_FILES) continue;
        let stat;
        try {
          stat = await fs.stat(child);
        } catch {
          continue;
        }
        if (stat.size > MAX_FILE) continue;
        files.push({ name: childRel, src: await fs.readFile(child, "utf8") });
      }
    }
  }

  await walk(root, "");
  files.sort((a, b) => a.name.localeCompare(b.name));
  folders.sort();
  return { files, folders };
}

function watch(id, root) {
  if (watchers.has(id)) return;
  let timer = null;
  try {
    const w = fsSync.watch(root, { recursive: true }, () => {
      clearTimeout(timer);
      timer = setTimeout(() => {
        for (const win of BrowserWindow.getAllWindows())
          if (!win.isDestroyed())
            win.webContents.send("shrewdness:files-changed", id);
      }, 400);
    });
    watchers.set(id, w);
  } catch {}
}

function stopWatch(id) {
  const w = watchers.get(id);
  if (w) {
    try {
      w.close();
    } catch {}
    watchers.delete(id);
  }
}

let cliFolder = null;
let activeRoot = null;

function setCliFolder(dir) {
  try {
    if (dir && fsSync.statSync(dir).isDirectory()) cliFolder = path.resolve(dir);
  } catch {}
}

async function openRoot(root) {
  roots.set(root, root);
  activeRoot = root;
  app.addRecentDocument(root);
  const tree = await readTree(root);
  watch(root, root);
  return { id: root, root, name: path.basename(root), ...tree };
}

function register() {
  ipcMain.handle("files:initial", async () => {
    const dir = cliFolder || activeRoot;
    cliFolder = null;
    if (!dir || !fsSync.existsSync(dir)) return null;
    return openRoot(dir);
  });

  ipcMain.handle("files:open", async (e) => {
    const win = BrowserWindow.fromWebContents(e.sender);
    const r = await dialog.showOpenDialog(win, {
      title: "Open a folder of Savvy files",
      properties: ["openDirectory", "createDirectory"],
    });
    if (r.canceled || !r.filePaths.length) return null;
    return openRoot(r.filePaths[0]);
  });

  ipcMain.handle("files:reload", async (_e, id) => readTree(claim(id)));

  ipcMain.handle("files:write", async (_e, id, rel, src) => {
    const full = inside(claim(id), rel);
    if (!full) throw new Error("path escapes the folder");
    await fs.mkdir(path.dirname(full), { recursive: true });
    await fs.writeFile(full, String(src), "utf8");
    return true;
  });

  ipcMain.handle("files:mkdir", async (_e, id, rel) => {
    const full = inside(claim(id), rel);
    if (!full) throw new Error("path escapes the folder");
    await fs.mkdir(full, { recursive: true });
    return true;
  });

  ipcMain.handle("files:remove", async (_e, id, rel) => {
    const full = inside(claim(id), rel);
    if (!full) throw new Error("path escapes the folder");
    await fs.rm(full, { recursive: true, force: true });
    return true;
  });

  ipcMain.handle("files:rename", async (_e, id, from, to) => {
    const root = claim(id);
    const a = inside(root, from);
    const b = inside(root, to);
    if (!a || !b) throw new Error("path escapes the folder");
    await fs.mkdir(path.dirname(b), { recursive: true });
    await fs.rename(a, b);
    return true;
  });

  ipcMain.handle("files:close", async (_e, id) => {
    stopWatch(id);
    roots.delete(id);
    if (activeRoot === id) activeRoot = null;
    for (const win of BrowserWindow.getAllWindows())
      if (!win.isDestroyed()) win.webContents.send("shrewdness:folder-closed", id);
    return true;
  });
}

function disposeAll() {
  for (const id of [...watchers.keys()]) stopWatch(id);
}

module.exports = { register, disposeAll, setCliFolder };
