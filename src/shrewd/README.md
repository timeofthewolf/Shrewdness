# Shrewd

The genome language: linear, Turing-complete, and robust to arbitrary editing.
A program is a flat list of integers, and *every* list of integers is a program.

Write it with [Savvy](../savvy/README.md), which compiles to it. Run things
with `shrewdc` or in the [Shrewdness IDE](../../web/README.md) (see the
[root README](../../README.md) for both).

## The central constraint

"C-like" and "every mutation still runs" pull in opposite directions. C syntax is
brittle by design: one stray character and the parse fails. A language whose
programs get randomly edited can't have a parse step at all, because a parse step
is a thing that can fail.

So the two requirements are met in different places:

- **The genome is a flat `std::vector<int32_t>`.** No nesting, no delimiters, no
  parse.
- **Legibility lives in Savvy**, which compiles *to* genomes, and in the
  decompiler, which renders any genome back *as* Savvy.

Machines operate on the flat form and humans read the source form. Nothing can
fail to parse because nothing is ever parsed.

```
[1, 72, 41, 1, 101, 41, 1, 108, 41, 1, 108, 41, 1, 111, 41, ...]   ->   "Hello, World"
```

The IDE's genome inspector is the same list with each gene tinted by instruction
family, which is a quicker way to see the shape of one than reading the numbers:

![The genome inspector: a coloured strip of genes with a family legend, and the
flat disassembly below it](../../docs/genome.png)

## Why any gene sequence runs

Two guarantees, each closing a way a program could fail to be a program.

| # | Guarantee | Where | How |
|---|-----------|-------|-----|
| 1 | Every gene is an instruction | `decode()` in `isa.hpp` | `op = gene mod 56`, so there is no invalid opcode and no invalid genome |
| 2 | Every instruction is defined in every state | `VM::run()` in `vm.cpp` | underflow reads 0; `/0` and `%0` are 0; every index wraps; arithmetic wraps through unsigned; `INT64_MIN / -1` is special-cased |

Malformed control flow is given a meaning rather than an error
(`build_control_map`): an unmatched `ELSE`/`END`/`BREAK` is inert, and a block
left open at the end of the genome closes at the end of the genome. Block
structure is well defined for *every* gene sequence.

Guarantee 2 isn't paranoia. Signed overflow and `INT64_MIN / -1` are undefined
behaviour in C++, and `x / 0` raises `SIGFPE` on x86. A garbled genome that
stumbles into one has to produce a boring number rather than take the process
down.

**Termination is deliberately not on that list.** There used to be a third
guarantee, a gas cap that bounded every run, and it was wrong. Deciding that a
program gets ten thousand instructions is a *policy*, and policy belongs to
whoever's paying for the CPU, not to the language. So:

- `while (1 == 1)` runs forever.
- Recursion recurses until the host runs out of memory.
- `VM::run()` on a looping genome never returns unless a caller says otherwise.

Every field of `Limits` defaults to zero, meaning unlimited, so a caller that
runs arbitrary genomes has to bring its own budget. `shrewd_demo` and the IDE
backend both do exactly that. C doesn't stop you writing `for (;;)` and neither
does this.

What survives is the property everything else rests on: **nothing ever has to ask
whether a genome is valid.** A genome may run forever, but it can't be malformed
and it can't crash.

## The instruction set

56 opcodes. One gene each, except `PUSHI`, which reads the next gene as a
literal.

```
NOP
PUSHI PUSH0 PUSH1 PUSH2 PUSH10 PUSHN1     constants (PUSHI takes the next gene)
POP DUP SWAP OVER                         stack
ADD SUB MUL DIV MOD NEG INC DEC           arithmetic (total)
LT GT EQ NEQ NOT AND OR                   comparison and logic
LD0..LD3 ST0..ST3                         four registers
MLOAD MSTORE MSIZE                        the tape (indices wrap; MSIZE asks how big)
IF ELSE DO END BREAK                      structured control flow
PROC CALL RET                             subroutines, called by index
OUT OUTNUM IN RAND                        the window onto the world
GLEN GREAD EMIT CLEN CREAD CWRITE SPAWN   reproduction
```

**The list lives in one place.** `SHREWD_OP_LIST` in `isa.hpp` generates the
enum, the mnemonic table is `static_assert`-checked against it, and the
interpreter's dispatch table is generated from it — so the three cannot drift
apart. Adding an opcode means extending the list and the table; everything else
follows, or refuses to compile. (A hand-maintained `switch` already cost one
working replicator; see the findings.)

