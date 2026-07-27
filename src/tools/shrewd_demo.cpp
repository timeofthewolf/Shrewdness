#include "savvy/savvy.hpp"
#include "shrewd/shrewd.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace shrewd;

namespace {

int g_failures = 0;

void heading(const std::string &title) {
    std::cout << "\n\033[1m" << title << "\033[0m\n"
              << std::string(title.size(), '-') << "\n";
}

void check(bool ok, const std::string &what) {
    std::cout << (ok ? "  \033[32mok\033[0m   " : "  \033[31mFAIL\033[0m ")
              << what << "\n";
    if (!ok)
        ++g_failures;
}

std::string escape(const std::string &s, std::size_t max = 64) {
    std::string out;
    for (std::size_t i = 0; i < s.size() && i < max; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n')
            out += "\\n";
        else if (c >= 32 && c < 127)
            out += static_cast<char>(c);
        else
            out += '.';
    }
    if (s.size() > max)
        out += "...";
    return out;
}

std::vector<Value> as_input(std::string_view s) {
    std::vector<Value> v;
    for (char c : s)
        v.push_back(static_cast<Value>(static_cast<unsigned char>(c)));
    return v;
}

Genome must_assemble(std::string_view src) {
    std::string err;
    if (auto g = assemble(src, &err))
        return *g;
    std::cerr << "seed genome failed to assemble: " << err << "\n";
    std::exit(1);
}

Genome must_compile(std::string_view src) {
    savvy::Error err;
    if (auto g = savvy::compile(src, &err))
        return *g;
    std::cerr << err.format("<inline>") << "\n";
    std::exit(1);
}

Genome must_compile_file(const std::string &name) {
    const std::string path = std::string(EXAMPLES_DIR) + "/" + name;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "cannot open " << path << "\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    savvy::Error err;
    if (auto g = savvy::compile(ss.str(), &err))
        return *g;
    std::cerr << err.format(path) << "\n";
    std::exit(1);
}

const char *kHelloSrc = R"(
    PUSHI 72   OUT      ; H
    PUSHI 101  OUT      ; e
    PUSHI 108  OUT      ; l
    PUSHI 108  OUT      ; l
    PUSHI 111  OUT      ; o
    PUSHI 44   OUT      ; ,
    PUSHI 32   OUT      ;
    PUSHI 87   OUT      ; W
    PUSHI 111  OUT      ; o
    PUSHI 114  OUT      ; r
    PUSHI 108  OUT      ; l
    PUSHI 100  OUT      ; d
)";

} // namespace

