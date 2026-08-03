const { app, BrowserWindow, dialog, ipcMain, shell } = require("electron");
const { spawn } = require("node:child_process");
const { createServer } = require("node:net");
const path = require("node:path");
const fs = require("node:fs");
const buildMenu = require("./menu");

const packaged = app.isPackaged;
const root = packaged ? process.resourcesPath : path.join(__dirname, "..");

const exe = () => {
  const name = process.platform === "win32" ? "shrewdness.exe" : "shrewdness";
  const candidates = packaged
    ? [path.join(root, "bin", name)]
    : [
        path.join(root, "build", name),
        path.join(root, "build-release", name),
        path.join(root, "build", "Release", name),
      ];
  return candidates.find((p) => fs.existsSync(p)) || null;
};

const webRoot = () =>
  packaged ? path.join(root, "web") : path.join(root, "web", "dist");
const examplesRoot = () => path.join(root, "examples");

function freePort() {
  return new Promise((resolve, reject) => {
    const s = createServer();
    s.once("error", reject);
    s.listen(0, "127.0.0.1", () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
  });
}

async function waitForBackend(url, deadlineMs = 15000) {
  const started = Date.now();
  while (Date.now() - started < deadlineMs) {
    try {
      const r = await fetch(url + "/api/isa");
      if (r.ok) return true;
    } catch {}
    await new Promise((r) => setTimeout(r, 120));
  }
  return false;
}

const iconPath = path.join(__dirname, "build", "icon.png");
let serverUrl = null;
let backend = null;
let stopping = false;

function stopBackend() {
  stopping = true;
  const child = backend;
  backend = null;
  if (!child || child.exitCode !== null) return;
  child.kill();
  const hard = setTimeout(() => {
    try {
      child.kill("SIGKILL");
    } catch {}
  }, 2000);
  hard.unref?.();
}

function fatal(message, detail) {
  dialog.showErrorBox(message, detail || "");
  app.quit();
}

async function start() {
  const bin = exe();
  if (!bin) {
    fatal(
      "Shrewdness backend not found",
      packaged
        ? "The application package is incomplete."
        : "Build it first:\n\n  cmake -B build && cmake --build build -j",
    );
    return;
  }

  const port = await freePort();
  const url = `http://127.0.0.1:${port}`;

  backend = spawn(
    bin,
    ["--port", String(port), "--web", webRoot()],
    {
      env: { ...process.env, SHREWDNESS_EXAMPLES: examplesRoot() },
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

  let stderr = "";
  backend.stderr.on("data", (d) => (stderr += d.toString().slice(0, 4096)));
  backend.on("exit", (code) => {
    if (stopping) return;
    backend = null;
    fatal("The Shrewdness backend stopped", `exit ${code}\n\n${stderr}`);
  });

  if (!(await waitForBackend(url))) {
    stopBackend();
    fatal("The Shrewdness backend did not start", stderr);
    return;
  }

  serverUrl = url;
  createWindow();
}

function createWindow(opts = {}) {
  const win = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 380,
    minHeight: 480,
    x: opts.x,
    y: opts.y,
    backgroundColor: "#101312",
    show: false,
    ...(fs.existsSync(iconPath) ? { icon: iconPath } : {}),
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  win.once("ready-to-show", () => win.show());
  buildMenu(win, serverUrl);

  const isLocal = (target) => {
    try {
      return new URL(target).origin === serverUrl;
    } catch {
      return false;
    }
  };
  win.webContents.on("will-navigate", (e, target) => {
    if (isLocal(target)) return;
    e.preventDefault();
    shell.openExternal(target);
  });
  win.webContents.setWindowOpenHandler(({ url: target }) => {
    if (!isLocal(target)) shell.openExternal(target);
    return { action: "deny" };
  });

  win.loadURL(
    opts.detach
      ? `${serverUrl}?detach=${encodeURIComponent(opts.detach)}`
      : serverUrl,
  );
  return win;
}

ipcMain.handle("shrewdness:detach", (_e, { key, x, y } = {}) => {
  if (!serverUrl || typeof key !== "string") return false;
  const px = Number.isFinite(x) ? Math.round(x) - 120 : undefined;
  const py = Number.isFinite(y) ? Math.round(y) - 40 : undefined;
  createWindow({ detach: key, x: px, y: py });
  return true;
});

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.whenReady().then(start);
  app.on("second-instance", () => {
    const [win] = BrowserWindow.getAllWindows();
    if (win) {
      if (win.isMinimized()) win.restore();
      win.focus();
    }
  });
  app.on("window-all-closed", () => app.quit());
  app.on("before-quit", stopBackend);
  app.on("will-quit", stopBackend);

  process.on("exit", stopBackend);
  for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"]) {
    process.on(sig, () => {
      stopBackend();
      app.quit();
      process.exit(0);
    });
  }
}
