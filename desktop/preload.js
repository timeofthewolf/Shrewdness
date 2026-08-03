const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("shrewdness", {
  desktop: true,
  detachTab: (payload) => ipcRenderer.invoke("shrewdness:detach", payload),
});