**The enum ordering is load-bearing.** Since `op = gene mod 56`, a ±1 change to
a gene lands on the *neighbouring* opcode. Keeping related instructions adjacent
(`ADD` beside `SUB`, `LT` beside `GT`) means a small edit usually causes a small
behavioural change, so the space of genomes has gradients in it instead of
cliffs everywhere. Appending new opcodes is safe. Reordering changes what every
existing genome means.

**Nothing in Shrewd refers to a position.** Blocks are matched, and procedures
are called by *index*: `CALL` enters the n-th `PROC` in the genome, modulo how
many exist. That's what makes the language relocatable. An edit that inserts,
deletes or duplicates genes shifts everything after it, so anything holding an
address is holding a number that's about to be wrong. This one was measured
rather than assumed; see the findings below.

**Loops are `do`/`while`, not `while`.** On a linear stack machine a pre-test
`while` can't work in two tokens, because the condition code sits *before* the
loop start and jumping back would never re-evaluate it. Having `END` pop the
condition puts the condition code *inside* the loop instead, and `IF` + `BREAK`
covers the rest. Pleasant accident: an empty stack pops 0, so a stray `END`
exits its loop rather than spinning.

**The return stack is separate from the data stack.** Arguments and return
values ride the data stack; return addresses do not. Neither can clobber the
other, which is what makes recursion work without a frame-pointer dance.

**`PROC ... END` is a definition, not a statement.** Reaching one in normal flow
skips the block. So a compiler can simply put procedures after the main code and
let execution walk past them — no jump over them, and no `HALT` to invent.

**Turing-complete**, and not by assertion. `examples/brainfuck.savvy` is a
Brainfuck interpreter that compiles to Shrewd and runs. Brainfuck is
Turing-complete, so anything Brainfuck computes, Shrewd computes. The unbounded
looping is real rather than notional, since there's no step cap unless a caller
sets one. The tape is finite because indices have to wrap into something, which
is the same sense in which any real computer is a finite-state machine.

### Opcode reference

`a`, `b` are the top two data-stack values (`b` on top); all of these pop what
they name and push what they promise. Every instruction is total: the "empty /
absent" column is what happens in the states other ISAs call invalid.

| op | does | when underflowing / absent |
|---|---|---|
| `NOP` | nothing | — |
| `PUSHI k` | push the next gene as a literal | at the genome's end, pushes 0 |
| `PUSH0/1/2/10/N1` | push that constant | — |
| `POP` | discard the top value | discards nothing |
| `DUP` | duplicate the top value | pushes two 0s |
| `SWAP` | exchange the top two | operates on 0s |
| `OVER` | copy the second value to the top | operates on 0s |
| `ADD SUB MUL` | `a ∘ b`, wrapping through unsigned | operands read as 0 |
| `DIV MOD` | `a / b`, `a % b`; **0 when `b == 0`**; `INT64_MIN ∘ -1` defined | operands read as 0 |
| `NEG INC DEC` | `-a`, `a+1`, `a-1`, wrapping | operand reads as 0 |
| `LT GT EQ NEQ` | comparison, pushes 0 or 1 | operands read as 0 |
| `NOT` | `a == 0`, pushes 0 or 1 | pushes 1 |
| `AND OR` | logical on both operands (no short-circuit), 0 or 1 | operands read as 0 |
| `LD0..LD3` | push register 0..3 | registers start each run at 0 |
| `ST0..ST3` | pop into register 0..3 | stores 0 |
| `MLOAD` | pop `i`, push `mem[i]` | index wraps into the tape |
| `MSTORE` | pop `v`, pop `i`, `mem[i] = v` | index wraps into the tape |
| `MSIZE` | push the tape size in cells | — |
| `IF` | pop; skip to `ELSE`/`END` when 0 | unclosed block ends at the genome's end |
| `ELSE` | jump to the block's `END` | unmatched: inert |
| `DO` | mark a loop start | — |
| `END` | close a block; for `DO`: pop, loop back while non-zero | unmatched: inert (pops nothing) |
| `BREAK` | jump past the innermost `DO`'s `END` | no enclosing loop: inert |
| `PROC` | begin a procedure; normal flow skips the block | unclosed: body runs to the genome's end |
| `CALL` | pop `k`, enter the k-th `PROC` (mod how many exist) | no `PROC`s: the pop still happens, nothing else |
| `RET` | return to the caller | not inside a call: halt |
| `OUT` | pop, emit low byte as a character | — |
| `OUTNUM` | pop, emit as decimal + one space | — |
| `IN` | push the next input value, or **-1 at end of input** | — |
| `RAND` | push a value in `[0, 2^31)` from the run's seed | — |
| `GLEN` | push my genome's length | — |
| `GREAD` | pop `i`, push my own gene at `i` (raw, undecoded) | index wraps into the genome |
| `EMIT` | pop, append to the child genome | beyond `max_child_genes`: dropped |
| `CLEN` | push the child's length so far | — |
| `CREAD` | pop `i`, push child gene at `i` | empty child: 0 |
| `CWRITE` | pop `v`, pop `i`, overwrite child gene | empty child: inert; index wraps |
| `SPAWN` | commit the child as offspring, start a new one | beyond `max_offspring`: nothing moves |

