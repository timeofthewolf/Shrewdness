const { Menu, shell, app } = require("electron");

const mac = process.platform === "darwin";

module.exports = function buildMenu(win, url) {
  const template = [
    ...(mac ? [{ role: "appMenu" }] : []),
    {
      label: "File",
      submenu: [
        {
          label: "Reload the workbench",
          accelerator: "CmdOrCtrl+R",
          click: () => win.loadURL(url),
        },
        { type: "separator" },
        mac ? { role: "close" } : { role: "quit" },
      ],
    },
    { role: "editMenu" },
    {
      label: "View",
      submenu: [
        { role: "resetZoom" },
        { role: "zoomIn" },
        { role: "zoomOut" },
        { type: "separator" },
        { role: "togglefullscreen" },
        { role: "toggleDevTools" },
      ],
    },
    { role: "windowMenu" },
    {
      role: "help",
      submenu: [
        {
          label: "Open in browser",
          click: () => shell.openExternal(url),
        },
        {
          label: "Project on GitHub",
          click: () =>
            shell.openExternal("https://github.com/timeofthewolf/Shrewdness"),
        },
        { type: "separator" },
        { label: `Shrewdness ${app.getVersion()}`, enabled: false },
      ],
    },
  ];

  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
};
