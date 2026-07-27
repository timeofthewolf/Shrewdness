# Shrewdness

The IDE for [Savvy](../src/savvy/README.md) and [Shrewd](../src/shrewd/README.md):
a Svelte app in the browser, a small C++ server behind it
(`src/tools/shrewdness.cpp`) that owns the actual toolchain.

```sh
cd web && npm install && npm run build     # once
./build/shrewdness                         # http://127.0.0.1:7070/
```

For front-end work, `npm run dev` serves with hot reload on :5173 and proxies
`/api` to a `shrewdness` on :7070, so run both.

## What it is for

Writing Savvy is only half of it. The other half is watching what the compiler
does with what you wrote — because in a language whose programs are lists of
integers, the genes *are* the artifact, and a Savvy program is just a
convenient way of saying which ones you want.

So the editor is on the left and the toolchain is on the right, live. Type, and
within 250 ms the assembly, the gene list and the decompiled form all catch up.

## The panes

**Editor.** CodeMirror 6 with syntax highlighting, folding, autocomplete and
hover documentation for all 56 opcodes (pulled from the server's `/api/isa`, so
the docs cannot drift from the ISA). Errors from the compiler are underlined in
place. Three file types, each with its own mode: `.savvy`, `.asm` and
`.shrewd` (raw genes).

**Explorer.** Multiple projects, files, real folders, drag to move, rename,
delete. The bundled [examples](../examples/README.md) are listed in their own
section; opening one copies it into your project. Import/export the whole
workspace as JSON, or download a project as a `.zip`.

**Compiler.** The active file in all three forms — `savvy`, `assembly`,
`genes` — recompiled as you type. The gene count sits in the dock header.

**Genome inspector.** The genome as a coloured strip, one cell per gene, tinted
by instruction family (push, arith, control, call, io, reproduction…), with a
legend. Pick any other open file and it diffs against it, marking every changed
gene — which is how you see what one edit did to the compiled output. Below it,
the flat disassembly: index, mnemonic, raw gene value.

**Debugger.** Runs the program under a tracing VM and lets you scrub through it.
Step forward and back, click a line to set a breakpoint and jump between them,
and at every step see the registers, the data stack, every memory cell written
so far, and the output as it accumulates. Traces are capped (20,000 steps) —
this is for understanding a program, not for running one.

**Terminal.** A real run, on its own thread on the server. `putchar` and `print`
stream out as they happen; `input()` blocks and waits for you to type a line.
Ctrl-C stops it, Ctrl-D sends EOF, Ctrl-L clears. When the run ends you get the
halt reason, the step count, the final stack and registers, and any offspring
genomes it committed.

## Getting around

Split panes horizontally or vertically, drag tabs between groups, and dock any
tool where you want it. Everything — layout, open files, project contents — is
persisted in `localStorage`.

| | |
|---|---|
| `Ctrl+Shift+P` | command palette (everything below is in it) |
| `Ctrl+P` | go to file |
| `Ctrl+Enter` / `F5` | run |
| `Ctrl+Shift+B` | build |
| `Ctrl+Shift+C` | stop the running program |
| `Ctrl+\`` | toggle the terminal |
| `Ctrl+Shift+\` | split editor right |
| `Ctrl+B` | toggle the sidebar |
| `Ctrl+,` | settings |
| `Alt+1..9` | nth tab in the focused group |
| `Ctrl+PageUp/Down` | previous / next tab |

Settings cover font size, tab width, line wrapping, lint-on-type, bracket
closing, completion, minimap, indent guides, an accent colour, and the keymap:
**standard, vim or emacs**. In vim mode `:w`, `:q`, `:run` and `:build` are
wired to the real actions.

Light and dark themes follow the system by default; the toggle is in the status
bar and in the right-click menu.

## The backend

`shrewdness` is one file, ~1,000 lines, and holds no database, no accounts and
no state you care about. It serves the built app and eight endpoints:

| endpoint | does |
|---|---|
| `POST /api/build` | compile a file set to a genome; returns gene list, assembly and decompiled Savvy, or an error with file/line/column |
| `POST /api/trace` | run under a step-recording VM; returns the disassembly plus per-step pc, depth, registers, stack and memory writes |
| `POST /api/console/start` | start a run on its own thread; returns a session id |
| `GET /api/console/poll` | new output since an offset, plus the result once it finishes |
| `POST /api/console/input` | feed a line (or EOF) to a waiting `input()` |
| `POST /api/console/kill` | stop a session |
| `GET /api/isa` | the 56 opcodes with mnemonics, sizes and documentation |
| `GET /api/examples` | the bundled Savvy examples |

`/api/build` takes a whole file map plus an entry point, which is how `include`
works in the browser: the compiler's resolver callback is pointed at the
project's files instead of at a directory.

Runs are budgeted and bounded — concurrent-session caps, a step cap, a memory
cap, output caps — because the thing on the other end of the socket is an
arbitrary genome and arbitrary genomes are allowed to loop forever. Idle
sessions are swept; a run left alone for five minutes is killed.

Connections are served on a thread pool, so one slow client cannot stall the
others, and each visitor is identified by an opaque `sid` cookie. That cookie
carries no data — it exists so quotas are per-visitor rather than global, and
so nobody can poll or kill someone else's run by guessing its id.

Flags:

```
shrewdness [options]
  --port N        port to listen on (default 7070)
  --net           bind 0.0.0.0 instead of 127.0.0.1
  --web DIR       where the built front end lives
  --workers N     connection threads (default 16)
  --public        tighten every per-visitor limit and turn on rate limiting
  --cloudflare    behind a Cloudflare tunnel: client address from
                  CF-Connecting-IP, cookies marked Secure
  --trust-proxy   client address from X-Forwarded-For (nginx, Caddy)
  --client-ip-header NAME    read it from some other header
  --secure-cookies           always mark the session cookie Secure
  --origin URL    send Access-Control-Allow-Origin: URL
  env: SHREWDNESS_WEB, SHREWDNESS_EXAMPLES
