# Shrewdness

Two languages and an IDE for them.

![The Shrewdness IDE: Savvy on the left, the assembly it compiles to and a live
console on the right](docs/ide.png)

**Shrewd** is a Turing-complete language whose programs are flat lists of
integers. Any list of integers is a valid program. There's no parse step, no
invalid opcode, nothing that can fault, so editing a program at random gives you
different behaviour instead of a syntax error.
→ [`src/shrewd/README.md`](src/shrewd/README.md)

**Savvy** is the language you actually write: small, C-like, one type, and it
compiles to Shrewd exactly. It also goes the other way. Hand `shrewdc savvy` any
list of integers at all and you get Savvy back that compiles and runs.
→ [`src/savvy/README.md`](src/savvy/README.md)

**Shrewdness** is the IDE. A browser front end over a small C++ backend, where
you edit Savvy, watch it turn into assembly and genes as you type, run it
against a live terminal, and step through it one instruction at a time.
→ [`web/README.md`](web/README.md)

```
   you write             compiles to               the VM runs
  ┌────────┐  compile  ┌──────────────────┐  execute ┌─────────┐
  │ .savvy │ ────────► │ [1,72,45,1,101,…]│ ───────► │ output, │
  └────────┘           │     (genome)     │          │offspring│
                       └──────────────────┘          └─────────┘
                            ▲       │ decompile (any genome -> runnable Savvy)
                            └───────┘
```

## Quick start

```sh
cmake -B build                 # defaults to Release; the VM is the product
cmake --build build -j

./build/shrewd_demo            # the test suite: ~700k genome runs, all checked
./build/shrewd_bench           # VM performance on representative workloads

echo "(12+34)*2" | ./build/shrewdc run examples/calculator.savvy    # => 92
./build/shrewdc run examples/replicator.savvy --trace   # => 1 offspring

./build/shrewdness             # the IDE on http://127.0.0.1:7070/
```

The IDE's front end is a Svelte app. Build it once (`cd web && npm install &&
npm run build`) and `shrewdness` will serve it. Or skip all of the above:

```sh
docker compose up --build      # then open http://localhost:7070/
```

Prebuilt binaries for Linux, macOS and Windows are on the
[releases page](https://github.com/timeofthewolf/Shrewdness/releases).

## The desktop app

`desktop/` wraps the IDE as an Electron application: it starts `shrewdness` on a
free port, waits for it, and puts the workbench in a native window. The backend
is a child process, so it goes away when you close the app.

Drag a tab out of the window and it becomes a window of its own; drag it onto
another window and it moves there instead, closing the window it left if that
was its last tab. Windows share one project — edit a file in either and the
other picks it up — but each keeps its own pane layout, so a window is a view,
not a copy.

It also edits real files. **Open a folder from disk** in the explorer (or pass
one on the command line) and the explorer shows what is actually there: `.savvy`,
`.asm` and `.shrewd` files, real subfolders, with `node_modules`, `.git` and
build directories skipped. Edits are written back to disk a moment after you
stop typing, and changes made outside the IDE appear in it. Without a folder
open, projects live in the browser's local storage exactly as they do on the
web.

```sh
cmake --build build -j                       # the backend it launches
cd web && npm run build && cd ..             # the front end it serves
cd desktop && npm install && npm start
```

Neither the macOS nor the Windows build is signed by a paid developer
certificate, so both operating systems warn on first launch. On macOS,
right-click the app and choose **Open** rather than double-clicking (or run
`xattr -dr com.apple.quarantine /Applications/Shrewdness.app`). On Windows,
SmartScreen offers **More info → Run anyway**. The macOS app is ad-hoc signed so
it runs on Apple Silicon; removing the warning entirely needs a Developer ID and
notarisation.

`npm run dist` packages it: an AppImage on Linux, an installer and a portable
zip on Windows, a dmg on macOS, each with the `shrewdness` binary, `web/` and
`examples/` bundled in. Put the binary for the platform you are packaging in
`desktop/resources/bin/` first; the packager takes it from there rather than
building it. Tagged releases carry all of them alongside the plain archives.

## The `shrewdc` toolchain

```
shrewdc build  <file.savvy> [-o out.shrewd]   compile Savvy to a genome file
shrewdc run    <file.savvy|file.shrewd>       compile if needed, then run
shrewdc asm    <file.savvy|file.shrewd>       show Shrewd assembly
shrewdc savvy  <file.savvy|file.shrewd>       decompile a genome to Savvy
shrewdc genes  <file.savvy|file.shrewd>       show the raw gene list

run options:
  --seed N          seed for rand()  (default 0; a run is reproducible per seed)
  --steps N         stop after N steps (default: no limit -- loops run forever)
  --trace           report steps, halt reason and offspring on stderr
  --offspring DIR   write each committed child to DIR/childN.shrewd
