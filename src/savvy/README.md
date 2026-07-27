# Savvy

The human-readable language that compiles to [Shrewd](../shrewd/README.md).

Shrewd genomes are lists of integers — right for machines, hopeless for
authorship. Savvy is what you actually write.

```sh
shrewdc build examples/calculator.savvy      # -> examples/calculator.shrewd
echo "12+34*2" | shrewdc run examples/calculator.shrewd
80
```

The relationship is deliberately one-way. **Savvy → Shrewd** is a compiler and
is exact. **Shrewd → Savvy** (`shrewdc savvy`) is a decompiler with a guarantee
of its own: whatever the genome — hand-written, machine-generated, random — its
output compiles and runs, and for compiler-shaped genomes it behaves
identically. A genome nobody wrote is under no obligation to look like anything
a person would have written; it is only obliged to be runnable when rendered.

## The shape of it

C-like, one type (a 64-bit signed integer), no declarations beyond `var`.

```savvy
// Print the alphabet.
var c = 'A';
while (c <= 'Z') {
    putchar(c);
    c = c + 1;
}
```

## Values and variables

There is one type. `var` introduces a variable; it lives in a cell of Shrewd's
tape, allocated at compile time and released at the end of its block.

```savvy
var x = 5;
var y;          // zero, always -- cells are recycled, so nothing is inherited
x = x + 1;
```

Literals are decimal (`42`), or character constants (`'A'`, `'\n'`, `'\t'`,
`'\0'`, `'\\'`, `'\''`). A literal must fit in a gene (a signed 32-bit value);
constant *expressions* may exceed it and are computed in 64 bits at run time.

`r0`–`r3` are predeclared and map onto Shrewd's registers. They are faster than
a `var`: one gene and one step against three. Reach for them in a hot loop, or
anywhere steps and genes are the currency.

```savvy
r0 = 0;
while (r0 < 10) { r0 = r0 + 1; }
```

## Arrays and addresses

```savvy
var a[10];          // ten cells, zeroed
a[0] = 5;
var n = a[0];
```

**An array's name is its base address.** So indexing an array and dereferencing
an address are the same operation, and arrays pass to functions without being
copied — exactly as in C:

```savvy
fn sum(xs, n) {
    var total = 0;
    for (var i = 0; i < n; i = i + 1) { total = total + xs[i]; }
    return total;
}

var data[3];
data[0] = 1; data[1] = 2; data[2] = 3;
print(sum(data, 3));        // 6
```

Any expression can be indexed, since indexing is just `mem[base + i]`:

```savvy
var p = data;       // p holds an address
p[1] = 20;          // writes data[1]
```

Array lengths must be literals — the frame has to know how many cells to
reserve. There is no bounds checking: an index off the end reads a neighbouring
variable, and a wild one wraps around the tape. Nothing faults, ever.

`mem` is the whole tape as one array (`mem[i]`), for when you want the raw
thing, and `mem.length` is how many cells the host gave you. Be careful:
`mem[0]` is the stack pointer, and the cells above it hold globals and string
data.

The tape's size is not baked into compiled code — the program asks at runtime, so
the same genome runs on whatever RAM it is handed.

## Strings

A string literal is a zero-terminated run of cells, and evaluates to its address:

```savvy
puts("Hello\n");         // literal: compiles to a putchar per character
var msg = "world";       // msg is an address
puts(msg);               // runtime: loops until the terminator
print(msg[0]);           // 119, 'w'
```

Escapes: `\n` `\t` `\r` `\0` `\\` `\'` `\"`. There is no string *type* — a string
is an address, and that is all.

## Operators

Standard C precedence, loosest first:

| Level | Operators |
|-------|-----------|
| 1 | `\|\|` |
| 2 | `&&` |
| 3 | `==` `!=` |
| 4 | `<` `>` `<=` `>=` |
| 5 | `+` `-` |
| 6 | `*` `/` `%` |
| 7 | unary `-` `!` |

Two things differ from C, both inherited from Shrewd and both harmless:

- **`&&` and `||` do not short-circuit.** They compile to Shrewd's `AND`/`OR`,
  which evaluate both sides. Nothing in Savvy can fault — no null, no
  out-of-range index, no division error — so the only cost is wasted steps.