```

It binds to localhost unless you pass `--net`, and there is no authentication —
the API compiles and runs whatever it is sent. Anything reachable from the
internet wants `--public`, which cuts the step, memory and output ceilings and
turns on per-visitor rate limiting.

## Hosting it

Projects live in the visitor's own browser (`localStorage`, key
`shrewdness-workspace-v1`), so a public instance needs no database and has no
accounts — but it does hand anonymous people a CPU.

```sh
docker compose up -d --build          # the root docker-compose.yml
```

That runs the container with `--public`, capped at 2 CPUs and 1 GiB, read-only
and unprivileged, published on **`127.0.0.1:7070` only**. The loopback bind is
load-bearing: Docker installs its own iptables rules ahead of ufw/firewalld, so
a bare `7070:7070` stays reachable from the internet even with the firewall
closed, and anyone who finds it walks straight around whatever proxy or tunnel
you put in front.

Then point your tunnel or proxy at it, and **tell the backend where the real
client address is** — without that, every visitor arriving through the proxy
shares one rate-limit bucket:

| in front | add | reads |
|---|---|---|
| `cloudflared` | `--cloudflare` | `CF-Connecting-IP` |
| nginx, Caddy, Traefik | `--trust-proxy` | `X-Forwarded-For` |

Behind Cloudflare it must be `--cloudflare`, not `--trust-proxy`:
`X-Forwarded-For` is a chain and anything the visitor sent arrives ahead of the
genuine entry, so its left-most value is a number the visitor chose.
`CF-Connecting-IP` is set by Cloudflare and holds one address — the real one.
Trusting either header is only sound because nothing but the proxy can reach
the port. The server prints a warning at startup if `--public` is on and
neither flag was given.

### What `--public` changes

| | default | `--public` |
|---|---|---|
| steps per run (default / ceiling) | 200M / 1B | 50M / 200M |
| memory ceiling per run | 4M cells (32 MiB) | 1M cells (8 MiB) |
| output ceiling per run | 1 MiB | 256 KiB |
| concurrent runs per visitor | 4 | 2 |
| concurrent runs, whole server | 16 | 8 |
| rate limiting | off | on |

200M steps is roughly 0.6 s of CPU, so `while (1 == 1)` costs a visitor half a
second and then halts with `OutOfGas`.

Rate limiting is token buckets on **two keys, both of which must have room**:
the session cookie (30 runs/min, 1200 other requests/min) and the client
address (120 and 3600). The cookie shares the machine out fairly; the address
is the one that binds, because a client that discards cookies is handed a fresh
identity on every request. The ordinary allowance is roomy on purpose — the
editor recompiles on a 250 ms debounce and the terminal polls every 170 ms.

### Notes

- **No authentication, by design.** Anyone can compile and run Savvy. What they
  cannot do is escape the VM: a genome is interpreted, never executed natively,
  with no syscalls, no filesystem and no network — its whole contact with the
  world is `input()`, the output stream, and the genomes it emits. If you want
  it gated, Cloudflare Access or basic auth at the proxy needs no change here.
- **Sessions are owned.** Run ids are sequential, so the server checks the `sid`
  cookie on every poll, input and kill; guessing an id gets `{"found":false}`.
- **Projects are per-browser** — worth saying on the site. Clearing site data
  loses them and they do not follow anyone to another machine; *Export
  workspace* and *Download project* are the way out.

## Layout of the source

| | |
|---|---|
| `src/App.svelte` | shell: theme, global context menu |
| `src/pages/Workbench.svelte` | the IDE proper — panes, tabs, commands, keybindings |
| `src/lib/CodeEditor.svelte`, `cm.js`, `search.js` | the editor and its CodeMirror setup |
| `src/lib/Explorer.svelte`, `project.svelte.js` | projects, files, folders, persistence |
| `src/lib/RightDock.svelte`, `GeneMap.svelte`, `genes.js` | compiler, inspector, debugger |
| `src/lib/Terminal.svelte` | the console |
| `src/lib/layout.svelte.js` | the split/dock tree |
| `src/lib/Palette.svelte`, `Menu.svelte`, `Modal.svelte`, `Settings.svelte`, `Icon.svelte`, `Pane.svelte` | chrome |
| `src/store.svelte.js` | the API client, the ISA and examples |
| `src/app.css` | tokens: colours, type, both themes |