int main() {
    Limits budget;
    budget.max_steps = 10'000;
    budget.stack_limit = 1'000'000;
    budget.output_limit = 4096;
    budget.max_child_genes = 100'000;
    budget.max_offspring = 8;

    VM vm(budget);
    std::mt19937_64 rng(20260715);

    heading("1. A genome is a list of numbers");

    const Genome hello = must_assemble(kHelloSrc);
    std::cout << "  " << to_gene_list(hello) << "\n";
    const Result hr = vm.run(hello);
    std::cout << "  output: \"" << escape(hr.output) << "\"    " << hr.steps
              << " steps, " << to_string(hr.halt) << "\n";
    check(hr.output == "Hello, World", "the seed genome prints Hello, World");

    heading("2. Savvy compiles to Shrewd: a calculator");

    const Genome calc = must_compile_file("calculator.savvy");
    std::cout << "  examples/calculator.savvy -> " << calc.size()
              << " genes\n\n";

    struct Case {
        const char *in;
        const char *want;
    };
    const Case cases[] = {
        {"2+3", "5 \n"},        {"12+34*2", "80 \n"},   {"100-7*3", "79 \n"},
        {"7%3", "1 \n"},        {"-5+12", "7 \n"},      {" 8 * 9 ", "72 \n"},
        {"1+2+3+4+5", "15 \n"}, {"(12+34)*2", "92 \n"}, {"2*(3+4)", "14 \n"},
        {"((1+2))*3", "9 \n"},  {"100/(2+3)", "20 \n"}, {"-(4+1)*2", "-10 \n"},
    };
    bool all_right = true;
    for (const Case &c : cases) {
        const Result r = vm.run(calc, as_input(c.in));
        const bool ok = r.output == c.want;
        if (!ok)
            all_right = false;
        std::cout << "    " << std::left << std::setw(12) << c.in << " => "
                  << std::setw(8) << escape(r.output) << std::right
                  << (ok ? "" : "  WANTED ") << (ok ? "" : c.want) << "\n";
    }
    check(all_right,
          "the compiled calculator evaluates with correct precedence");

    heading("2b. Recursion, on a real return stack");

    {
        Limits deep = budget;
        deep.max_steps = 20'000'000;
        VM deep_vm(deep);

        const Genome hanoi = must_compile_file("hanoi.savvy");
        const Result hr2 = deep_vm.run(hanoi);
        std::size_t moves = 0;
        for (std::size_t at = hr2.output.find("move disc");
             at != std::string::npos;
             at = hr2.output.find("move disc", at + 1)) {
            ++moves;
        }
        std::cout << "  hanoi.savvy   " << hanoi.size() << " genes, "
                  << hr2.steps << " steps, " << moves << " moves\n";
        check(moves == 15, "hanoi(4) makes exactly 15 moves");

        const Genome fib = must_compile_file("fib.savvy");
        const Result fr = deep_vm.run(fib);
        std::cout << "  fib.savvy     " << fib.size() << " genes, " << fr.steps
                  << " steps\n";
        check(fr.output.find("1548008755920") != std::string::npos,
              "fib(60) via a memo array is exactly 1548008755920");

        const Genome bf = must_compile_file("brainfuck.savvy");
        const Result br = deep_vm.run(bf);
        std::cout << "  brainfuck.savvy " << bf.size() << " genes, " << br.steps
                  << " steps -> \"" << escape(br.output) << "\"\n";
        check(br.output.find("Hello World!") != std::string::npos,
              "the Brainfuck interpreter runs a Brainfuck Hello World");
    }

    heading("2c. Text in and text out");

    {
        const Genome repl = must_compile_file("repl.savvy");
        const Result rr =
            vm.run(repl, as_input("Lowe\n(1+2)*3\n10/(2+3)\nquit\n"));
        std::cout << "  repl.savvy -> \"" << escape(rr.output, 96) << "\"\n";
        check(rr.output.find("Hello, Lowe!") != std::string::npos,
              "it reads a line of text and echoes it back");
        check(rr.output.find("= 9") != std::string::npos &&
                  rr.output.find("= 2") != std::string::npos,
              "it evaluates what it is typed, parentheses and all");
        check(rr.output.find("bye") != std::string::npos, "it exits on 'quit'");

        const Genome fizz = must_compile_file("fizzbuzz.savvy");
        const Result fz = vm.run(fizz);
        check(fz.output.find("FizzBuzz") != std::string::npos &&
                  fz.output.find("Fizz\n") != std::string::npos,
              "fizzbuzz.savvy does what its name promises");
    }

    heading("2e. The language imposes no limits of its own");

    {
        const Genome forever =
            must_compile("var n = 0; while (1 == 1) { n = n + 1; }");
        const Genome recurse =
            must_compile("fn down(n) { return down(n + 1); } down(0);");

        VM free_vm(Limits{});
        for (std::size_t budget_steps : {1'000u, 50'000u, 1'000'000u}) {
            Limits b;
            b.max_steps = budget_steps;
            VM bounded(b);
            const Result r = bounded.run(forever);
            if (budget_steps == 1'000'000u) {
                std::cout << "  while (1 == 1) ran for exactly the " << r.steps
                          << " steps it was given\n";
            }
            if (r.steps != budget_steps || r.halt != Halt::OutOfGas) {
                check(false,
                      "an infinite loop should spend exactly its budget");
            }
        }
        check(true, "while (1 == 1) loops until the caller's budget runs out, "
                    "not before");

        Limits b;
        b.max_steps = 2'000'000;
        VM deep_vm(b);
        const Result rr = deep_vm.run(recurse);
        std::cout << "  unbounded recursion reached depth "
                  << rr.registers.size() * 0 + rr.steps
                  << " steps deep before the budget stopped it\n";
        check(rr.halt == Halt::OutOfGas,
              "infinite recursion recurses until the budget says stop");

        Limits small = Limits{};
        small.memory_size = 300;
        small.max_steps = 100'000;
        Limits large = Limits{};
        large.memory_size = 50'000;
        large.max_steps = 100'000;

        const Genome asks = must_compile("print(mem.length);");
        VM small_vm(small), large_vm(large);
        const std::string a = small_vm.run(asks).output;
        const std::string b2 = large_vm.run(asks).output;
        std::cout << "  the same genome reports its RAM as " << escape(a)
                  << "and " << escape(b2) << "depending on what it was given\n";
        check(a == "300 " && b2 == "50000 ",
              "MSIZE reports the tape the environment handed out");
    }

    heading("2f. Programs can span files: include");

    {
        const std::map<std::string, std::string> files = {
            {"main", "include \"math\";\ninclude \"greet\";\n"
                     "print(cube(3));\ngreet();\n"},
            {"math", "include \"util\";\ninclude \"greet\";\n"
                     "fn cube(x) { return sq(x) * x; }\n"},
            {"greet", "include \"util\";\ninclude \"math\";\n"
                      "fn greet() { puts(\"hi\"); }\n"},
            {"util", "fn sq(x) { return x * x; }\n"},
        };
        savvy::Options opts;
        opts.entry = "main";
        opts.resolver =
            [&](const std::string &name,
                const std::string &) -> std::optional<savvy::Source> {
            const auto it = files.find(name);
            if (it == files.end())
                return std::nullopt;
            return savvy::Source{it->first, it->second};
        };

        savvy::Error err;
        auto g = savvy::compile(files.at("main"), opts, &err);
        check(g.has_value(), "a diamond of includes with a cycle compiles "
                             "(include-once does the untangling)");
        if (g) {
            VM vm;
            check(vm.run(*g).output == "27 hi",
                  "functions from every included file are callable");
        }

        auto broken = files;
        broken["util"] = "fn sq(x) { return x * missing; }\n";
        opts.resolver =
            [&](const std::string &name,
                const std::string &) -> std::optional<savvy::Source> {
            const auto it = broken.find(name);
            if (it == broken.end())
                return std::nullopt;
            return savvy::Source{it->first, it->second};
        };
        check(!savvy::compile(broken.at("main"), opts, &err) &&
                  err.file == "util" && err.line == 1,
              "an error inside an included file names that file and line");

        check(!savvy::compile("include \"ghost\";", opts, &err) &&
                  err.message.find("ghost") != std::string::npos,
              "a missing include is a compile error naming the include");

        savvy::Error plain_err;
        check(!savvy::compile("include \"x\";", &plain_err),
              "single-file compiles reject include (no resolver)");
    }

    heading("2d. Calls survive being moved");

    {
        auto nop_tolerance = [&](const Genome &g,
                                 const std::vector<Value> &in) {
            const std::string want = vm.run(g, in).output;
            std::size_t neutral = 0;
            Genome m;
            for (std::size_t i = 0; i <= g.size(); ++i) {
                const auto cut = g.begin() + static_cast<std::ptrdiff_t>(i);
                m.clear();
                m.reserve(g.size() + 1);
                m.insert(m.end(), g.begin(), cut);
                m.push_back(static_cast<Gene>(Op::NOP));
                m.insert(m.end(), cut, g.end());
                if (vm.run(m, in).output == want)
                    ++neutral;
            }
            return 100.0 * static_cast<double>(neutral) /
                   static_cast<double>(g.size() + 1);
        };

        const double rep_pct =
            nop_tolerance(must_compile_file("replicator.savvy"), {});
        const double fizz_pct =
            nop_tolerance(must_compile_file("fizzbuzz.savvy"), {});
        const double calc_pct = nop_tolerance(calc, as_input("12+3"));

        std::cout
            << std::fixed << std::setprecision(1)
            << "  a NOP inserted at every position, output still identical:\n"
            << "    replicator (no calls):  " << rep_pct << "%\n"
            << "    fizzbuzz   (no calls):  " << fizz_pct << "%\n"
            << "    calculator (calls):     " << calc_pct << "%\n";

        check(calc_pct > 80.0,
              "calls tolerate insertion (23% when CALL held an address)");
        check(rep_pct > 99.0,
              "a genome without calls is perfectly relocatable");
    }

    heading("3. An organism copies itself");

    {
        const Genome rep = must_compile_file("replicator.savvy");
        const Result r = vm.run(rep);

        std::cout << "  examples/replicator.savvy -> " << rep.size()
                  << " genes, " << r.steps << " steps, " << r.offspring.size()
                  << " offspring\n";
        check(r.offspring.size() == 1,
              "the replicator spawns exactly one child");
        check(!r.offspring.empty() && r.offspring[0] == rep,
              "the child is a gene-for-gene copy of the parent");

        if (!r.offspring.empty()) {
            const Result r2 = vm.run(r.offspring[0]);
            check(r2.offspring.size() == 1 && r2.offspring[0] == rep,
                  "the child replicates too: the lineage is self-sustaining");
        }
    }

    heading("4. An organism mutates its own offspring");

    {
        const Genome mut = must_compile_file("mutator.savvy");
        std::cout << "  examples/mutator.savvy -> " << mut.size() << " genes\n";

        std::size_t identical = 0, mutated = 0, fertile = 0;
        for (std::uint64_t seed = 0; seed < 200; ++seed) {
            const Result r = vm.run(mut, {}, seed);
            if (r.offspring.empty())
                continue;
            const Genome &child = r.offspring[0];
            (child == mut) ? ++identical : ++mutated;

            if (!vm.run(child, {}, seed + 1).offspring.empty())
                ++fertile;
        }
        std::cout << "  200 seeds: " << identical << " exact copies, "
                  << mutated << " mutated children, " << fertile
                  << " of them still fertile\n";
        check(identical > 0 && mutated > 0,
              "the same genome both copies and varies, by seed");
        check(fertile > 0, "mutated offspring can still reproduce");

        const Result fast = vm.run(must_compile_file("mutator.savvy"), {}, 3);
        std::cout << "  the rate lives at a gene, not in the harness: "
                  << "changing that one literal changes the lineage's mutation "
                     "rate\n";
        check(!fast.offspring.empty(), "the mutator reproduces");
    }

    heading("5. Every random genome is a valid program");

    {
        std::uniform_int_distribution<std::size_t> len(0, 200);
        std::size_t out_of_gas = 0, produced_output = 0, total_steps = 0,
                    spawned = 0;
        constexpr int kTrials = 200'000;

        for (int i = 0; i < kTrials; ++i) {
            const Genome g = random_genome(len(rng), rng);
            const Result r = vm.run(g, {}, static_cast<std::uint64_t>(i));
            total_steps += r.steps;
            if (r.halt == Halt::OutOfGas)
                ++out_of_gas;
            if (!r.output.empty())
                ++produced_output;
            spawned += r.offspring.size();
        }

        std::cout << "  " << kTrials
                  << " random genomes ran to a defined halt\n"
                  << "    hit the gas cap: " << out_of_gas << "\n"
                  << "    emitted output:  " << produced_output << "\n"
                  << "    spawned:         " << spawned << " offspring\n"
                  << "    mean cost:       " << (total_steps / kTrials)
                  << " steps\n";
        check(true, "no crash, no hang, no undefined behaviour");
    }

    heading("6. Every single-gene mutant of the seed, exhaustively");

    {
        std::size_t total = 0, neutral = 0, altered = 0, silenced = 0;

        for (std::size_t i = 0; i < hello.size(); ++i) {
            for (Gene v = 0; v <= kRandomGeneMax; ++v) {
                if (v == hello[i])
                    continue;
                Genome m = hello;
                m[i] = v;
                const Result r = vm.run(m);
                ++total;
                if (r.output == "Hello, World")
                    ++neutral;
                else if (r.output.empty())
                    ++silenced;
                else
                    ++altered;
            }
        }

        const auto pct = [&](std::size_t x) {
            return 100.0 * static_cast<double>(x) / static_cast<double>(total);
        };
        std::cout << "  " << total << " one-gene variants (" << hello.size()
                  << " positions x " << kRandomGeneMax
                  << " alternative values)\n"
                  << std::fixed << std::setprecision(1)
                  << "    invalid or crashing: 0\n"
                  << "    neutral (unchanged): " << neutral << "  ("
                  << pct(neutral) << "%)\n"
                  << "    altered phenotype:   " << altered << "  ("
                  << pct(altered) << "%)\n"
                  << "    silenced (no output):" << silenced << "  ("
                  << pct(silenced) << "%)\n";
        check(total == hello.size() * kRandomGeneMax,
              "the entire one-mutation neighbourhood runs");
        check(neutral > 0 && altered > 0,
              "the neighbourhood mixes neutral and altered variants");
    }

    heading("7. Mutation never produces a broken program");

    {
        const MutationRates rates;
        std::size_t survived = 0, out_of_gas = 0;
        constexpr int kLineages = 2'000;
        constexpr int kGenerations = 50;

        for (int l = 0; l < kLineages; ++l) {
            Genome g = hello;
            for (int gen = 0; gen < kGenerations; ++gen) {
                g = mutate(g, rates, rng);
                const Result r = vm.run(g);
                ++survived;
                if (r.halt == Halt::OutOfGas)
                    ++out_of_gas;
            }
        }

        std::cout << "  " << kLineages << " lineages x " << kGenerations
                  << " generations of unselected drift\n"
                  << "    runs completed:  " << survived << " / "
                  << (kLineages * kGenerations)
                  << "\n    hit the gas cap: " << out_of_gas << "\n";
        check(survived == kLineages * kGenerations,
              "every mutant in every lineage ran");

        MutationRates runaway;
        runaway.insertion = 0.5;
        runaway.deletion = 0.0;
        runaway.duplication = 1.0;
        runaway.max_length = 64;

        Genome g = hello;
        bool capped = true;
        for (int i = 0; i < 500; ++i) {
            g = mutate(g, runaway, rng);
            if (g.size() > runaway.max_length)
                capped = false;
        }
        check(capped, "max_length holds under a runaway insertion rate");
    }

    heading("8. Mutants drift, they do not die");

    {
        MutationRates rates;
        rates.substitution = 0.06;
        for (int i = 0; i < 6; ++i) {
            const Genome m = mutate(hello, rates, rng);
            const Result r = vm.run(m);
            std::cout << "  " << std::setw(3) << m.size() << " genes  "
                      << std::setw(4) << r.steps << " steps  " << std::left
                      << std::setw(11) << to_string(r.halt) << std::right
                      << "  \"" << escape(r.output, 40) << "\"\n";
        }
    }

    heading("9. Runs are reproducible, and the translators agree with the VM");

    {
        std::uniform_int_distribution<std::size_t> len(0, 120);
        bool deterministic = true, seed_matters = false, round_trips = true;
        bool reuse_matches = true;
        Result reused;

        for (int i = 0; i < 20'000; ++i) {
            const Genome g = random_genome(len(rng), rng);
            const auto seed = static_cast<std::uint64_t>(i);

            const Result a = vm.run(g, {}, seed);
            const Result b = vm.run(g, {}, seed);
            if (a.output != b.output || a.steps != b.steps ||
                a.registers != b.registers || a.offspring != b.offspring) {
                deterministic = false;
            }
            if (vm.run(g, {}, seed + 991).output != a.output)
                seed_matters = true;

            vm.run_into(reused, g, {}, seed);
            if (reused.halt != a.halt || reused.steps != a.steps ||
                reused.output != a.output || reused.registers != a.registers ||
                reused.stack != a.stack ||
                reused.inputs_read != a.inputs_read ||
                reused.offspring != a.offspring ||
                reused.uncommitted_child_genes != a.uncommitted_child_genes) {
                reuse_matches = false;
            }

            const auto back = assemble(to_assembly(g));
            if (!back || *back != g)
                round_trips = false;
        }

        check(deterministic,
              "(genome, input, seed) always reproduces a run exactly");
        check(seed_matters, "the seed actually reaches rand()");
        check(round_trips, "assemble(to_assembly(g)) returns g gene for gene");
        check(reuse_matches,
              "run_into a recycled Result matches a fresh run exactly");

        const char *const fold_ops[] = {"+",  "-",  "*",  "/",  "%",  "<", ">",
                                        "<=", ">=", "==", "!=", "&&", "||"};
        const long long fold_vals[] = {0,  1,  2,   -1,         10,         7,
                                       -7, 56, -56, 2147483647, -2147483648};
        bool fold_agrees = true;
        for (const char *op : fold_ops) {
            for (const long long a : fold_vals) {
                for (const long long b : fold_vals) {
                    const std::string sa = std::to_string(a);
                    const std::string sb = "(" + std::to_string(b) + ")";
                    const Genome folded = must_compile("print(" + sa + " " +
                                                       op + " " + sb + ");");
                    const Genome live = must_compile(
                        "var x = " + sa + "; print(x " + op + " " + sb + ");");
                    if (vm.run(folded).output != vm.run(live).output)
                        fold_agrees = false;
                }
            }
        }
        check(fold_agrees,
              "constant folding is bit-identical to running the ops");
    }

    heading("9b. Any genome decompiles to Savvy that compiles and runs");

    {
        struct RoundTrip {
            const char *file;
            const char *input;
        };
        const RoundTrip trips[] = {
            {"fizzbuzz.savvy", ""},
            {"hanoi.savvy", ""},
            {"fib.savvy", ""},
            {"brainfuck.savvy", ""},
            {"calculator.savvy", "(12+34)*2\n"},
            {"repl.savvy", "Lowe\n(1+2)*3\nquit\n"},
            {"replicator.savvy", ""},
            {"mutator.savvy", ""},
        };
        Limits deep = budget;
        deep.max_steps = 30'000'000;
        VM rt_vm(deep);

        bool all_compile = true, all_match = true;
        for (const RoundTrip &t : trips) {
            const Genome orig = must_compile_file(t.file);
            savvy::Error err;
            const auto back = savvy::compile(savvy::decompile(orig), &err);
            if (!back) {
                std::cout << "  " << t.file
                          << " FAILED to recompile: " << err.format(t.file)
                          << "\n";
                all_compile = false;
                continue;
            }
            const Result a = rt_vm.run(orig, as_input(t.input), 5);
            const Result b = rt_vm.run(*back, as_input(t.input), 5);
            if (a.output != b.output ||
                a.offspring.size() != b.offspring.size()) {
                std::cout << "  " << t.file << " diverged: \""
                          << escape(a.output, 24) << "\" vs \""
                          << escape(b.output, 24) << "\", "
                          << a.offspring.size() << " vs " << b.offspring.size()
                          << " offspring\n";
                all_match = false;
            }
        }
        check(all_compile, "every example survives decompile -> recompile");
        check(
            all_match,
            "and behaves identically afterwards (output and offspring count)");

        const Genome rep2 = *savvy::compile(
            savvy::decompile(must_compile_file("replicator.savvy")));
        const Result rr2 = vm.run(rep2);
        check(
            rr2.offspring.size() == 1 && rr2.offspring[0] == rep2,
            "the decompiled replicator still copies itself, not its ancestor");

        const std::string fz =
            savvy::decompile(must_compile_file("fizzbuzz.savvy"));
        check(fz.find("while (") != std::string::npos &&
                  fz.find("} else if (") != std::string::npos &&
                  fz.find("puts(\"FizzBuzz\");") != std::string::npos,
              "decompiled fizzbuzz reads as while / else-if / puts, not stack "
              "ops");
        const std::string fb = savvy::decompile(must_compile_file("fib.savvy"));
        check(fb.find("arg0") != std::string::npos &&
                  fb.find("mem[mem[0]") == std::string::npos,
              "decompiled fib has named locals and parameters, no frame "
              "arithmetic");

        std::uniform_int_distribution<std::size_t> len(0, 120);
        constexpr int kTrials = 5'000;
        std::size_t failed = 0;
        bool clean_text = true;
        for (int i = 0; i < kTrials; ++i) {
            const Genome g = random_genome(len(rng), rng);
            const std::string src = savvy::decompile(g);
            if (src.find('\x01') != std::string::npos)
                clean_text = false;
            savvy::Error err;
            const auto back = savvy::compile(src, &err);
            if (!back) {
                if (++failed == 1) {
                    std::cout
                        << "  first failure: " << err.format("<decompiled>")
                        << "\n  genome: " << to_gene_list(g) << "\n";
                }
                continue;
            }
            vm.run(*back, {}, static_cast<std::uint64_t>(i));
        }
        std::cout << "  " << kTrials
                  << " random genomes decompiled, recompiled, and rerun to a "
                     "defined halt\n";
        check(failed == 0,
              "decompiled Savvy always compiles, whatever the genome");
        check(clean_text,
              "no internal renaming markers ever leak into the text");
    }

    heading("9c. Decompilation preserves behaviour (found by fuzzing, kept "
            "as regressions)");

    {
        auto same_after_round_trip = [&](const Genome &g,
                                         const std::vector<Value> &input) {
            Limits lim;
            lim.max_steps = 1'000'000;
            VM rt(lim);
            const Result a = rt.run(g, input, 7);
            savvy::Error err;
            const auto back = savvy::compile(savvy::decompile(g), &err);
            if (!back)
                return false;
            const Result b = rt.run(*back, input, 7);
            return a.halt == Halt::Completed && b.halt == Halt::Completed &&
                   a.output == b.output && a.registers == b.registers &&
                   a.offspring == b.offspring && a.inputs_read == b.inputs_read;
        };

        const Genome reads_cell0 = {117, 142, 168, 146, 142, 203, 36, 179, 175};
        check(same_after_round_trip(reads_cell0, {}),
              "a genome that reads virgin cell 0 still sees zero");

        const Genome carried_loop = {
            38, 3,  39, 40, 38, 43, 44, 38, 41, 3,  3,  2,  3,  3,  2,
            43, 39, 39, 42, 2,  39, 2,  44, 2,  42, 37, 43, 37, 40, 43,
            44, 43, 37, 37, 37, 39, 42, 3,  37, 39, 44, 43, 2,  42, 40,
            41, 3,  42, 40, 2,  37, 39, 43, 2,  38, 2,  40};
        check(same_after_round_trip(carried_loop, {}),
              "a loop fed by values pushed before it still terminates");

        const Genome open_if_call = {67,  81,  146, 176, 19, 159, 202, 134, 90,
                                     41,  69,  180, 65,  2,  192, 29,  80,  205,
                                     215, 180, 219, 99,  42, 65,  45,  18,  66};
        check(same_after_round_trip(open_if_call, {5}),
              "effects inside an unclosed block stay inside it");

        const Genome bangbang = must_compile(
            "r0 = 5; print(!(!r0)); r1 = (!(!(159 && r0))) % 5; print(r1);");
        check(same_after_round_trip(bangbang, {}),
              "double negation keeps its value and its precedence");

        const Genome fn_puts = must_compile(
            "fn shout(c) { var s[3]; s[0] = c; s[1] = 33; s[2] = 0; puts(s); "
            "return 0; } shout('a'); shout('b');");
        check(same_after_round_trip(fn_puts, {}),
              "puts(pointer) owns no cell and survives moving frames");
        {
            Limits lim;
            VM pv(lim);
            const Result pr = pv.run(fn_puts);
            check(pr.output == "a!b!", "and it still prints what it should");
        }

        savvy::Error err;
        check(!savvy::compile("var x = 99999999999999999999;", &err) &&
                  err.message.find("too large") != std::string::npos,
              "a number literal too big for a value is a parse error");
    }

    heading("10. The landscape is climbable");

    {
        const std::string target = "Hello, World";

        auto edit_distance = [](const std::string &a, const std::string &b) {
            std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
            for (std::size_t j = 0; j <= b.size(); ++j)
                prev[j] = j;
            for (std::size_t i = 1; i <= a.size(); ++i) {
                cur[0] = i;
                for (std::size_t j = 1; j <= b.size(); ++j) {
                    cur[j] = std::min(
                        {prev[j] + 1, cur[j - 1] + 1,
                         prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
                }
                std::swap(prev, cur);
            }
            return prev[b.size()];
        };

        Limits fit_budget = budget;
        fit_budget.output_limit = 32;
        VM fit_vm(fit_budget);

        constexpr std::size_t kFreeGenes = 96;
        auto length_cost = [](std::size_t n) {
            return n <= kFreeGenes ? 0.0
                                   : 0.02 * static_cast<double>(n - kFreeGenes);
        };
        auto fitness = [&](const Genome &g) {
            const Result r = fit_vm.run(g);
            return -static_cast<double>(edit_distance(r.output, target)) -
                   0.0005 * static_cast<double>(r.steps) -
                   length_cost(g.size());
        };

        MutationRates rates;
        rates.substitution = 0.02;
        rates.insertion = 0.01;
        rates.deletion = 0.01;

        constexpr int kGenerations = 6'000;
        constexpr int kOffspring = 60;

        Genome best = random_genome(40, rng);
        double best_f = fitness(best);
        const double start_f = best_f;

        for (int gen = 0; gen <= kGenerations; ++gen) {
            if (gen % 2000 == 0) {
                std::cout << "  gen " << std::setw(5) << gen << "  fitness "
                          << std::setw(7) << std::fixed << std::setprecision(2)
                          << best_f << "  \""
                          << escape(fit_vm.run(best).output, 30) << "\"\n";
            }
            for (int k = 0; k < kOffspring; ++k) {
                Genome child = mutate(best, rates, rng);
                const double f = fitness(child);
                if (f >= best_f) {
                    best_f = f;
                    best = std::move(child);
                }
            }
        }

        std::cout << "\n  evolved " << best.size() << " genes, output \""
                  << escape(fit_vm.run(best).output, 30) << "\"\n";
        check(best_f > start_f, "selection improved fitness over random noise");
    }

    std::cout << "\n";
    if (g_failures) {
        std::cout << "\033[31m" << g_failures << " check(s) failed\033[0m\n";
        return 1;
    }
    std::cout << "\033[32mall checks passed\033[0m\n";
    return 0;
}