- **`x / 0` and `x % 0` are `0`,** not a trap. Totality is the point.

Arithmetic is 64-bit and wraps; comparisons yield `0` or `1`.

## Control flow

`if` / `else`, `while`, `do`/`while`, `for`, `break`. No `continue`, no `goto`.

```savvy
if (x > 3) { ... } else if (x > 1) { ... } else { ... }

while (x < 10) { x = x + 1; }

do { x = x + 1; } while (x < 10);

for (var i = 0; i < 10; i = i + 1) { ... }
```

Braces are required. `do`/`while` is the one that maps directly onto hardware —
it is Shrewd's only native loop, and the others are built from it.

`while (1)` and `while (1 == 1)` are the idiomatic infinite loops and compile
to a bare loop with **no per-iteration test at all** (see constant folding
below). Nothing in the language stops them; a caller's budget does
(`--steps` on the CLI, the IDE's stop button), or `break` from inside.

## Functions

```savvy
fn max(a, b) {
    if (a > b) { return a; }   // early return: fine
    return b;
}
```

Functions compile to real `CALL`/`RET` on Shrewd's return stack, and each call
gets a frame carved out of the tape. So:

- **Recursion works**, including mutual recursion.
- **`return` works anywhere** — inside a loop, inside nested blocks, wherever.
- **No forward declarations.** Every signature is registered before any body is
  compiled, so a function can call one defined further down the file.

```savvy
fn fact(n) {
    if (n < 2) { return 1; }
    return n * fact(n - 1);
}
```

A function sees its own parameters, its own locals, and globals — never the
caller's locals. A function without a `return` yields `0`.

`return` also works at top level, where it halts the program — return from
main, exactly as in C. The value is left on the data stack (visible in
`Result::stack`), which suits calculator-style programs.

## Programs can span files

`include "name";` splices another file into the program at the point of the
include — top level only, and exactly once per file, however many routes lead
to it (a diamond of includes, or even a cycle, resolves to one copy with no
fuss). Everything the included file defines — functions, globals, top-level
statements — lands in the including program, in order, exactly as if the text
had been written there.

```savvy
// main.savvy                      // copy.savvy
include "copy";                    fn copy_self(rate) {
copy_self(24);                         for (var i = 0; i < self.length; ...
spawn();                           }
```

Where names come from is the *host's* choice, not the language's: `shrewdc`
resolves them against the including file's directory (appending `.savvy` if
needed), the web workbench resolves them among the project's tabs, and
`savvy::compile` takes a resolver callback, so an embedding can serve sources
from anywhere. Errors keep their file — `copy.savvy:3:7: error: …` names the
file the mistake is actually in.

One program still comes out the other end. Includes exist in Savvy only: the
genome has no idea it was born in five files — mutation edits one flat list
of integers, the same as ever.

Calls compile to an *index*, not an address: `f()` becomes "enter the n-th
procedure in the genome". That is what lets a mutated genome still call what it
meant to — see the Shrewd README, where addresses turned a do-nothing mutation
into a 77% chance of death.

Recursion depth is unlimited by default — the language imposes nothing, so
recursion goes until the host runs out of memory. The *caller* may cap it
(`Limits::max_call_depth`); past the cap `CALL` declines to call rather than
failing, so the program keeps running and gets a wrong answer instead of a
crash. Frames grow down the tape towards the globals and nothing checks for the
collision — deep recursion with large frames will quietly scribble on your
globals. If that matters, ask `mem.length` how much room you have.

## Builtins

Expression builtins (yield a value):

| | |
|---|---|
| `input()` | next input value, `-1` at end of input |
| `rand()` | pseudorandom value in `[0, 2^31)`, from the run's seed |
| `self.length` | length of my own genome |
| `self[i]` | my own gene at `i` |
| `child.length` | genes emitted into the child so far |
| `child[i]` | read back a gene already emitted |
| `mem.length` | how many cells of tape the host gave me |

Statement builtins (yield nothing — `var x = print(1);` is an error):

| | |
|---|---|
| `print(e)` | emit as a decimal number, with a trailing space |
| `putchar(e)` | emit as a character |
| `puts(e)` | emit a zero-terminated string at address `e` |
| `emit(e)` | append a gene to the child |
| `child[i] = e` | overwrite a gene already emitted |
| `spawn()` | commit the child as offspring, and start a new one |