Two details worth pinning down: an `END` that closes a `PROC` returns to the
caller, and if there is no caller it simply continues — reachable only in
tangled mutants, still defined. And `GREAD` returns the *raw* gene, not its
canonical opcode value: a genome is data to itself, so 200 and 44 (both decode
to `RAND`) are different genes even though they are the same instruction.

## Resources belong to the caller

`Limits` is how a caller says what a program may spend. Every field defaults to
zero, meaning unlimited:

| | |
|---|---|
| `max_steps` | CPU |
| `stack_limit` | data stack depth |
| `max_call_depth` | recursion depth |
| `output_limit` | how much it may say |
| `max_child_genes`, `max_offspring` | how much it may reproduce |
| `memory_size` | RAM — the one that cannot be unlimited |

The tape has to have a size, because every index wraps into it, so "unlimited"
means nothing there. It's a *resource* instead: the caller decides how much RAM a
program gets (default 65,536 cells), and the program asks with `MSIZE`
(`mem.length` in Savvy) rather than assuming. Compiled Savvy relies on this. It
lays its call frames out from whatever `MSIZE` reports, so the same genome runs
correctly on a 300-cell tape or a 50,000-cell one.

Nothing faults at the cap. A push past `stack_limit` is dropped, an `EMIT` past
`max_child_genes` is dropped, a `CALL` past `max_call_depth` declines to call, a
`SPAWN` past `max_offspring` commits nothing. Only `max_steps` halts the run, and
then `Result::halt` says `OutOfGas` instead of `Completed`.

Two consequences worth being explicit about:

- **An unbudgeted caller can hang.** `VM::run()` on `while (1 == 1)` never
  returns. That's the intended behaviour, and it means a caller running untrusted
  genomes has to impose a budget or run them somewhere it can kill them. The IDE
  backend does the latter: each console session runs on its own thread with a
  stop button wired to it.
- **Unlimited memory can take the host down.** With no `stack_limit`, a genome
  that pushes forever grows the stack until the allocator gives up, and that
  kills the process rather than just the run. A step budget bounds it implicitly,
  since a step can only allocate so much, and that's the cheapest way to stay
  safe.

## How the interpreter works

`VM::run()` does two things: build a *plan* from the genome (one pass), then
execute against it. Nothing else is ever computed about a genome.

### The plan: `ControlMap`

`build_control_map_into()` makes a single O(genes) pass and produces three
arrays:

- **`ops`** — the *dispatch plan*: every gene decoded (`gene mod 56`) to a
  byte, then legalised. Control ops that resolved to "do nothing" — a stray
  `ELSE`/`BREAK`/`END`, every `DO` (loops are driven from their `END`) — are
  rewritten to `NOP`, and `END` splits into synthetic loop-back and
  proc-return forms. The execution loop therefore never decodes, and never
  re-checks what the matcher already knows: a dispatched `IF`/`ELSE`/`BREAK`/
  `PROC` *always* has a valid jump target. The decode sweep is a separate
  loop so the compiler vectorises the modulo; the matcher then only touches
  the ~1-in-9 genes that are control ops, behind a single range compare.
