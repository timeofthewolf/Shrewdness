<script>
  import Modal from "./Modal.svelte";
  import { settings, setSetting, resetSettings, ACCENTS } from "./settings.svelte.js";

  let { onclose = () => {} } = $props();

  const KEYMAPS = [
    ["standard", "Standard"],
    ["vim", "Vim"],
    ["emacs", "Emacs"],
  ];
</script>

<Modal title="Settings" {onclose}>
  <div class="settings">
    <section>
      <h3>Editor</h3>

      <label class="row range">
        <span class="lbl">Font size <em>{settings.fontSize}px</em></span>
        <input
          type="range"
          min="10"
          max="20"
          step="1"
          value={settings.fontSize}
          oninput={(e) => setSetting("fontSize", +e.currentTarget.value)}
        />
      </label>

      <label class="row range">
        <span class="lbl">Tab size <em>{settings.tabSize}</em></span>
        <input
          type="range"
          min="2"
          max="8"
          step="1"
          value={settings.tabSize}
          oninput={(e) => setSetting("tabSize", +e.currentTarget.value)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl">Word wrap</span>
        <input
          type="checkbox"
          checked={settings.lineWrapping}
          onchange={(e) => setSetting("lineWrapping", e.currentTarget.checked)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl">Highlight active line</span>
        <input
          type="checkbox"
          checked={settings.activeLine}
          onchange={(e) => setSetting("activeLine", e.currentTarget.checked)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl">Auto-close brackets</span>
        <input
          type="checkbox"
          checked={settings.closeBrackets}
          onchange={(e) => setSetting("closeBrackets", e.currentTarget.checked)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl">Autocomplete</span>
        <input
          type="checkbox"
          checked={settings.completion}
          onchange={(e) => setSetting("completion", e.currentTarget.checked)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl">Indent guides</span>
        <input
          type="checkbox"
          checked={settings.indentGuides}
          onchange={(e) => setSetting("indentGuides", e.currentTarget.checked)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl"
          >Minimap{#if settings.minimap && settings.lineWrapping}<em>
            (hidden while word wrap is on)</em
          >{/if}</span
        >
        <input
          type="checkbox"
          checked={settings.minimap}
          onchange={(e) => setSetting("minimap", e.currentTarget.checked)}
        />
      </label>

      <label class="row toggle">
        <span class="lbl"
          >Compile as you type <em>(live error underlines)</em></span
        >
        <input
          type="checkbox"
          checked={settings.lintOnType}
          onchange={(e) => setSetting("lintOnType", e.currentTarget.checked)}
        />
      </label>
    </section>

    <section>
      <h3>Appearance</h3>
      <div class="row">
        <span class="lbl">Accent colour</span>
        <div class="swatches">
          {#each ACCENTS as a (a.slot)}
            <button
              class="swatch"
              class:on={settings.accent === a.slot}
              style:background={a.css}
              title={a.label}
              aria-label={a.label}
              onclick={() => setSetting("accent", a.slot)}
            ></button>
          {/each}
        </div>
      </div>
    </section>

    <section>
      <h3>Keybindings</h3>
      <div class="row seg">
        <span class="lbl">Keymap</span>
        <div class="segbtns">
          {#each KEYMAPS as [id, label] (id)}
            <button
              class:on={settings.keymap === id}
              onclick={() => setSetting("keymap", id)}>{label}</button
            >
          {/each}
        </div>
      </div>

      {#if settings.keymap === "vim"}
        <label class="row toggle">
          <span class="lbl">Map <code>jk</code>/<code>jj</code> to Esc</span>
          <input
            type="checkbox"
            checked={settings.vimJk}
            onchange={(e) => setSetting("vimJk", e.currentTarget.checked)}
          />
        </label>
        <p class="note">
          Ex-commands are wired to the IDE: <code>:w</code> saves,
          <code>:run</code> runs, <code>:build</code> builds,
          <code>:q</code> closes the tab, <code>:wq</code> does both. The current
          mode shows in the status bar.
        </p>
      {:else if settings.keymap === "emacs"}
        <p class="note">
          Standard Emacs motion and editing commands are active
          (C-s search, C-w/M-w, C-space mark, …).
        </p>
      {:else}
        <p class="note">
          Ctrl-F find, Ctrl-D add next occurrence, Ctrl-/ (via keymap) and the
          usual editor shortcuts.
        </p>
      {/if}
    </section>

    <section>
      <h3>Shortcuts</h3>
      <div class="keys">
        <div><kbd>Ctrl</kbd><kbd>Enter</kbd><span>run · <kbd>F5</kbd> too</span></div>
        <div><kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>B</kbd><span>build</span></div>
        <div><kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>C</kbd><span>stop the run</span></div>
        <div><kbd>Ctrl</kbd><kbd>P</kbd><span>go to file</span></div>
        <div><kbd>Ctrl</kbd><kbd>Shift</kbd><kbd>P</kbd><span>command palette</span></div>
        <div><kbd>Ctrl</kbd><kbd>S</kbd><span>save</span></div>
        <div><kbd>Ctrl</kbd><kbd>B</kbd><span>toggle sidebar</span></div>
        <div><kbd>Ctrl</kbd><kbd>`</kbd><span>toggle console</span></div>
        <div><kbd>Ctrl</kbd><kbd>,</kbd><span>settings</span></div>
        <div><kbd>Ctrl</kbd><kbd>PgUp/Dn</kbd><span>prev / next tab</span></div>
        <div><kbd>Alt</kbd><kbd>1…9</kbd><span>jump to tab</span></div>
        <div><span class="grp">explorer</span></div>
        <div><kbd>↑</kbd><kbd>↓</kbd><span>move · <kbd>←</kbd><kbd>→</kbd> fold</span></div>
        <div><kbd>Enter</kbd><span>open · <kbd>F2</kbd> rename</span></div>
        <div><kbd>Del</kbd><span>delete (twice to confirm)</span></div>
        <div><span class="grp">editor</span></div>
        <div><kbd>Ctrl</kbd><kbd>F</kbd><span>find / replace</span></div>
        <div><kbd>Ctrl</kbd><kbd>D</kbd><span>select next match</span></div>
        <div><kbd>Ctrl</kbd><kbd>Space</kbd><span>autocomplete</span></div>
        <div><kbd>Alt</kbd><span>+ click adds a cursor</span></div>
        <div><kbd>Ctrl</kbd><kbd>/</kbd><span>toggle comment</span></div>
        <div><span class="grp">right-click anywhere for a menu</span></div>
      </div>
    </section>

    <div class="foot">
      <button class="ghost" onclick={resetSettings}>reset to defaults</button>
      <span class="grow"></span>
      <button class="primary" onclick={onclose}>done</button>
    </div>
  </div>
</Modal>

<style>
  .settings {
    display: flex;
    flex-direction: column;
    gap: 18px;
  }
  section {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  h3 {
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--muted);
    margin: 0 0 2px;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 14px;
    min-height: 30px;
  }
  .lbl {
    flex: 1;
    font-size: 13px;
    color: var(--ink);
  }
  .lbl em {
    color: var(--muted);
    font-style: normal;
  }
  .range input {
    width: 220px;
  }
  .toggle input {
    width: 34px;
    height: 20px;
    accent-color: var(--accent);
  }
  .seg .segbtns {
    display: flex;
    gap: 4px;
  }
  .segbtns button {
    padding: 4px 14px;
    font-size: 12.5px;
  }
  .segbtns button.on {
    background: var(--accent);
    border-color: var(--accent);
    color: var(--accent-ink);
    font-weight: 600;
  }
  .swatches {
    display: flex;
    gap: 8px;
  }
  .swatch {
    width: 24px;
    height: 24px;
    padding: 0;
    border-radius: 50%;
    border: 2px solid transparent;
    cursor: pointer;
  }
  .swatch.on {
    border-color: var(--ink);
    box-shadow: 0 0 0 2px var(--surface) inset;
  }
  .note {
    margin: 2px 0 0;
    font-size: 12px;
    color: var(--muted);
    line-height: 1.5;
  }
  code {
    font-family: var(--mono);
    font-size: 11.5px;
    background: color-mix(in srgb, var(--ink) 7%, transparent);
    padding: 0 4px;
    border-radius: 4px;
  }
  .keys {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 6px 18px;
  }
  .keys div {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 12.5px;
    color: var(--ink-2);
  }
  .keys span {
    color: var(--ink-2);
  }
  .keys .grp {
    color: var(--muted);
    font-size: 10.5px;
    font-weight: 700;
    letter-spacing: 0.07em;
    text-transform: uppercase;
    padding-top: 4px;
  }
  kbd {
    font: 11px var(--mono);
    border: 1px solid var(--border);
    border-bottom-width: 2px;
    border-radius: 4px;
    padding: 1px 5px;
    color: var(--ink);
    background: var(--page);
  }
  .foot {
    display: flex;
    align-items: center;
    gap: 8px;
    padding-top: 6px;
    border-top: 1px solid var(--grid);
  }
  .grow {
    flex: 1;
  }
</style>
