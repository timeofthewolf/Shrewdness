<script>
  import { tick } from "svelte";
  import { app } from "../store.svelte.js";
  import {
    project,
    workspace,
    projectFileNames,
    projectFolders,
    projectEntry,
    switchProject,
    createProject,
    duplicateProject,
    renameProject,
    deleteProject,
    addFile,
    addFolder,
    renameFile,
    renameFolder,
    removeFile,
    removeFolder,
    setEntry,
    moveItem,
    uniqueName,
    exportWorkspace,
    importWorkspace,
  } from "./project.svelte.js";
  import { langForName } from "./cm.js";
  import { downloadText, downloadProjectZip } from "./download.js";
  import { ui } from "./media.svelte.js";
  import Icon from "./Icon.svelte";
  import { isDesktop } from "./desktop.js";
  import { isLocal, localFolder, openLocalFolder, closeLocalFolder } from "./localfs.svelte.js";
  import Menu from "./Menu.svelte";

  let {
    activeKey = null,
    onFile = () => {},
    onExample = () => {},
    onSettings = () => {},
  } = $props();

  let sec = $state({ projects: true, examples: false });
  let openProj = $state({ [project.id]: true });
  let openFolder = $state({});
  let renaming = $state(null);
  let renameVal = $state("");
  let confirmDel = $state(null);
  let menu = $state(null);
  let dragItem = $state(null);
  let dropTarget = $state(null);
  let focusKey = $state(project.active ? project.id + "␟" + "file:" + project.active : null);
  let explorerEl = $state();
  const nk = (pid, key) => pid + "␟" + key;

  const base = (p) => p.slice(p.lastIndexOf("/") + 1);
  const fkey = (pid, path) => pid + "|" + path;
  const isOpen = (pid, path) => openFolder[fkey(pid, path)] ?? true;

  function buildTree(names, folders) {
    const root = { dirs: new Map(), files: [] };
    const ensure = (path) => {
      let node = root, cur = "";
      for (const p of path.split("/").filter(Boolean)) {
        cur = cur ? cur + "/" + p : p;
        if (!node.dirs.has(p)) node.dirs.set(p, { name: p, path: cur, dirs: new Map(), files: [] });
        node = node.dirs.get(p);
      }
      return node;
    };
    for (const f of folders) ensure(f);
    for (const name of names) {
      const s = name.lastIndexOf("/");
      if (s < 0) root.files.push({ name, base: name });
      else ensure(name.slice(0, s)).files.push({ name, base: name.slice(s + 1) });
    }
    return root;
  }
  const sortedDirs = (node) => [...node.dirs.values()].sort((a, b) => a.name.localeCompare(b.name));
  const sortedFiles = (node) => [...node.files].sort((a, b) => a.base.localeCompare(b.base));

  function toggleSec(k) {
    sec[k] = !sec[k];
  }

  async function startRename(kind, id, path) {
    renaming = { kind, id, path };
    renameVal = kind === "file" ? path : base(path);
    confirmDel = null;
    await tick();
    document.getElementById("xp-rename")?.select();
  }
  function commitRename() {
    if (!renaming) return;
    const { kind, id, path } = renaming;
    if (id === project.id) {
      if (kind === "proj") renameProject(renameVal);
      else if (kind === "file") renameFile(path, renameVal);
      else if (kind === "folder") {
        const parent = path.includes("/") ? path.slice(0, path.lastIndexOf("/") + 1) : "";
        renameFolder(path, parent + renameVal.replace(/\/$/, ""));
      }
    }
    renaming = null;
  }
  function armDel(key, fn) {
    if (confirmDel === key) {
      fn();
      confirmDel = null;
    } else confirmDel = key;
  }

  function uniqueFolder(dir) {
    const pre = dir ? dir + "/" : "";
    const set = new Set([...projectFolders(project.id), ...projectFileNames(project.id)]);
    let n = "new-folder";
    if (![...set].some((s) => s === pre + n || s.startsWith(pre + n + "/"))) return pre + n;
    for (let i = 2; ; i++) {
      const c = pre + "new-folder-" + i;
      if (![...set].some((s) => s === c || s.startsWith(c + "/"))) return c;
    }
  }
  const NEW_FILE = {
    ".savvy": "",
    ".asm": "; Shrewd assembly — one mnemonic per line; ; begins a comment\n",
    ".shrewd": "",
  };
  function newFileIn(id, dir = "", ext = ".savvy") {
    if (id !== project.id) switchProject(id);
    const name = addFile(uniqueName("untitled", ext, dir), NEW_FILE[ext] ?? "");
    onFile(project.id, name);
    startRename("file", project.id, name);
  }
  function newFolderIn(id, dir = "") {
    if (id !== project.id) switchProject(id);
    const path = addFolder(uniqueFolder(dir));
    openFolder[fkey(project.id, path)] = true;
    startRename("folder", project.id, path);
  }
  function downloadProject(id) {
    if (id !== project.id) switchProject(id);
    downloadProjectZip(project.name, project.files);
  }

  let importInput = $state();
  function doExport() {
    downloadText("shrewdness-workspace.json", exportWorkspace());
  }
  function onImportFile(e) {
    const file = e.currentTarget.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const n = importWorkspace(String(reader.result));
      if (n) openProj[project.id] = true;
    };
    reader.readAsText(file);
    e.currentTarget.value = "";
  }

  const dirOf = (name) => (name.includes("/") ? name.slice(0, name.lastIndexOf("/")) : "");

  const navList = $derived.by(() => {
    workspace.rev;
    const out = [];
    for (const p of workspace.list) {
      out.push({ key: "proj:" + p.id, kind: "proj", pid: p.id, path: "" });
      if (openProj[p.id]) {
        const tree = buildTree(projectFileNames(p.id), projectFolders(p.id));
        const walk = (node) => {
          for (const dir of sortedDirs(node)) {
            out.push({ key: "dir:" + dir.path, kind: "folder", pid: p.id, path: dir.path });
            if (isOpen(p.id, dir.path)) walk(dir);
          }
          for (const f of sortedFiles(node))
            out.push({ key: "file:" + f.name, kind: "file", pid: p.id, path: f.name });
        };
        walk(tree);
      }
    }
    return out;
  });

  const localKey = (it) =>
    it.kind === "proj" ? "proj:" + it.pid : it.kind === "folder" ? "dir:" + it.path : "file:" + it.path;
  const parentNk = (it) => {
    if (it.kind === "proj") return null;
    const d = it.path.includes("/") ? it.path.slice(0, it.path.lastIndexOf("/")) : "";
    return d ? nk(it.pid, "dir:" + d) : nk(it.pid, "proj:" + it.pid);
  };

  async function focusRow() {
    await tick();
    const el = explorerEl?.querySelector(".row.kfocus");
    (el?.querySelector(".label") ?? el)?.focus?.();
    el?.scrollIntoView?.({ block: "nearest" });
  }

  function delFocused(it) {
    const cur = () => { if (it.pid !== project.id) switchProject(it.pid); };
    if (it.kind === "file") {
      if (projectFileNames(it.pid).length <= 1) return;
      armDel(localKey(it), () => { cur(); removeFile(it.path); });
    } else if (it.kind === "folder") {
      armDel(localKey(it), () => { cur(); removeFolder(it.path); });
    } else {
      armDel(localKey(it), () => { cur(); deleteProject(); });
    }
  }

  function onTreeKey(e) {
    if (renaming) return;
    const list = navList;
    if (!list.length) return;
    const idx = list.findIndex((it) => nk(it.pid, it.key) === focusKey);
    const it = idx >= 0 ? list[idx] : null;
    const go = (i) => {
      const j = Math.max(0, Math.min(list.length - 1, i));
      const t = list[j];
      focusKey = nk(t.pid, t.key);
      confirmDel = null;
      focusRow();
    };
    switch (e.key) {
      case "ArrowDown": e.preventDefault(); go(idx < 0 ? 0 : idx + 1); break;
      case "ArrowUp": e.preventDefault(); go(idx < 0 ? 0 : idx - 1); break;
      case "Home": e.preventDefault(); go(0); break;
      case "End": e.preventDefault(); go(list.length - 1); break;
      case "ArrowRight":
        e.preventDefault();
        if (!it) { go(0); break; }
        if (it.kind === "proj" && !openProj[it.pid]) { openProj[it.pid] = true; focusRow(); }
        else if (it.kind === "folder" && !isOpen(it.pid, it.path)) { openFolder[fkey(it.pid, it.path)] = true; focusRow(); }
        else go(idx + 1);
        break;
      case "ArrowLeft": {
        e.preventDefault();
        if (!it) { go(0); break; }
        const openNow =
          (it.kind === "proj" && openProj[it.pid]) ||
          (it.kind === "folder" && isOpen(it.pid, it.path));
        if (openNow) {
          if (it.kind === "proj") openProj[it.pid] = false;
          else openFolder[fkey(it.pid, it.path)] = false;
          focusRow();
        } else {
          const pk = parentNk(it);
          const pi = pk ? list.findIndex((x) => nk(x.pid, x.key) === pk) : -1;
          if (pi >= 0) go(pi);
        }
        break;
      }
      case "F2":
        if (!it) return;
        e.preventDefault();
        if (it.pid !== project.id) switchProject(it.pid);
        if (it.kind === "proj")
          startRename("proj", project.id, workspace.list.find((p) => p.id === it.pid)?.name ?? "");
        else startRename(it.kind, project.id, it.path);
        break;
      case "Delete":
      case "Backspace":
        if (!it) return;
        e.preventDefault();
        delFocused(it);
        break;
      case "Escape":
        if (confirmDel) { confirmDel = null; e.stopPropagation(); }
        break;
    }
  }

  function menuItems(kind, pid, path) {
    const cur = () => { if (pid !== project.id) switchProject(pid); };
    let items = [];
    if (kind === "file") {
      const isEntry = projectEntry(pid) === path;
      items = [
        { label: "Open", icon: "file", run: () => onFile(pid, path) },
        { label: "Rename", icon: "pencil", kbd: "F2", run: () => { cur(); startRename("file", project.id, path); } },
        ...(langForName(path) === "savvy" && !isEntry
          ? [{ label: "Set as entry file", icon: "play", run: () => { cur(); setEntry(path); } }]
          : []),
        { label: "Download", icon: "download", run: () => downloadText(base(path), project.files.find((x) => x.name === path)?.src ?? "") },
        { sep: true },
        { label: "Delete", icon: "trash", danger: true, run: () => { cur(); removeFile(path); } },
      ];
    } else if (kind === "folder") {
      items = [
        { label: "New Savvy File", icon: "newFile", run: () => newFileIn(pid, path, ".savvy") },
        { label: "New Assembly File", icon: "newFile", run: () => newFileIn(pid, path, ".asm") },
        { label: "New Gene File", icon: "dna", run: () => newFileIn(pid, path, ".shrewd") },
        { label: "New Folder", icon: "newFolder", run: () => newFolderIn(pid, path) },
        { label: "Rename", icon: "pencil", kbd: "F2", run: () => { cur(); startRename("folder", project.id, path); } },
        { sep: true },
        { label: "Delete Folder", icon: "trash", danger: true, run: () => { cur(); removeFolder(path); } },
      ];
    } else {
      items = [
        { label: "New Savvy File", icon: "newFile", run: () => newFileIn(pid, "", ".savvy") },
        { label: "New Assembly File", icon: "newFile", run: () => newFileIn(pid, "", ".asm") },
        { label: "New Gene File", icon: "dna", run: () => newFileIn(pid, "", ".shrewd") },
        { label: "New Folder", icon: "newFolder", run: () => newFolderIn(pid) },
        { label: "Rename Project", icon: "pencil", run: () => startRename("proj", pid, workspace.list.find((p) => p.id === pid)?.name ?? "") },
        { label: "Duplicate", icon: "copy", run: () => { cur(); duplicateProject(); openProj[project.id] = true; } },
        { label: "Download .zip", icon: "download", run: () => downloadProject(pid) },
        { sep: true },
        { label: "Delete Project", icon: "trash", danger: true, run: () => { cur(); deleteProject(); } },
      ];
    }
    return items;
  }

  function openMenu(e, kind, pid, path) {
    e.preventDefault();
    e.stopPropagation();
    menu = { x: e.clientX, y: e.clientY, items: menuItems(kind, pid, path) };
  }

  function openRowMenu(e, kind, pid, path) {
    e.preventDefault();
    e.stopPropagation();
    const r = e.currentTarget.getBoundingClientRect();
    menu = { x: r.right, y: r.bottom + 2, items: menuItems(kind, pid, path) };
  }

  function onDragStart(e, type, pid, path) {
    dragItem = { type, path, pid };
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("text/plain", path);
  }
  function onDragOver(e, pid, key) {
    if (!dragItem) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "move";
    dropTarget = key;
  }
  function onDrop(e, pid, destDir) {
    e.preventDefault();
    const item = dragItem;
    dragItem = null;
    dropTarget = null;
    if (!item) return;
    if (item.pid === project.id && pid === project.id) {
      const b = base(item.path);
      const dest = destDir ? destDir.replace(/\/$/, "") + "/" + b : b;
      if (dest === item.path) return;
      if (item.type === "file") renameFile(item.path, dest);
      else if (!(dest + "/").startsWith(item.path + "/")) renameFolder(item.path, dest);
    } else {
      moveItem(item.pid, item.path, item.type, pid, destDir);
    }
  }
  const endDrag = () => {
    dragItem = null;
    dropTarget = null;
  };