- **`jump`** — for every block instruction, where it sends the program counter:
  `IF`→ after `ELSE`/`END` when false, `ELSE` → after `END`, `END` of a `DO` →
  back to the `DO`, `BREAK` → past the loop's `END`, `PROC` → past its `END`
  (and that `END` is marked "return to caller").
- **`procs`** — where each `PROC` body starts, in genome order. `CALL` is an
  index into this table, which is why calls survive relocation.

Block matching uses an explicit stack of open blocks and a flat list of pending
`BREAK`s. Anything unmatched is left as "no jump" in `jump[]` (and becomes a
`NOP` in the plan); anything unclosed is closed at the genome's end. This is
where "malformed control flow has a meaning" is implemented. `jump[]` keeps
the documented meaning for external readers (the assembler and decompiler use
it); only `ops[]` is legalised.

The pass allocates nothing in steady state: the arrays and the matcher's scratch
live in the `ControlMap` and are reused run to run.

### Dispatch

The execution loop is token-threaded: each opcode body ends by loading the next
opcode and jumping *directly* to its body through a computed goto (a GNU
extension), so every opcode gets its own indirect branch and the branch
predictor learns each opcode's habitual successor. A plain `switch` remains as
the portable fallback (`-DSHREWD_NO_COMPUTED_GOTO`), compiles the very same
opcode bodies, and is the reference for what dispatch means. Threading is worth
a further ~10% on loop-heavy code over the predecoded switch — most of the
speed lives in the predecode, not the goto.

The opcode bodies are written once, wrapped in two macros (`VM_CASE`/`VM_NEXT`)
that expand to labels or `case`s. The dispatch table is generated from
`SHREWD_OP_LIST`, and the switch build proves exhaustiveness with `-Wswitch` —
between them, an opcode cannot be forgotten in either mode.

Per step, the interpreter pays exactly one predictable branch: the step budget
(the `OutOfGas` halt). Falling off the end costs no check at all — the
predecoded plan is padded with two *halt pseudo-ops* after the last gene (two,
because a trailing `PUSHI` advances the pc by 2), so "the genome is over"
dispatches through the same table as any instruction. The legalised plan
(above) extends the same idea to control flow: two more synthetic opcodes
split `END` into its loop-back and proc-return forms, strays arrive as `NOP`,
and no dispatched control op ever asks whether its jump target exists. Every
jump the control map can produce lands at or before the halt padding, which
the demo's 700k-genome battering exists to keep true.

Stack ops take the same attitude: a binary op with both operands present
writes its result where the deeper operand was — one bounds check instead of
pop-pop-push's three — and the underflow-reads-0 path is the cold branch, not
a tax on the common case.

### State

A run touches five pieces of state, all owned by the VM and reused across runs:

- **The data stack** — a vector. Underflow reads 0; a push past `stack_limit`
  is dropped; otherwise it grows as needed.
- **The return stack** — separate, holding only return addresses. `CALL`
  pushes, `RET` and a `PROC`'s `END` pop.
- **Four registers** — zeroed each run, returned in `Result::registers`.
- **The tape** — see below.
- **The child genome** — what `EMIT`/`CWRITE` build and `SPAWN` commits.

### Each run sweeps up after itself

The tape starts every run genuinely zero, but nobody ever wipes all of it:
`MSTORE` records each cell's *first* touch of the run on a dirty list (a
per-cell run stamp dedupes repeat stores), and the end of the run writes zero
back to exactly those cells — while their cache lines are still hot from the
run's own stores. `MLOAD` is therefore a plain array read, with no freshness
check in the hottest instruction the compiler emits, and handing a program a
generous tape costs nothing until it actually touches cells. When the 32-bit
run counter would wrap (once every 4 billion runs) the stamps are wiped once
and the counter restarts; correctness does not depend on the wrap never
happening. The tape only reallocates when `memory_size` changes between runs.

Indices into the tape (and the genome, and the child) wrap by modulo, with a
fast path: in-range indices — the overwhelmingly common case — cost one
unsigned compare, and only a genuinely wild index pays for a division.

### Randomness, arithmetic, I/O

- `RAND` is splitmix64 over a counter seeded by the run's `seed` argument —
  fast, seedable, and *per run*, so `run(genome, input, seed)` is a pure
  function. Reproducible runs matter too much to give up for ambient entropy.