## Talking to a terminal

`input()` reads one character code; `putchar`/`puts`/`print` write. `shrewdc run`
wires these to real streams, so a program can prompt and wait:

```savvy
var line[128];

fn read_line(buf, max) {
    var n = 0;
    var c = input();
    while (c != '\n' && c != -1) {
        if (n < max - 1) { buf[n] = c; n = n + 1; }
        c = input();
    }
    buf[n] = 0;
    return n;
}

puts("Name? ");
read_line(line, 128);
puts("Hello, ");
puts(line);
```

The prompt appearing *before* the program blocks is not automatic: it needs the
output flushed at the moment input is requested. `shrewdc run` uses a streaming
I/O for that, and so does the IDE's terminal. Code that runs a genome against
input it already has uses a buffered one instead, which keeps `VM::run` a pure
function of `(genome, input, seed)` — reproducibility matters more there than
interactivity.

`input()` returns `-1` once input is exhausted — the only way a read loop over
stdin can know to stop, since nothing else in the language will stop it.

## Reproduction

Shrewd lets a genome read itself and write new genomes, and Savvy exposes that
directly. A program reads its own genes and emits a child's:

```savvy
for (var i = 0; i < self.length; i = i + 1) {
    emit(self[i]);
}
spawn();
```

That is a complete self-replicator (`examples/replicator.savvy`). There is no
quine trick: `self[i]` reads the running genome directly, including the genes of
that very loop.

Mutation is then just something the copy loop *does*
(`examples/mutator.savvy`):

```savvy
var rate = 20;

for (var i = 0; i < self.length; i = i + 1) {
    var g = self[i];
    if (rand() % rate == 0) { g = g + 1; }
    emit(g);
}
spawn();
```

`rate` is initialised from a literal, and that literal is a gene like any other.
So the rate is copied into the child along with everything else, and a child can
end up with a different one. Nobody outside the program tunes it.

Nothing in that loop is privileged, which is the whole point: it is ordinary
Savvy, compiled to ordinary genes, and `rand() % rate` is an ordinary expression
that could just as well be a condition on what is worth changing.

## Examples

Catalogued in [`examples/README.md`](../../examples/README.md). The short
version:

| | |
|---|---|
| `fizzbuzz.savvy` | the language in one page |
| `calculator.savvy` | expression parser; parentheses via mutual recursion |
| `repl.savvy` | interactive: prompts, reads text, evaluates |
| `hanoi.savvy` | recursion that branches twice per level |
| `fib.savvy` | naive vs memoised — what recursion costs |
| `brainfuck.savvy` | a Brainfuck interpreter, i.e. Turing-completeness demonstrated |
| `replicator.savvy` | a complete self-replicator, in four lines |
| `mutator.savvy` | a replicator whose mutation rate is a heritable gene |

## How the compiler works

One pass over a small AST: **lex → parse → emit**, in `parser.cpp` and
`compiler.cpp`. The parser is precedence-climbing; assignment is parsed as an
expression then reinterpreted as an lvalue, which is how `a[i] = e`,
`child[i] = e` and `r0 = e` all fall out of one rule. The compiler walks the
statements once, emitting genes as it goes — there is no IR and no
backpatching except one word: each function's frame size is patched into its
prologue after its body has been compiled.

### Constant folding

Any expression the compiler can evaluate becomes a single push, and it is
evaluated with the **VM's own arithmetic** (`shrewd/arith.hpp`) — wraparound,
`/0 == 0`, `INT64_MIN` edge cases and all — so folding can never change what a
program computes. The demo verifies this exhaustively (every operator crossed
with the awkward values, folded vs. executed).

Statements specialise on constant conditions:

- `while (1 == 1) { ... }` → a bare `DO ... PUSH1 END`: the test vanishes and
  an iteration pays 2 genes of loop plumbing instead of 7.
- `while (0) { ... }` → nothing at all.
- `if (const) { A } else { B }` → only the taken branch is compiled.
- `for (init; 0; ...)` → the init still runs (it is in scope), the loop never.
- A statement that is only a constant (`3 + 4;`) compiles to nothing.

A folded value that no longer fits in a gene (`2000000000 + 2000000000`) is
left to run time, where the same wrap produces the same answer. A pleasant side
effect: `-2147483648`, the one literal whose positive half does not fit a gene,
now folds and compiles.