</script>

{#snippet fileRow(pid, f, depth)}
  {@const key = "file:" + f.name}
  {@const isEntry = projectEntry(pid) === f.name}
  <div
    class="row file tree"
    class:active={pid === project.id && activeKey === key}
    class:kfocus={focusKey === nk(pid, key)}
    class:armed={confirmDel === key}
    class:drop={dropTarget === "file:" + f.name}
    style="--d:{depth}"
    draggable={!ui.touch}
    ondragstart={(e) => onDragStart(e, "file", pid, f.name)}
    ondragover={(e) => onDragOver(e, pid, "file:" + f.name)}
    ondragleave={() => (dropTarget = null)}
    ondrop={(e) => onDrop(e, pid, dirOf(f.name))}
    ondragend={endDrag}
    oncontextmenu={(e) => openMenu(e, "file", pid, f.name)}
  >
    {#if renaming?.kind === "file" && renaming.path === f.name && pid === project.id}
      <span class="lead"><Icon name="file" size={14} /></span>
      <input id="xp-rename" class="rename" bind:value={renameVal}
        onblur={commitRename}
        onkeydown={(e) => { if (e.key === "Enter") commitRename(); if (e.key === "Escape") renaming = null; }} />
    {:else}
      <button
        class="label"
        title={f.name}
        draggable={!ui.touch}
        ondragstart={(e) => onDragStart(e, "file", pid, f.name)}
        ondragend={endDrag}
        onclick={() => { focusKey = nk(pid, key); onFile(pid, f.name); }}
        ondblclick={() => pid === project.id && startRename("file", pid, f.name)}
      >
        <span class="lead ic-{langForName(f.name)}"><Icon name="file" size={14} /></span>
        <span class="nm">{f.base}</span>
        {#if isEntry}<span class="entry" title="entry file"><Icon name="play" size={9} /></span>{/if}
        {#if pid === project.id && project.diags[f.name]}<span class="errdot"></span>{/if}
      </button>
      {#if ui.touch}
        <button class="icon more" aria-label="file actions"
          onclick={(e) => openRowMenu(e, "file", pid, f.name)}><Icon name="dots" size={16} /></button>
      {:else if pid === project.id}
        <span class="rowacts">
          {#if !isEntry && langForName(f.name) === "savvy"}
            <span class="icon" role="button" tabindex="0" title="set as entry file"
              onclick={() => setEntry(f.name)} onkeydown={() => {}}><Icon name="play" size={13} /></span>
          {/if}
          <span class="icon" role="button" tabindex="0" title="download"
            onclick={() => downloadText(f.base, project.files.find((x) => x.name === f.name)?.src ?? "")} onkeydown={() => {}}><Icon name="download" size={13} /></span>
          {#if project.files.length > 1}
            <span class="icon del" class:armed={confirmDel === key} role="button" tabindex="0"
              title={confirmDel === key ? "click again" : "delete file"}
              onclick={() => armDel(key, () => removeFile(f.name))} onkeydown={() => {}}>
              {#if confirmDel === key}?{:else}<Icon name="trash" size={13} />{/if}</span>
          {/if}
        </span>
      {/if}
    {/if}
  </div>
{/snippet}

{#snippet dirNode(pid, dir, depth)}
  {@const open = isOpen(pid, dir.path)}
  {@const dkey = "dir:" + dir.path}
  <div
    class="row folder tree"
    class:kfocus={focusKey === nk(pid, dkey)}
    class:armed={confirmDel === dkey}
    class:drop={dropTarget === dkey}
    style="--d:{depth}"
    draggable={!ui.touch}
    ondragstart={(e) => onDragStart(e, "folder", pid, dir.path)}
    ondragover={(e) => onDragOver(e, pid, dkey)}
    ondragleave={() => (dropTarget = null)}
    ondrop={(e) => onDrop(e, pid, dir.path)}
    ondragend={endDrag}
    oncontextmenu={(e) => openMenu(e, "folder", pid, dir.path)}
  >
    {#if renaming?.kind === "folder" && renaming.path === dir.path && pid === project.id}
      <span class="chev"><Icon name="chevron" size={13} /></span>
      <span class="lead"><Icon name="folder" size={14} /></span>
      <input id="xp-rename" class="rename" bind:value={renameVal}
        onblur={commitRename}
        onkeydown={(e) => { if (e.key === "Enter") commitRename(); if (e.key === "Escape") renaming = null; }} />
    {:else}
      <button
        class="label"
        draggable={!ui.touch}
        ondragstart={(e) => onDragStart(e, "folder", pid, dir.path)}
        ondragend={endDrag}
        onclick={() => { focusKey = nk(pid, dkey); openFolder[fkey(pid, dir.path)] = !open; }}
        ondblclick={() => pid === project.id && startRename("folder", pid, dir.path)}
      >
        <span class="chev" class:open><Icon name="chevron" size={13} /></span>
        <span class="lead ic-folder"><Icon name={open ? "folderOpen" : "folder"} size={14} /></span>
        <span class="nm">{dir.name}</span>
      </button>
      {#if ui.touch}
        <button class="icon more" aria-label="folder actions"
          onclick={(e) => openRowMenu(e, "folder", pid, dir.path)}><Icon name="dots" size={16} /></button>
      {:else if pid === project.id}
        <span class="rowacts">
          <span class="icon" role="button" tabindex="0" title="new file" onclick={() => newFileIn(pid, dir.path)} onkeydown={() => {}}><Icon name="newFile" size={13} /></span>
          <span class="icon" role="button" tabindex="0" title="new folder" onclick={() => newFolderIn(pid, dir.path)} onkeydown={() => {}}><Icon name="newFolder" size={13} /></span>
          <span class="icon" role="button" tabindex="0" title="rename" onclick={() => startRename("folder", pid, dir.path)} onkeydown={() => {}}><Icon name="pencil" size={13} /></span>
          <span class="icon del" class:armed={confirmDel === dkey} role="button" tabindex="0"
            title={confirmDel === dkey ? "click again — deletes contents" : "delete folder"}
            onclick={() => armDel(dkey, () => removeFolder(dir.path))} onkeydown={() => {}}>
            {#if confirmDel === dkey}?{:else}<Icon name="trash" size={13} />{/if}</span>
        </span>
      {/if}
    {/if}
  </div>
  {#if open}
    {#each sortedDirs(dir) as sub (sub.path)}{@render dirNode(pid, sub, depth + 1)}{/each}
    {#each sortedFiles(dir) as f (f.name)}{@render fileRow(pid, f, depth + 1)}{/each}
  {/if}
{/snippet}

<div class="explorer" role="tree" tabindex="0" bind:this={explorerEl} onkeydown={onTreeKey}>
  <div class="xhead">
    <span class="title">Explorer</span>
    <button class="icon" title="settings" onclick={onSettings}><Icon name="gear" size={15} /></button>
  </div>

  <div class="section">
    <button class="sechead" onclick={() => toggleSec("projects")}>
      <span class="chev" class:open={sec.projects}><Icon name="chevron" size={12} /></span>
      <span class="secname">{isLocal() ? "Folder" : "Projects"}</span>
      <span class="grow"></span>
      {#if isDesktop}
        <span class="icon" role="button" tabindex="0"
          title={isLocal() ? `${localFolder.root} — click to close` : "open a folder from disk"}
          onclick={(e) => { e.stopPropagation(); isLocal() ? closeLocalFolder() : openLocalFolder(); }}
          onkeydown={() => {}}><Icon name={isLocal() ? "close" : "folderOpen"} size={13} /></span>
      {/if}
      <span class="icon" role="button" tabindex="0" title="import workspace (.json)"
        onclick={(e) => { e.stopPropagation(); importInput?.click(); }} onkeydown={() => {}}><Icon name="upload" size={13} /></span>
      <span class="icon" role="button" tabindex="0" title="export all projects (.json)"
        onclick={(e) => { e.stopPropagation(); doExport(); }} onkeydown={() => {}}><Icon name="download" size={13} /></span>
      <span class="icon" role="button" tabindex="0" title="new project"
        onclick={(e) => { e.stopPropagation(); createProject(); openProj[project.id] = true; }} onkeydown={() => {}}><Icon name="plus" size={14} /></span>
    </button>

    {#if sec.projects}
      {#each workspace.list as p (p.id)}
        {@const _rev = workspace.rev}
        {@const tree = buildTree((_rev, projectFileNames(p.id)), projectFolders(p.id))}
        <div class="pnode">
          <div
            class="row proj tree"
            class:current={p.id === project.id}
            class:kfocus={focusKey === nk(p.id, "proj:" + p.id)}
            class:armed={confirmDel === "proj:" + p.id}
            class:drop={dropTarget === "proj:" + p.id}
            style="--d:0"
            ondragover={(e) => onDragOver(e, p.id, "proj:" + p.id)}
            ondragleave={() => (dropTarget = null)}
            ondrop={(e) => onDrop(e, p.id, "")}
            oncontextmenu={(e) => openMenu(e, "project", p.id, "")}
          >
            {#if renaming?.kind === "proj" && renaming.id === p.id}
              <span class="chev"><Icon name="chevron" size={13} /></span>
              <input id="xp-rename" class="rename" bind:value={renameVal}
                onblur={commitRename}
                onkeydown={(e) => { if (e.key === "Enter") commitRename(); if (e.key === "Escape") renaming = null; }} />
            {:else}
              <button class="label" onclick={() => { focusKey = nk(p.id, "proj:" + p.id); openProj[p.id] = !(openProj[p.id] ?? false); }}
                ondblclick={() => startRename("proj", p.id, p.name)}>
                <span class="chev" class:open={openProj[p.id]}><Icon name="chevron" size={13} /></span>
                <span class="lead ic-folder"><Icon name={openProj[p.id] ? "folderOpen" : "folder"} size={15} /></span>
                <span class="nm">{p.name}</span>
              </button>
              {#if ui.touch}
                <button class="icon more" aria-label="project actions"
                  onclick={(e) => openRowMenu(e, "project", p.id, "")}><Icon name="dots" size={16} /></button>
              {:else}
              <span class="rowacts">
                <span class="icon" role="button" tabindex="0" title="new file" onclick={() => newFileIn(p.id)} onkeydown={() => {}}><Icon name="newFile" size={13} /></span>
                <span class="icon" role="button" tabindex="0" title="new folder" onclick={() => newFolderIn(p.id)} onkeydown={() => {}}><Icon name="newFolder" size={13} /></span>
                <span class="icon" role="button" tabindex="0" title="download .zip" onclick={() => downloadProject(p.id)} onkeydown={() => {}}><Icon name="download" size={13} /></span>
                <span class="icon" role="button" tabindex="0" title="duplicate"
                  onclick={() => { if (p.id !== project.id) switchProject(p.id); duplicateProject(); openProj[project.id] = true; }} onkeydown={() => {}}><Icon name="copy" size={13} /></span>
                <span class="icon del" class:armed={confirmDel === "proj:" + p.id} role="button" tabindex="0"
                  title={confirmDel === "proj:" + p.id ? "click again to delete" : "delete project"}
                  onclick={() => armDel("proj:" + p.id, () => { if (p.id !== project.id) switchProject(p.id); deleteProject(); })} onkeydown={() => {}}>
                  {#if confirmDel === "proj:" + p.id}?{:else}<Icon name="trash" size={13} />{/if}</span>
              </span>
              {/if}
            {/if}
          </div>

          {#if openProj[p.id]}
            {#each sortedDirs(tree) as dir (dir.path)}{@render dirNode(p.id, dir, 1)}{/each}
            {#each sortedFiles(tree) as f (f.name)}{@render fileRow(p.id, f, 1)}{/each}
          {/if}
        </div>
      {/each}
    {/if}
  </div>

  <div class="section">
    <button class="sechead" onclick={() => toggleSec("examples")}>
      <span class="chev" class:open={sec.examples}><Icon name="chevron" size={12} /></span>
      <span class="secname">Examples</span>
      <span class="grow"></span>
    </button>
    {#if sec.examples}
      {#each app.examples as ex (ex.name)}
        <div class="row file tree" style="--d:1">
          <button class="label" onclick={() => onExample(ex.name)}>
            <span class="lead ic-savvy"><Icon name="file" size={13} /></span>
            <span class="nm mono">{ex.name}</span>
            <span class="plus"><Icon name="plus" size={12} /></span>
          </button>
        </div>
      {/each}
    {/if}
  </div>
</div>

<input
  bind:this={importInput}
  type="file"
  accept=".json,application/json"
  style="display:none"
  onchange={onImportFile}
/>

{#if menu}
  <Menu x={menu.x} y={menu.y} items={menu.items} onclose={() => (menu = null)} />
{/if}

<style>
  .explorer {
    height: 100%;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    font-size: 12.5px;
    user-select: none;
  }
  .xhead {
    display: flex;
    align-items: center;
    padding: 8px 8px 8px 12px;
    font-size: 10.5px;
    font-weight: 700;
    letter-spacing: 0.1em;
    text-transform: uppercase;
    color: var(--muted);
  }
  .xhead .title {
    flex: 1;
  }
  .section {
    display: flex;
    flex-direction: column;
    padding-bottom: 4px;
  }
  .sechead {
    display: flex;
    align-items: center;
    gap: 4px;
    border: none;
    background: none;
    border-radius: 0;
    width: 100%;
    text-align: left;
    padding: 3px 10px 3px 8px;
    color: var(--ink-2);
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }
  .sechead:hover {
    color: var(--ink);
  }
  .count {
    font-size: 10.5px;
    font-weight: 600;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
    margin-left: 4px;
  }
  .chev {
    display: inline-flex;
    align-items: center;
    color: var(--muted);
    transition: transform 0.12s;
  }
  .chev.open {
    transform: rotate(90deg);
  }
  .grow {
    flex: 1;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 1px;
    padding-right: 6px;
    border-left: 2px solid transparent;
    min-height: 23px;
  }
  .row.tree {
    padding-left: calc(var(--d, 0) * 13px);
    background-image: repeating-linear-gradient(
      to right,
      color-mix(in srgb, var(--grid) 90%, transparent) 0 1px,
      transparent 1px 13px
    );
    background-size: calc(var(--d, 0) * 13px) 100%;
    background-repeat: no-repeat;
    background-position: 13px 0;
  }
  .row:hover {
    background-color: color-mix(in srgb, var(--ink) 5%, transparent);
  }
  .row.active {
    background-color: color-mix(in srgb, var(--accent) 14%, transparent);
    border-left-color: var(--accent);
  }
  .row.kfocus {
    box-shadow: inset 0 0 0 1.5px color-mix(in srgb, var(--accent) 60%, transparent);
    border-radius: 4px;
  }
  .row.armed {
    background-color: color-mix(in srgb, var(--crit) 13%, transparent);
    box-shadow: inset 0 0 0 1.5px var(--crit);
    border-radius: 4px;
  }
  .row.drop {
    background-color: color-mix(in srgb, var(--accent) 22%, transparent);
    box-shadow: inset 0 0 0 1px var(--accent);
    border-radius: 4px;
  }
  .explorer:focus,
  .explorer:focus-visible,
  .label:focus,
  .label:focus-visible {
    outline: none;
  }
  .row[draggable="true"] {
    cursor: grab;
  }
  .label {
    display: flex;
    align-items: center;
    gap: 6px;
    flex: 1;
    min-width: 0;
    border: none;
    background: none;
    border-radius: 0;
    text-align: left;
    padding: 3px 2px 3px 6px;
    color: var(--ink-2);
    font: inherit;
  }
  .row.active .label,
  .row.current .label {
    color: var(--ink);
  }
  .lead {
    display: inline-flex;
    color: var(--muted);
  }
  .lead.ic-folder {
    color: var(--s4);
  }
  .lead.ic-savvy {
    color: var(--s1);
  }
  .lead.ic-asm {
    color: var(--s4);
  }
  .lead.ic-genes {
    color: var(--s5);
  }
  .lead.star {
    color: var(--s4);
  }
  .nm {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .row.proj .nm {
    font-weight: 600;
    color: var(--ink);
  }
  .row.folder .nm {
    font-weight: 500;
  }
  .meta {
    color: var(--muted);
    font-size: 10.5px;
    font-variant-numeric: tabular-nums;
    flex: none;
  }
  .plus {
    margin-left: auto;
    color: var(--muted);
    display: inline-flex;
  }
  .ldot {
    flex: none;
    width: 9px;
    height: 9px;
    margin-left: 3px;
    border-radius: 50%;
  }
  .entry {
    color: var(--accent);
    display: inline-flex;
    flex: none;
  }
  .errdot {
    flex: none;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--crit);
    margin-left: 3px;
  }
  .rowacts {
    display: none;
    align-items: center;
    gap: 0;
  }
  .row:hover .rowacts {
    display: flex;
  }
  .icon {
    display: inline-flex;
    align-items: center;
    border: none;
    background: none;
    color: var(--muted);
    padding: 2px 3px;
    border-radius: 5px;
    cursor: pointer;
  }
  .icon:hover {
    color: var(--ink);
    background: color-mix(in srgb, var(--ink) 10%, transparent);
  }
  .icon.del.armed {
    color: var(--crit);
    font-size: 11px;
  }
  .rename {
    flex: 1;
    min-width: 0;
    font: 12px var(--mono);
    padding: 1px 5px;
    margin-left: 2px;
  }
  .more {
    flex: none;
    align-self: stretch;
    padding: 0 11px;
    border: none;
    background: none;
    border-radius: 6px;
  }
  @media (hover: none) {
    .explorer {
      font-size: 14px;
    }
    .row {
      min-height: 38px;
      padding-right: 2px;
    }
    .label {
      padding: 8px 2px 8px 6px;
    }
    .sechead {
      padding: 9px 10px 9px 8px;
    }
    .sechead .icon,
    .xhead .icon {
      padding: 9px 10px;
    }
    .rename {
      font-size: 16px;
    }
  }
  .sub {
    padding: 2px 8px 2px 40px;
    font-size: 11.5px;
  }
  .empty {
    padding: 3px 12px;
    color: var(--muted);
    font-size: 11.5px;
  }
</style>
