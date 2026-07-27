# Shrewdness

Two languages and an IDE for them.

**Shrewd** is a Turing-complete language whose programs are flat lists of
integers. Any list of integers is a valid program: there is no parse step, no
invalid opcode and no faulting instruction, so editing a program at random
produces different behaviour rather than a syntax error. It is built, tested
and fast. → [`src/shrewd/README.md`](src/shrewd/README.md)

**Savvy** is the language you actually write. Small, C-like, one type, and it
compiles to Shrewd exactly. It also decompiles *back*: hand `shrewdc savvy` any
list of integers at all and it returns Savvy that compiles and runs.
→ [`src/savvy/README.md`](src/savvy/README.md)

**Shrewdness** is the IDE — a browser front end over a small C++ backend, where
you edit Savvy, watch it become assembly and genes as you type, run it against
a live terminal, and step through it instruction by instruction.
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

The IDE's front end is a Svelte app; build it once (`cd web && npm install &&
npm run build`) and `shrewdness` serves it. Or skip all of the above:

```sh
docker compose up --build      # then open http://localhost:7070/
```

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

`run` wires `input()`/`putchar()` to the real terminal, so interactive programs
(`examples/repl.savvy`) prompt and wait like any other program. A program that
loops forever runs forever — that is a feature; kill it like any other process,
or hand it a `--steps` budget.

A program can also write genomes: `EMIT`/`SPAWN` build a child gene by gene, and
`--offspring` writes each one out. Children are genomes, so they run like
anything else:

```sh
shrewdc run examples/mutator.savvy --seed 1 --offspring gen1
shrewdc run gen1/child0.shrewd     --seed 2 --offspring gen2
diff gen1/child0.shrewd gen2/child0.shrewd   # what one generation changed
```

Programs can span files: `include "lib";` splices `lib.savvy` (resolved
relative to the including file) into the build, once — see the
[Savvy README](src/savvy/README.md#programs-can-span-files).

## The Shrewdness IDE

```sh
./build/shrewdness                       # 127.0.0.1:7070
./build/shrewdness --net --port 8080     # listen on all interfaces
```

An editor with the toolchain wired into the panes beside it: type Savvy on the
left and the assembly, the gene list and the decompiled form on the right stay
current as you type. Multi-file projects, split panes, a command palette, vim
and emacs keymaps, an interactive terminal that talks to `input()`, and a
step debugger that replays a run with the stack, registers and memory writes at
each step. Full tour in [`web/README.md`](web/README.md).

The backend holds no database and no accounts — projects live in your browser's
local storage, and the server just compiles, runs and traces what it is sent.
To put it on a public domain, see
[Hosting it](web/README.md#hosting-it).

## Layout

| | |
|---|---|
| `src/shrewd/` | the genome language: ISA, interpreter, assembler, mutation helpers |
| `src/savvy/` | the human language: parser, compiler, decompiler |
| `src/net/` | a minimal blocking HTTP/1.1 server — the IDE backend's transport |
| `src/tools/` | `shrewdc` (CLI), `shrewdness` (IDE backend), `shrewd_demo` (test suite), `shrewd_bench` (benchmarks), `shrewd_fuzz` (differential fuzzer) |
| `web/` | the IDE front end: a Svelte app served by `shrewdness` |
| `examples/` | Savvy programs, each demonstrating one thing → [`examples/README.md`](examples/README.md) |

Two libraries — `shrewd`, then `savvy` on top of it, never the other way — plus
`net`, which knows about neither. The interpreter has no idea Savvy or the IDE
exists.

## Design rules the whole project obeys

- **Any genome runs.** No parse step, no invalid opcode, no faulting
  instruction. Editing a genome produces a different program, never a broken
  one.
- **The language sets no limits.** `while (1 == 1)` runs forever; recursion
  goes as deep as the host allows. Budgets (`shrewd::Limits`) are imposed by
  the *caller* — the test suite, the IDE, or your own embedding. Policy lives
  with whoever pays for the resources.
- **Nothing names a position.** Blocks are matched, procedures are called by
  index. Anything holding an absolute address is destroyed by the first
  insertion — measured, not assumed (see the Shrewd README).
- **Runs are reproducible.** A run is a pure function of
  `(genome, input, seed)`. A result you cannot reproduce is a result you cannot
  trust.
- **Text forms round-trip.** `shrewdc asm` is bitwise-exact; `shrewdc savvy`
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

A whole short run — control-map build, execution, result — costs ~0.14 µs, so
a single core evaluates **around half a million budgeted genomes per second**,
and through the buffer-recycling `run_into` API a steady-state
evaluation loop performs zero heap allocations. Runs are independent pure
functions, so N cores give N× by giving each thread its own `VM`. How it gets
there is documented in the [Shrewd README](src/shrewd/README.md#speed).

## Status

The language, the compiler, the decompiler, the toolchain and the IDE are
built and working.

`shrewd_demo` runs over 700,000 genomes per invocation: every example compiled
and checked against its expected output, the entire one-mutation neighbourhood
of a seed program, 200,000 random genomes, 100,000 mutants across 2,000
lineages, constant folding verified against the VM's own arithmetic, and
determinism checks — clean under `-fsanitize=address,undefined`.

Beyond the suite the toolchain has been fuzzed: every VM optimisation round is
proven observably invisible by hashing full results over 200,000 random genomes
under randomized budgets (both dispatch strategies, before vs after), 110,000
generated Savvy programs survive compile → decompile → recompile with behaviour
intact, and the parser eats arbitrary garbage under ASan/UBSan without fault.
