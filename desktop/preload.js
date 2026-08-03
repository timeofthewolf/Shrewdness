const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("shrewdness", {
  desktop: true,
  detachTab: (payload) => ipcRenderer.invoke("shrewdness:detach", payload),
  onAdoptTab: (fn) => ipcRenderer.on("shrewdness:adopt", (_e, key) => fn(key)),
  files: {
    open: () => ipcRenderer.invoke("files:open"),
    initial: () => ipcRenderer.invoke("files:initial"),
    reload: (id) => ipcRenderer.invoke("files:reload", id),
    write: (id, rel, src) => ipcRenderer.invoke("files:write", id, rel, src),
    mkdir: (id, rel) => ipcRenderer.invoke("files:mkdir", id, rel),
    remove: (id, rel) => ipcRenderer.invoke("files:remove", id, rel),
    rename: (id, from, to) => ipcRenderer.invoke("files:rename", id, from, to),
    close: (id) => ipcRenderer.invoke("files:close", id),
    onChanged: (fn) =>
      ipcRenderer.on("shrewdness:files-changed", (_e, id) => fn(id)),
    onClosed: (fn) =>
      ipcRenderer.on("shrewdness:folder-closed", (_e, id) => fn(id)),
  },
});