```

`run` wires `input()` and `putchar()` to the real terminal, so interactive
programs like `examples/repl.savvy` prompt and wait the way anything else does.
A program that loops forever will loop forever; that's the point. Kill it like
any other process, or hand it a `--steps` budget.

Programs can also write genomes. `EMIT` and `SPAWN` build a child gene by gene,
and `--offspring` writes each one out. Children are genomes, so they run like
anything else:

```sh
shrewdc run examples/mutator.savvy --seed 1 --offspring gen1
shrewdc run gen1/child0.shrewd     --seed 2 --offspring gen2
diff gen1/child0.shrewd gen2/child0.shrewd   # what one generation changed
```

Programs can span files: `include "lib";` splices `lib.savvy` (resolved relative
to the including file) into the build, once. See the
[Savvy README](src/savvy/README.md#programs-can-span-files).

## The Shrewdness IDE

```sh
./build/shrewdness                       # 127.0.0.1:7070
./build/shrewdness --net --port 8080     # listen on all interfaces
```

An editor with the toolchain wired into the panes beside it. Type Savvy on the
left; the assembly, the gene list and the decompiled form on the right keep up
as you go.

![Typing a Savvy program while the assembly pane recompiles alongside
it](docs/live.gif)

You also get multi-file projects, split panes, a command palette, vim and emacs
keymaps, a terminal that really talks to `input()`, and a step debugger that
replays a run with the stack, registers and memory writes at every step. It
rearranges itself down to a phone rather than dropping features. Full tour in
[`web/README.md`](web/README.md).

The backend holds no database and no accounts. Projects live in your browser's
local storage, and the server just compiles, runs and traces what it's sent. To
put it on a public domain, see [Hosting it](web/README.md#hosting-it).

## Layout

| | |
|---|---|
| `src/shrewd/` | the genome language: ISA, interpreter, assembler, mutation helpers |
| `src/savvy/` | the human language: parser, compiler, decompiler |
| `src/net/` | a minimal blocking HTTP/1.1 server — the IDE backend's transport |
| `src/tools/` | `shrewdc` (CLI), `shrewdness` (IDE backend), `shrewd_demo` (test suite), `shrewd_bench` (benchmarks), `shrewd_fuzz` (differential fuzzer) |
| `web/` | the IDE front end: a Svelte app served by `shrewdness` |
| `examples/` | Savvy programs, each demonstrating one thing → [`examples/README.md`](examples/README.md) |

Two libraries, `shrewd` and then `savvy` on top of it, never the other way
round, plus `net`, which knows about neither. The interpreter has no idea that
Savvy or the IDE exist.

## Design rules the whole project obeys

- **Any genome runs.** No parse step, no invalid opcode, no faulting
  instruction. Edit a genome and you get a different program, never a broken
  one.
- **The language sets no limits.** `while (1 == 1)` runs forever; recursion goes
  as deep as the host allows. Budgets (`shrewd::Limits`) are imposed by the
  *caller*: the test suite, the IDE, or your own embedding. Policy belongs with
  whoever pays for the resources.
- **Nothing names a position.** Blocks are matched, procedures are called by
  index. Anything holding an absolute address is destroyed by the first
  insertion, which is measured rather than assumed (see the Shrewd README).
- **Runs are reproducible.** A run is a pure function of
  `(genome, input, seed)`. If you can't reproduce a result you can't trust it.
- **Text forms round-trip.** `shrewdc asm` is bitwise-exact. `shrewdc savvy`
  always produces something that compiles and runs, and for compiler-shaped
  genomes something that behaves identically.

## Performance

Measured by `shrewd_bench` on a Ryzen 7 5800X, GCC 16.1.1 `-O3` (July 2026):

| workload | per step | throughput |
|---|---|---|
| tight counting loop | 2.4 ns | ~415 Mstep/s |
| recursive fib (call-heavy) | 2.7 ns | ~365 Mstep/s |
| self-replication | 2.6 ns | 1.45 µs per replication |
| Brainfuck interpreter in Savvy | 2.5 ns | ~400 Mstep/s |
| 20k random genomes, 10k-step budget | 3.6 ns | 2.0 µs per genome |

A whole short run (control-map build, execution, result) costs about 0.14 µs, so
one core gets through **roughly half a million budgeted genomes per second**.
Through the buffer-recycling `run_into` API a steady-state evaluation loop
performs zero heap allocations. Runs are independent pure functions, so N cores
give you N× by giving each thread its own `VM`. How it gets there is documented
in the [Shrewd README](src/shrewd/README.md#speed).

## Status

The language, the compiler, the decompiler, the toolchain and the IDE are all
built and working.

`shrewd_demo` runs over 700,000 genomes per invocation: every example compiled
and checked against its expected output, the entire one-mutation neighbourhood
of a seed program, 200,000 random genomes, 100,000 mutants across 2,000
lineages, constant folding verified against the VM's own arithmetic, and
determinism checks. It's clean under `-fsanitize=address,undefined`.

Past the suite, the toolchain has been fuzzed. Every VM optimisation round is
proven observably invisible by hashing full results over 200,000 random genomes
under randomized budgets (both dispatch strategies, before and after), 110,000
generated Savvy programs survive compile → decompile → recompile with their
behaviour intact, and the parser eats arbitrary garbage under ASan and UBSan
without faulting.