### The memory map

Compiled code owns the tape it is given and lays it out like this:

```
cell 0          the compiler's stack pointer (SP)
cells 1..G      globals and string data, allocated upward
    ...         free
frames          function frames, allocated downward from mem.length
```

The first three genes of every program are `PUSH0 MSIZE MSTORE` — "SP starts at
the top of whatever tape I was handed". Nothing about the tape's size is baked
in; the same genome runs on 300 cells or 50,000 (`shrewd_demo` checks exactly
this). String literals are materialised once, up front, as stores to their
global cells, terminator included.

### Functions, frames, calls

A function body compiles to `PROC ... END` placed *after* the main code —
Shrewd skips `PROC` blocks in normal flow, so no jump over them is needed. A
call is: evaluate the arguments onto the data stack, push the callee's *index*,
`CALL`. The prologue then:

1. drops SP by the frame size (that one patched literal),
2. moves the arguments from the data stack into frame cells, last first,
3. runs the body with locals addressed as `SP + offset`.

`return e` evaluates `e`, restores SP, and `RET`s; falling off the end returns
`0`. Return values ride the data stack; return addresses ride Shrewd's separate
return stack — which is why the two recursive calls in `hanoi` cannot tread on
each other's locals, and why nothing a function pushes can corrupt where it
returns to.

Block scopes recycle cells: a `var` in a block releases its cell at the block's
end, and the next block reuses it (zeroed on first write in a fresh frame — a
`var` without an initialiser is explicitly stored as 0).

### What things cost

In this project code size and step count are currency, so the lowering is worth
knowing:

- A **global** read is `PUSHI addr; MLOAD` — 3 genes, 2 steps (2 genes when the
  address happens to be 0, 1, 2 or 10, which get one-gene pushes).
- A **local** read is `PUSH0; MLOAD; PUSHI off; ADD; MLOAD` — 6 genes, 5 steps:
  its address is `SP + offset`, computed every time. Locals cost more than
  globals; registers beat both.
- **`r0`–`r3`**: 1 gene, 1 step.
- `0`, `1`, `2`, `10`, `-1` are one-gene pushes; any other literal is two.
- `while`/`for` lower to `do { if (!cond) break; body; step; 1 } while(...)` —
  a live condition costs its own evaluation + 4 genes per iteration; a constant
  condition costs nothing (see folding).
- `a <= b` is `a > b` negated (`GT NOT`) — Shrewd has no `LE`/`GE`; fewer
  opcodes keeps the mutation space smaller and the identity is exact.
- A call: the arguments, then 2–3 genes (`PUSH<index>; CALL`); a frame
  prologue/epilogue is ~7 genes at each end.
- `puts` of a *literal* is a `putchar` per character — and allocates **no
  string data**: a literal that only ever feeds `puts` is never read from
  memory, so storing it would waste cells and startup stores. A literal used
  as a *value* (`var m = "hi";`, `streq(s, "quit")`) is materialised in
  memory as usual.
- `puts` of an *address* compiles to a 14-gene scan loop whose walking
  pointer lives **on the stack** (`DUP MLOAD` to test and print, `INC` to
  advance, `BREAK`+`POP` to leave) — it owns no memory cell. That matters:
  puts-ing data that can sit anywhere means the loop must not claim an
  address of its own, or it would eventually claim someone's data. ~11 steps
  per character printed.

### The decompiler

`decompile()` (`shrewdc savvy`) renders *any* genome — compiled, edited,
random — as Savvy, with two tested guarantees:

1. **The output always compiles and runs.** Checked in the demo against every
   example and 5,000 random genomes.
2. **For compiler-shaped genomes it behaves identically.** Every example
   round-trips: decompile, recompile, run with the same input and seed — same
   output, same offspring count. The decompiled replicator still copies
   *itself* (the new genome, not its ancestor). Beyond the examples, this was
   fuzz-tested with 110,000 generated programs (nested control flow, function
   calls, arrays, registers, I/O, reproduction) compared after the round trip
   on output, registers, offspring contents and inputs consumed — the demo
   keeps the failures that campaign found as permanent regression checks.

