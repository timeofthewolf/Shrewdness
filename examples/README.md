# Examples

Each example demonstrates one thing. All of them compile with `shrewdc` and are
also compiled and checked on every run of `shrewd_demo` — they are the test
corpus, so they cannot silently rot.

```sh
./build/shrewdc run examples/<name>.savvy          # compile and run
./build/shrewdc asm examples/<name>.savvy          # the Shrewd it becomes
./build/shrewdc genes examples/<name>.savvy        # the raw genome
```

They are also bundled into the [Shrewdness IDE](../web/README.md), which lists
them in the Explorer — opening one copies it into your project, editable.

None of them need a `--steps` budget — they all halt on their own (except the
REPL and calculator, which halt when input runs out).

## The language

| example | demonstrates | expected |
|---|---|---|
| [`fizzbuzz.savvy`](fizzbuzz.savvy) | most of the language on one page: `for`, `if`/`else if`, `%`, `puts`, `print` | `1 2 Fizz 4 Buzz ... FizzBuzz` up to 20 |
| [`hanoi.savvy`](hanoi.savvy) | recursion that branches twice per level — the calls at one depth must not share locals, and don't, because every call gets its own frame | 15 moves for 4 discs, then `done` |
| [`fib.savvy`](fib.savvy) | naive vs memoised recursion — `--trace` counts the steps each costs, and the two differ exponentially | ends `memo(60) = 1548008755920` |
| [`brainfuck.savvy`](brainfuck.savvy) | an interpreter for a Turing-complete language, hosted by Shrewd — the completeness claim made concrete | `Hello World!` |

## Talking to a terminal

| example | demonstrates | try |
|---|---|---|
| [`calculator.savvy`](calculator.savvy) | recursive-descent parsing with **mutual recursion** (`factor` calls `expr`); loops forever by design, stops only at end of input | `echo "(12+34)*2" \| shrewdc run examples/calculator.savvy` → `92` |
| [`repl.savvy`](repl.savvy) | an interactive session: prompts flush before input blocks, reads lines into a buffer, evaluates | `shrewdc run examples/repl.savvy`, type sums, `quit` to leave |

## Programs that write programs

| example | demonstrates | try |
|---|---|---|
| [`replicator.savvy`](replicator.savvy) | complete self-replication in four lines: `self[i]` reads the running genome, `emit` builds the child, `spawn` commits it | `shrewdc run examples/replicator.savvy --trace` → `1 offspring` |
| [`mutator.savvy`](mutator.savvy) | a replicator that perturbs its own child — the rate is a literal in the genome, so it is copied along with everything else | `shrewdc run examples/mutator.savvy --seed 7 --trace` |

Neither prints anything — their product is a child genome, not text. Capture it
with `--offspring`; the children are genomes, so they run like anything else:

```sh
shrewdc run examples/replicator.savvy --offspring gen1   # -> gen1/child0.shrewd
shrewdc run gen1/child0.shrewd        --offspring gen2   # the child reproduces
diff gen1/child0.shrewd gen2/child0.shrewd               # faithful: identical
```

The mutator is the language's central claim in miniature: vary the seed and the
same genome produces exact copies or altered ones, and every one of them is a
valid program — some of which can no longer copy themselves. See "Reproduction"
in the [Savvy README](../src/savvy/README.md#reproduction).