- The wrapping/total arithmetic lives in `arith.hpp`, shared with the Savvy
  compiler's constant folder — a value folded at compile time is bit-identical
  to the same expression executed at run time. The demo verifies this
  exhaustively over the operators' edge cases.
- I/O goes through the two-method `Io` interface (`read`/`put`). The
  input-vector overloads use an internal replay io that borrows the input and
  writes output straight into the `Result` — the batch path, used whenever a
  run's input is known up front; `BufferedIo` is the same thing as a public
  class; `StreamIo` talks to real streams and is what `shrewdc run` uses. `StreamIo` flushes before every read (so prompts
  appear before the program blocks) and on every newline (so a program that
  loops forever still shows its output — it will never reach an end-of-run
  flush).

### What a run costs

Three O(genes) array passes to build the plan, then O(steps) to execute, plus
a sweep over the cells the run dirtied. Through `run_into()` the steady state
performs **zero heap allocations, full stop**: the plan, stacks, tape, child
buffer and matcher scratch live in the VM, the caller's `Result` buffers
(output, stack, offspring — including the offspring genomes' own buffers,
which cycle through a small pool) are recycled, the input is borrowed rather
than copied, and output is written straight into the caller's string. A
24-step genome runs in ~0.09 µs, control-map build included.

## Using it from C++

Everything is in `namespace shrewd`, umbrella header `shrewd/shrewd.hpp`.

```cpp
#include "shrewd/shrewd.hpp"

// A budget: this caller runs arbitrary genomes, so it brings one. Zero means
// unlimited, and Limits{} is all zeros -- fine for code you trust to halt.
shrewd::Limits budget;
budget.max_steps       = 10'000;
budget.stack_limit     = 1'000'000;
budget.output_limit    = 4096;
budget.max_child_genes = 100'000;
budget.max_offspring   = 8;
budget.memory_size     = 1u << 16;      // the program's RAM

shrewd::VM vm(budget);                   // reusable; one per thread

shrewd::Genome g = {1, 72, 41, 1, 105, 41};          // any ints whatsoever
shrewd::Result r = vm.run(g, /*input=*/{}, /*seed=*/42);

r.output;                  // what it printed        ("Hi")
r.steps;                   // real CPU spent, measured not estimated
r.halt;                    // Completed or OutOfGas
r.offspring;               // genomes it committed with SPAWN
r.uncommitted_child_genes; // it was building a child but never spawned it
r.registers, r.stack;      // final machine state, for calculator-style use
r.inputs_read;             // how much input it consumed
```

The two-argument overload takes any `Io&` instead of an input vector — pass a
`StreamIo` for a live terminal, or your own implementation (the IDE backend's
console sessions do exactly this, blocking on a queue fed over HTTP).

Code that runs many genomes should prefer `run_into`, which recycles the
buffers already inside the `Result` it is handed — in steady state one
evaluation allocates nothing at all:

```cpp
shrewd::Result r;                        // reused across the whole batch
for (const auto &genome : genomes) {
    vm.run_into(r, genome, {}, seed++);
    scores.push_back(score(r));
}
```

Contracts worth knowing:

- **A run is a pure function of `(genome, input, seed)`.** Any run can be
  replayed exactly; the demo checks this over 20,000 random genomes.
- **A `VM` is reusable but not shareable.** It owns scratch buffers (that is
  why short runs are fast), so give each thread its own VM. Two VMs never
  share state.
- **`run()` may never return** if the genome loops and the budget is
  unlimited. That is the documented deal; see "Resources belong to the
  caller".

Text formats, for files and inspection (`asm.hpp`):

- `to_assembly(g)` / `assemble(src)` — mnemonic form, indented by block
  structure. Bitwise round-trip: non-canonical genes (200 encoding `RAND`) are
  written as numbers, so `assemble(to_assembly(g)) == g` gene for gene.
- `write_genes(g)` / `read_genes(src)` — the plain-number `.shrewd` file
  format.
- `to_gene_list(g)` — one-line `[1, 72, 41, ...]` form.

Mutation scaffolding (`genome.hpp`): `random_genome()`, `mutate()` (point
substitutions with a ±1 "nudge" bias, insertion, deletion, duplication,
inversion, an optional length cap) and `crossover()`. These exist to generate
test genomes and to measure the language's robustness — they are what
`shrewd_demo` batters the VM with. A genome can also edit *itself*; see
Self-replication below.

File map:

| | |
|---|---|
| `isa.hpp/.cpp` | genes, values, the op list, `decode`, per-op documentation table |
| `arith.hpp` | the total arithmetic, shared by VM and compiler |
| `vm.hpp/.cpp` | `Limits`, `Result`, `ControlMap`, the interpreter |
| `io.hpp/.cpp` | `Io`, `BufferedIo`, `StreamIo`, end-of-input convention |
| `genome.hpp/.cpp` | `Genome`, `random_genome`, `mutate`, `crossover` |
| `asm.hpp/.cpp` | assembly and gene-list text formats |

## Speed

Anything that sweeps a large space of genomes runs millions of short programs,
so per-run overhead matters as much as peak throughput. `shrewd_bench` measures
both on fixed workloads; numbers from a Ryzen 7 5800X, GCC 16.1.1 `-O3`:

| workload | per run | per step |
|---|---|---|
| whole 24-step run (per-run overhead) | 0.14 µs | 5.8 ns |
| tight counting loop | — | 2.4 ns |
| recursive fib (call-heavy) | — | 2.7 ns |
| self-replication (GREAD/EMIT loop) | 1.45 µs | 2.6 ns |
| Brainfuck interpreter (mixed) | 0.47 ms | 2.5 ns |
| 20k random genomes, 10k-step budget | 2.0 µs | 3.6 ns |

That's 280–420 million instructions per second per core: around half a million
budgeted genomes per second, or 700,000 replications.

The optimisation work below was measured on the same machine under GCC 15, where
it took the 24-step run from 0.30 µs to 0.09 µs, the counting loop from 5.8 to
1.5 ns/step, fib from 5.6 to 1.9, Brainfuck from 6.2 to 1.9, and the
20k-random-genome sweep from 4.1 to 1.7 µs/run. Between 2.4× and 3.9× across the
board. The table above is the same code rebuilt with GCC 16.1.1, which gives back
roughly a third of that. The ordering of what mattered hasn't changed, but
**benchmark your own toolchain rather than trusting either column.** What got it
there, in order of effect:

- **Decode once, not per step.** The control-map pass predecodes every gene
  into a byte array; the old loop paid a modulo *and* an out-of-line table
  call (for the instruction's width) on every step.
- **Buffers outlive runs.** Tape, stacks, child, control map, matcher scratch —
  all reused; `run_into` extends the same discipline to the caller's `Result`,
  so the steady state of a run-many-genomes loop allocates nothing at all (see
  "What a run costs").
- **Reads are plain loads.** The tape starts each run zero because each run
  sweeps up its own writes (dirty list, while the lines are hot) — so `MLOAD`
  carries no freshness check. Compiled Savvy keeps locals in memory, which
  makes this the hottest instruction there is.
- **One branch per step, not two.** Halt pseudo-ops pad the predecoded plan,
  so end-of-genome is dispatched, not checked; only the gas test remains in
  the step path (see Dispatch).
- **The plan is legalised at build time.** Stray control ops dispatch as
  `NOP`, `END` arrives pre-split into its loop/return forms, and no handler
  re-validates a jump target the matcher already proved. The matcher itself
  hides behind one range compare per gene, so plan-building costs a few ns per
  gene and a whole random 100-gene genome well under a microsecond to set up.
- **Stack ops work in place.** Binary ops rewrite the deeper operand's slot;
  `INC`/`NOT`/`MLOAD`/`GREAD` rewrite the top, `DUP`/`SWAP`/`OVER` touch only
  the slots they mean. One bounds check per op instead of three, underflow
  semantics preserved on the cold path.
- **Token-threaded dispatch** — worth ~10–15% on loop-heavy code beyond the
  predecoded switch, measured against the fallback build.
- **Index wrapping got a fast path** — in-range indices (every well-behaved
  `MLOAD`/`MSTORE`, i.e. all compiled Savvy variable traffic) cost one compare
  instead of a 64-bit division.
- **Code layout is pinned** (`-falign-functions/-labels/-jumps`). Before that,
  recompiling *unrelated* code moved the interpreter's labels and swung the
  benchmarks ±25%; no change smaller than that could even be measured.

Optimisations tried, measured and **rejected**. They're recorded here because
each of them looked like an obvious win:

- *Raw-pointer stacks* (no size write-back per push): lost 15–50%. Six more
  pointers pinned live in a huge function beat the register allocator.
- *Direct threading* (a plan of label addresses instead of opcode bytes, one
  load fewer per dispatch): lost 5–15%. An 8×-larger plan plus a per-run resolve
  pass beats one L1 hit that out-of-order execution was hiding anyway.
- *Devirtualising I/O* by templating the interpreter over the io type: the
  inlined string-append machinery bloated the loop body and lost more on
  compute-heavy code than two virtual calls per printed character cost.
- *Link-time optimisation*: lost 15–30% across the board, because cross-module
  inlining re-lays-out the interpreter and undoes the alignment pinning.
  `-march=native` measured as pure noise on this integer workload.

Benchmark before believing any of it.

To re-measure: `./build/shrewd_bench`. To compare dispatch strategies, build
with `-DSHREWD_NO_COMPUTED_GOTO`. To tune for one machine, configure with
`-DSHREWD_NATIVE=ON`.

The cost of the reuse design is that a `VM` carries scratch state and must not
be shared between threads. Give each thread its own; results are still a pure
function of `(genome, input, seed)`.

## Self-replication

A genome can write genomes, including its own. It reads its own genes
(`GLEN`/`GREAD`), builds a child gene by gene (`EMIT`), may revise what it built
(`CLEN`/`CREAD`/`CWRITE`), and commits it (`SPAWN`). A run returns whatever it
committed in `Result::offspring`; what the caller does with those genomes is the
caller's business, not the VM's.

No quine trick is involved. `GREAD` reads the running genome directly, including
the genes of the copy loop itself. `examples/replicator.savvy` is a complete
self-replicator in four lines of Savvy, and `examples/mutator.savvy` is the same
loop perturbing what it copies.

The design choice worth naming is that **the copying loop lives in the genome,
not in the VM.** How faithfully a genome copies itself, and what it changes when
it doesn't, is written in the program, where a program can change it. An external
`mutate()` is a fixed policy applied from outside. This isn't.

`mutate()` and `crossover()` in `genome.hpp` remain as tools for generating test
genomes and measuring the language. They're scaffolding, not the mechanism.

**`RAND` is seeded per run, not ambient.** `VM::run(genome, input, seed)` stays a
pure function, so the same triple always reproduces a run exactly, while the
program still sees noise. Reproducibility matters too much to trade away for a
source of randomness.

## Build and run

```sh
cmake -B build               # Release by default -- the VM is the product
cmake --build build -j
./build/shrewd_demo          # the test suite
./build/shrewd_bench         # the benchmarks
./build/shrewdc --help       # the toolchain
```

For the sanitizer build the suite is verified under:

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j && ./build-asan/shrewd_demo
```

`shrewd_demo` checks the seed prints `Hello, World`; that the compiled calculator
evaluates with correct precedence *and parentheses*; that hanoi(4) makes exactly
15 moves, fib(60) is exact, and the Brainfuck interpreter prints Hello World;
that the REPL reads text and answers it; that the replicator's child is a
gene-for-gene copy *and is itself fertile*; that nothing in the language stops
`while (1 == 1)` but a budget does; then runs **200,000 random genomes**
and **100,000 mutants across 2,000 lineages**, walks the *entire* one-mutation
neighbourhood of the seed, verifies constant folding against the VM's own
arithmetic, and verifies reproducibility and exact round-tripping. It is clean
under `-fsanitize=address,undefined`.

## What running it actually taught us

Findings from the demo. Several of the design rules above are written the way
they are because of these.

**Turing-complete does not mean robust to editing, and the gap was nearly
fatal.** When `CALL` took an absolute address the language could still compute
anything — the Brainfuck interpreter proved that — while being unable to
*survive* an edit anywhere near a subroutine. The measurement: insert a `NOP`, an
instruction that does nothing, at every position of a working genome and count
how often the output is unchanged.

| genome | `CALL` by address | `CALL` by index |
|---|---|---|
| replicator (no calls) | 100% | 100% |
| fizzbuzz (no calls) | 86% | 86% |
| calculator (calls) | **23%** | **96%** |
| hanoi (calls) | **28%** | **85%** |

An insertion shifts every later gene, so every absolute target became wrong at
once, and a do-nothing edit was lethal three times out of four. Subroutines are
how functionality gets *reused* instead of duplicated inline, so this quietly
capped how complex a machine-written genome could ever get. Calling procedures by
index made position irrelevant and cost nothing, since the procedure table is
built by the prescan and a call is still O(1). `shrewd_demo` section 2d now
measures this every run, because it's exactly the kind of regression that comes
back silently.

The general lesson: **any mechanism that names a place by where it is will be
destroyed by the edit operators.** Name things structurally.

**Self-inspection made gene values observable, and quietly broke an
equivalence.** A gene of 200 and a gene of 44 both decode to `RAND`, so the
disassembler used to canonicalise one to the other for free. `GREAD` ended that.
The genome is now data to itself, and `self[i]` returns the raw number. The
round-trip test caught it, and `to_assembly` is now bitwise-exact, writing
non-canonical genes as numbers with the mnemonic demoted to a comment. Expect
more of this: anything that rewrites a genome "equivalently" now has to mean
*bitwise*.

**Neutral edits are rare in a dense genome — exactly 72 of 8,028 (0.9%),** and
the number is fully explained. The seed has 24 opcode genes, and 224 = 4 × 56, so
each has exactly 3 redundant encodings. 24 × 3 = 72. Where every gene does work,
*modulo redundancy is the only source of neutrality*. Slack has to come from
somewhere else, like unreachable genes or duplicated regions, before an edit can
be free.

**A `default:` label in a switch cost a working replicator.** Adding `puts`
produced a `-Wswitch` warning about an unhandled enum case; "fixing" it with
`default:` silenced the warning *and* swallowed `Emit`, so the compiler stopped
emitting the `EMIT` opcode and `replicator.savvy` shipped a copy loop that copied
nothing. The demo caught it because it checks the child gene-for-gene rather than
checking that a child exists — `spawn()` still reported one offspring, an empty
one. Switches over the opcode and builtin enums now list every case, the
interpreter's dispatch table is generated from the single op list, and the
compiler is left free to complain.

**A search over genomes must charge for length.** With no length cost the demo's
hill-climb bloated to thousands of junk genes while executing 13 instructions. An
early jump skipped the junk, so it burned no CPU and the score never saw it.

**But a flat per-gene charge is fatal.** It collapsed the same search to a single
gene. Growth is then strictly worse at *every* step, so nothing can ever afford
the two or three neutral insertions needed to reach the next working instruction.
What works is a free allowance with a steep charge beyond it; `shrewd_demo`
section 10 uses 96 free genes, then 0.02 per gene. Expect that shape wherever a
resource is charged.

**A score must match the edit operators.** Scoring output position-by-position
made a single insertion catastrophic, even for a genome one gene away from
perfect. Since insertions and deletions are first-class, edit distance is the
metric that fits. An earlier attempt also rewarded output *length*, which one
`OUTNUM` gets for free, and the search found that local optimum immediately and
sat in it for 2,500 generations. Cheap proxies get gamed fast.

**The gradient is real, and greedy climbing plateaus.** A `(1+λ)` hill-climb from
a random genome halves the edit distance to `Hello, World` and then stalls
(section 10 runs it every time). Small edits really do produce small changes in
behaviour, which is the property the ISA ordering was designed for, but a greedy
climber on its own doesn't get all the way there.

**Self-editing genomes can break their own copy loop, and that's the point.** Of
200 seeds of `mutator.savvy`, 184 produced a modified child and 136 of those
could still reproduce. When the copy loop is data like everything else, a genome
can damage its own ability to copy itself. That's the direct consequence of
putting the loop in the genome rather than the VM.

## Interfaces for embedding

- `Result::steps` — real CPU burned, measured not estimated.
- `Result::offspring` — the genomes a run committed. Also
  `uncommitted_child_genes`, which distinguishes "started building a genome and
  didn't finish" from "never tried".
- `Limits` — per-run CPU, RAM, stack and reproduction quotas. A caller handing
  out resources hands out a `Limits`.
- `VM::run()` is pure in (genome, input, seed) and holds no state between runs,
  so genomes can be evaluated in parallel (one VM per thread) and any run can be
  replayed exactly.
- `Op::IN` / `Result::output` — the I/O channel. Or implement `Io` yourself for
  anything live; the IDE backend does.

Known cost, not yet paid down: the plan (`ControlMap`) is rebuilt per run —
O(genes), three flat passes, allocation-free, but still work. A caller that runs
the *same* genome many times would win by caching the plan per genome.