It works in four stages: symbolic replay (pushes become expression fragments,
side-effecting ops become statements), a statement *tree*, idiom recovery on
that tree, then printing. The idioms it recovers are the ones the compiler
lowers away, so compiler-shaped genomes come back looking hand-written:

- **Control flow.** `do { if (!c) break; ... } while (1)` is recognised as
  `while (c)`; `else if` chains print flat; negated comparisons flip
  (`!(a > b)` renders as `a <= b`, which also resurrects the `<=`/`>=` the
  compiler lowered to `GT NOT`).
- **Expressions.** Precedence-aware printing drops every parenthesis the
  grammar makes redundant; a literal fed to `putchar` is shown as the
  character it prints.
- **Output.** Runs of `putchar` with literal characters merge into one
  `puts("...")`, and the compiled 14-gene shape of `puts(pointer)` is
  recognised straight off the gene stream (every op in it is one gene and
  every block in it binds innermost, so the match cannot be fooled by
  context) and folded back into a single `puts(p);` — naming the local array
  it prints when the frame lifts.
- **Variables get names, exactly.** The recompiler allocates globals
  sequentially from cell 1, so the decompiler declares names covering cells
  1..K contiguously in cell order — `var g5;` for scalars, `bufN[len]` for
  indexed regions, `strN[len]` for string data (grouped and captioned
  `// "quit"`), `padN[len]` for gaps. Every name is pinned to its original
  cell *by construction*; nothing about it is heuristic.
- **Function frames are lifted.** A body whose only stack-pointer traffic is
  the compiler's own enter/leave bookkeeping gets real named locals — `var
  v0;`, `a1[8]` — laid out at the original offsets, and the bookkeeping
  disappears. Parameters are reconstructed by watching a body pop more than it
  pushed: the shortfall becomes `fn procN(arg0, arg1)`, and every call site
  passes exactly that many values.
- **Evaluation position is preserved.** An expression in text evaluates where
  it is *consumed*, but the genome evaluated it where it was *pushed* — so
  before any statement runs, pending fragments that could observe it are
  pinned into temporaries at the top of the tape (never `var`s, which would
  alias the cells the text addresses absolutely), and the stack-pointer init
  is rebased below them.
- **Cell 0 keeps its meaning.** Recompiling always plants the compiler's
  stack-pointer init (`mem[0] = mem.length`) at the top. A genome that had
  its own gets it back (rebased, above); a genome that never initialised
  cell 0 gets an explicit `mem[0] = 0;` so it still reads the zero it
  originally saw — omitted when nothing in the genome can load from memory.

What it cannot express, it says so and degrades — and the degraded shape is
chosen to *halt* whenever the original halted: a `CALL` whose target is
computed at run time is assumed 0; a loop whose continue-condition was pushed
*before* it (each iteration would pop a different value) is unrolled when it
is empty and its conditions are known, and otherwise runs once rather than
spinning forever; a `BREAK` that would jump out of its procedure becomes a
`return` (control never came back to the code below it, so falling through
would run code the original never reached); a body whose frame traffic is too
strange to lift keeps its raw pointer arithmetic and gets latched arguments.
Each carries a comment saying what was approximated.

Honest caveats remain even for faithful round-trips: a self-inspecting genome
(`self[i]`) sees the *recompiled* gene list, which is not the original one;
`Result::stack` may differ, since abandoned stack values are dropped (their
side effects are kept); and a program that reads memory it never wrote —
uninitialised locals, out-of-range indices that wrap into old frames — sees
the decompiler's bookkeeping (the temporaries at the top of the tape, frames
shifted below them) instead of whatever stale data it read originally. For a
bitwise-exact, round-trippable text form use `shrewdc asm`.

## What Savvy is not

It is a small procedural language, not C++. There are no structs, no classes, no
templates, no floats, no `continue`, no `switch`, no function pointers, no
multiple types. One type, one tape, and functions.

## Errors

The compiler is the one part of the toolchain that can fail. Once source becomes
a genome, nothing can fail again — that is the whole point of the split.

```
examples/calculator.savvy:12:5: error: unknown variable 'chr'
```

Parse errors carry line and column; compile errors (unknown variable, wrong
argument count, an array assignment, a literal too big for a gene) carry the
line. `savvy::compile()` returns `std::nullopt` and fills a `savvy::Error`
rather than throwing across the API.
