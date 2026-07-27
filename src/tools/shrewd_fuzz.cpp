#include "savvy/savvy.hpp"
#include "shrewd/asm.hpp"
#include "shrewd/genome.hpp"
#include "shrewd/isa.hpp"
#include "shrewd/vm.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace shrewd;

static std::uint64_t fnv(std::uint64_t h, const void *p, std::size_t n) {
    const unsigned char *b = static_cast<const unsigned char *>(p);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}
template <typename T> static std::uint64_t fnv1(std::uint64_t h, T v) {
    return fnv(h, &v, sizeof v);
}

static std::uint64_t hash_result(const Result &r) {
    std::uint64_t h = 1469598103934665603ULL;
    h = fnv1(h, static_cast<int>(r.halt));
    h = fnv1(h, r.steps);
    h = fnv(h, r.output.data(), r.output.size());
    for (Value v : r.registers)
        h = fnv1(h, v);
    for (Value v : r.stack)
        h = fnv1(h, v);
    h = fnv1(h, r.inputs_read);
    h = fnv1(h, r.offspring.size());
    for (const Genome &c : r.offspring) {
        h = fnv1(h, c.size());
        h = fnv(h, c.data(), c.size() * sizeof(Gene));
    }
    h = fnv1(h, r.uncommitted_child_genes);
    return h;
}

static Genome random_genome_any(std::mt19937_64 &rng) {
    std::uniform_int_distribution<std::size_t> len(0, 300);
    std::uniform_int_distribution<int> kind(0, 5);
    const std::size_t n = len(rng);
    Genome g(n);
    switch (kind(rng)) {
    case 0: {
        std::uniform_int_distribution<Gene> d(std::numeric_limits<Gene>::min(),
                                              std::numeric_limits<Gene>::max());
        for (Gene &x : g)
            x = d(rng);
        break;
    }
    case 1: {
        static const Op ctl[] = {Op::IF,    Op::ELSE, Op::DO,   Op::END,
                                 Op::BREAK, Op::PROC, Op::CALL, Op::RET,
                                 Op::PUSH1, Op::PUSH0};
        std::uniform_int_distribution<std::size_t> d(0, std::size(ctl) - 1);
        for (Gene &x : g)
            x = static_cast<Gene>(ctl[d(rng)]);
        break;
    }
    case 2: {
        static const Op rep[] = {Op::EMIT,   Op::SPAWN, Op::CLEN,  Op::CREAD,
                                 Op::CWRITE, Op::IN,    Op::OUT,   Op::OUTNUM,
                                 Op::GLEN,   Op::GREAD, Op::PUSHI, Op::RAND};
        std::uniform_int_distribution<std::size_t> d(0, std::size(rep) - 1);
        for (Gene &x : g)
            x = static_cast<Gene>(rep[d(rng)]);
        break;
    }
    default:
        return random_genome(n, rng);
    }
    return g;
}

static Limits random_limits(std::mt19937_64 &rng) {
    auto pick = [&](std::initializer_list<std::size_t> xs) {
        std::uniform_int_distribution<std::size_t> d(0, xs.size() - 1);
        return xs.begin()[d(rng)];
    };
    Limits l;
    l.max_steps = pick({1, 7, 100, 5000, 20000});
    l.stack_limit = pick({0, 1, 2, 16, 100000});
    l.output_limit = pick({0, 1, 8, 4096});
    l.max_call_depth = pick({0, 1, 4, 1000});
    l.max_child_genes = pick({0, 1, 16, 10000});
    l.max_offspring = pick({0, 1, 4});
    l.memory_size = pick({1, 2, 3, 8, 64, 65536});
    return l;
}

static std::vector<Value> random_input(std::mt19937_64 &rng) {
    std::uniform_int_distribution<std::size_t> len(0, 16);
    std::uniform_int_distribution<int> kind(0, 3);
    std::vector<Value> in(len(rng));
    for (Value &v : in) {
        switch (kind(rng)) {
        case 0:
            v = kEndOfInput;
            break;
        case 1:
            v = std::uniform_int_distribution<Value>(-5, 300)(rng);
            break;
        default:
            v = std::uniform_int_distribution<Value>(
                std::numeric_limits<Value>::min(),
                std::numeric_limits<Value>::max())(rng);
            break;
        }
    }
    return in;
}

static int mode_vmhash(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uint64_t all = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Genome g = random_genome_any(rng);
        VM vm(random_limits(rng));
        const Result r = vm.run(g, random_input(rng), rng());
        const std::uint64_t h = hash_result(r);
        all = fnv1(all, h);
        std::printf("%zu %016llx\n", i, static_cast<unsigned long long>(h));
    }
    std::printf("total %016llx\n", static_cast<unsigned long long>(all));
    return 0;
}

struct SavvyGen {
    std::mt19937_64 rng;
    std::string out;
    int uniq = 0;
    int fn_count = 0;
    int current_fn = -1;
    int loop_depth = 0;
    std::vector<std::vector<std::string>> scalars;
    std::vector<std::vector<std::pair<std::string, int>>> arrays;

    explicit SavvyGen(std::uint64_t seed) : rng(seed) {}

    int irand(int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    }
    bool chance(int pct) { return irand(0, 99) < pct; }

    std::string fresh(const char *stem) {
        return std::string(stem) + std::to_string(uniq++);
    }

    void push_scope() {
        scalars.emplace_back();
        arrays.emplace_back();
    }
    void pop_scope() {
        scalars.pop_back();
        arrays.pop_back();
    }

    const std::string *pick_scalar() {
        std::size_t total = 0;
        for (const auto &s : scalars)
            total += s.size();
        if (total == 0)
            return nullptr;
        std::size_t k =
            std::uniform_int_distribution<std::size_t>(0, total - 1)(rng);
        for (const auto &s : scalars) {
            if (k < s.size())
                return &s[k];
            k -= s.size();
        }
        return nullptr;
    }
    const std::pair<std::string, int> *pick_array() {
        std::size_t total = 0;
        for (const auto &s : arrays)
            total += s.size();
        if (total == 0)
            return nullptr;
        std::size_t k =
            std::uniform_int_distribution<std::size_t>(0, total - 1)(rng);
        for (const auto &s : arrays) {
            if (k < s.size())
                return &s[k];
            k -= s.size();
        }
        return nullptr;
    }

    std::string expr(int depth) {
        if (depth <= 0 || chance(35)) {
            switch (irand(0, 9)) {
            case 0:
                return std::to_string(irand(-3, 3));
            case 1:
                return std::to_string(irand(0, 255));
            case 2:
                return chance(50) ? "2000000000" : "-1999999999";
            case 3: {
                const std::string *v = pick_scalar();
                return v ? *v : std::to_string(irand(0, 9));
            }
            case 4:
                return "r" + std::to_string(irand(0, 3));
            case 5:
                return chance(50) ? "input()" : "rand()";
            case 6:
                return "child.length";
            case 7: {
                const auto *a = pick_array();
                if (!a)
                    return std::to_string(irand(0, 9));
                return a->first + "[" +
                       std::to_string(irand(0, a->second - 1)) + "]";
            }
            case 8:
                return "mem[" + std::to_string(irand(50000, 50040)) + "]";
            default: {
                const std::string *v = pick_scalar();
                return v ? *v : "1";
            }
            }
        }
        switch (irand(0, 12)) {
        case 0:
            return "(-" + expr(depth - 1) + ")";
        case 1:
            return "(!" + expr(depth - 1) + ")";
        case 2: {
            const auto *a = pick_array();
            if (a) {
                const std::string n = std::to_string(a->second);
                return a->first + "[(" + expr(depth - 1) + " % " + n + " + " +
                       n + ") % " + n + "]";
            }
            return expr(depth - 1);
        }
        case 3:
            if (current_fn + 1 < fn_count || (current_fn == -1 && fn_count))
                return call_expr(depth - 1);
            return expr(depth - 1);
        default: {
            static const char *const ops[] = {
                "+",  "-",  "*",  "/",  "%",  "<", ">",
                "<=", ">=", "==", "!=", "&&", "||"};
            const char *op = ops[irand(0, 12)];
            return "(" + expr(depth - 1) + " " + op + " " + expr(depth - 1) +
                   ")";
        }
        }
    }

    std::vector<int> fn_params;

    std::string call_expr(int depth) {
        const int lo = current_fn + 1;
        const int f = irand(lo, fn_count - 1);
        std::string s = "f" + std::to_string(f) + "(";
        for (int i = 0; i < fn_params[static_cast<std::size_t>(f)]; ++i) {
            if (i)
                s += ", ";
            s += expr(depth);
        }
        return s + ")";
    }

    std::string rand_string() {
        static const char cs[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
            "!?.,:;<>[]{}()*&^%$#@~-_=+";
        std::string s = "\"";
        const int n = irand(0, 12);
        for (int i = 0; i < n; ++i) {
            const char c = cs[static_cast<std::size_t>(
                irand(0, static_cast<int>(sizeof cs) - 2))];
            if (c == '"' || c == '\\')
                s += '\\';
            s += c;
        }
        return s + "\"";
    }

    void stmt(int depth, int indent) {
        const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
        switch (irand(0, 17)) {
        case 0: {
            std::string v = fresh("v");
            out += pad + "var " + v +
                   (chance(70) ? " = " + expr(depth) : std::string()) + ";\n";
            scalars.back().push_back(v);
            return;
        }
        case 1: {
            std::string a = fresh("a");
            const int len = irand(2, 12);
            out += pad + "var " + a + "[" + std::to_string(len) + "];\n";
            for (int k = 0; k < len; ++k) {
                out += pad + a + "[" + std::to_string(k) +
                       "] = " + std::to_string(irand(0, 200)) + ";\n";
            }
            arrays.back().emplace_back(a, len);
            return;
        }
        case 2: {
            const std::string *v = pick_scalar();
            if (!v)
                return stmt(depth, indent);
            out += pad + *v + " = " + expr(depth) + ";\n";
            return;
        }
        case 3:
            out += pad + "r" + std::to_string(irand(0, 3)) + " = " +
                   expr(depth) + ";\n";
            return;
        case 4: {
            const auto *a = pick_array();
            if (!a)
                return stmt(depth, indent);
            const std::string n = std::to_string(a->second);
            out += pad + a->first + "[(" + expr(depth) + " % " + n + " + " + n +
                   ") % " + n + "] = " + expr(depth) + ";\n";
            return;
        }
        case 5:
            out += pad + "mem[" + std::to_string(irand(50000, 50040)) +
                   "] = " + expr(depth) + ";\n";
            return;
        case 6:
            out += pad + (chance(50) ? "print(" : "putchar(") + expr(depth) +
                   ");\n";
            return;
        case 7:
            out += pad + "puts(" + rand_string() + ");\n";
            return;
        case 8:
            if (chance(70))
                out += pad + "emit(" + expr(depth) + ");\n";
            else
                out += pad + "spawn();\n";
            return;
        case 9: {
            out += pad + "child[" + expr(depth) + "] = " + expr(depth) + ";\n";
            return;
        }
        case 10: {
            out += pad + "if (" + expr(depth) + ") {\n";
            block(depth - 1, indent + 1, irand(0, 3));
            if (chance(50)) {
                out += pad + "} else {\n";
                block(depth - 1, indent + 1, irand(0, 3));
            }
            out += pad + "}\n";
            return;
        }
        case 11: {
            if (loop_depth >= 4)
                return stmt(depth, indent);
            const std::string c = fresh("lc");
            out += pad + "var " + c + " = 0;\n";
            out += pad + "while (" + c + " < " + std::to_string(irand(1, 6)) +
                   ") {\n";
            out += pad + "  " + c + " = " + c + " + 1;\n";
            ++loop_depth;
            block(depth - 1, indent + 1, irand(0, 3));
            --loop_depth;
            out += pad + "}\n";
            return;
        }
        case 12: {
            if (loop_depth >= 4)
                return stmt(depth, indent);
            const std::string c = fresh("i");
            out += pad + "for (var " + c + " = 0; " + c + " < " +
                   std::to_string(irand(1, 6)) + "; " + c + " = " + c +
                   " + 1) {\n";
            ++loop_depth;
            block(depth - 1, indent + 1, irand(0, 3));
            --loop_depth;
            out += pad + "}\n";
            return;
        }
        case 13: {
            if (loop_depth >= 4)
                return stmt(depth, indent);
            const std::string c = fresh("dc");
            out += pad + "var " + c + " = 0;\n";
            out += pad + "do {\n";
            out += pad + "  " + c + " = " + c + " + 1;\n";
            ++loop_depth;
            block(depth - 1, indent + 1, irand(0, 2));
            --loop_depth;
            out += pad + "} while (" + c + " < " + std::to_string(irand(1, 4)) +
                   ");\n";
            return;
        }
        case 14:
            if (loop_depth > 0) {
                out += pad + "break;\n";
                return;
            }
            return stmt(depth, indent);
        case 15: {
            out += pad + "{\n";
            block(depth - 1, indent + 1, irand(1, 3));
            out += pad + "}\n";
            return;
        }
        case 16:
            out += pad + expr(depth) + ";\n";
            return;
        default: {
            const auto *a = pick_array();
            if (!a || a->second < 2)
                return stmt(depth, indent);
            for (int k = 0; k + 1 < a->second; ++k) {
                out += pad + a->first + "[" + std::to_string(k) +
                       "] = " + std::to_string(irand(33, 126)) + ";\n";
            }
            out += pad + a->first + "[" + std::to_string(a->second - 1) +
                   "] = 0;\n";
            out += pad + "puts(" + a->first + ");\n";
            return;
        }
        }
    }

    void block(int depth, int indent, int stmts) {
        push_scope();
        for (int i = 0; i < stmts; ++i)
            stmt(depth, indent);
        pop_scope();
    }

    std::string generate() {
        fn_count = irand(0, 3);
        fn_params.assign(static_cast<std::size_t>(fn_count), 0);
        for (int f = 0; f < fn_count; ++f)
            fn_params[static_cast<std::size_t>(f)] = irand(0, 3);

        out.clear();
        current_fn = -1;
        push_scope();
        const int top = irand(1, 10);
        for (int i = 0; i < top; ++i)
            stmt(3, 0);
        pop_scope();

        for (int f = 0; f < fn_count; ++f) {
            current_fn = f;
            out += "\nfn f" + std::to_string(f) + "(";
            push_scope();
            for (int p = 0; p < fn_params[static_cast<std::size_t>(f)]; ++p) {
                std::string pn = fresh("p");
                out += (p ? ", " : "") + pn;
                scalars.back().push_back(pn);
            }
            out += ") {\n";
            const int body = irand(1, 5);
            for (int i = 0; i < body; ++i)
                stmt(2, 1);
            if (chance(60))
                out += "  return " + expr(2) + ";\n";
            pop_scope();
            out += "}\n";
        }
        current_fn = -1;
        return out;
    }
};

static bool same_behaviour(const Result &a, const Result &b, std::string &why) {
    if (a.halt != b.halt) {
        why = "halt";
        return false;
    }
    if (a.output != b.output) {
        why = "output";
        return false;
    }
    if (a.registers != b.registers) {
        why = "registers";
        return false;
    }
    if (a.inputs_read != b.inputs_read) {
        why = "inputs_read";
        return false;
    }
    if (a.offspring != b.offspring) {
        why = "offspring";
        return false;
    }
    if (a.uncommitted_child_genes != b.uncommitted_child_genes) {
        why = "uncommitted_child_genes";
        return false;
    }
    return true;
}

static int mode_savvy(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::size_t failures = 0, skipped = 0;
    for (std::size_t i = 0; i < count; ++i) {
        SavvyGen gen(rng());
        const std::string src = gen.generate();

        savvy::Error err;
        const auto g = savvy::compile(src, &err);
        if (!g) {
            std::printf("== GENERATOR/COMPILE FAILURE case %zu ==\n%s\n-- %s\n",
                        i, src.c_str(), err.format().c_str());
            return 1;
        }

        Limits lim;
        lim.max_steps = 20'000'000;
        VM vm(lim);
        const std::vector<Value> input = random_input(rng);
        const std::uint64_t vmseed = rng();
        const Result r1 = vm.run(*g, input, vmseed);
        if (r1.halt != Halt::Completed) {
            ++skipped;
            continue;
        }

        const std::string dec = savvy::decompile(*g);
        const auto g2 = savvy::compile(dec, &err);
        if (!g2) {
            std::printf("== DECOMPILED DOES NOT COMPILE case %zu ==\n%s\n---\n"
                        "%s\n-- %s\n",
                        i, src.c_str(), dec.c_str(), err.format().c_str());
            ++failures;
            continue;
        }
        const Result r2 = vm.run(*g2, input, vmseed);
        std::string why;
        if (!same_behaviour(r1, r2, why)) {
            std::printf("== BEHAVIOUR DIVERGES (%s) case %zu ==\n%s\n--- "
                        "decompiled:\n%s\n",
                        why.c_str(), i, src.c_str(), dec.c_str());
            ++failures;
            if (failures >= 5)
                return 1;
        }
    }
    std::printf("savvy round-trip: %zu cases, %zu skipped, %zu failures\n",
                count, skipped, failures);
    return failures ? 1 : 0;
}

static bool uses_self_inspection(const Genome &g) {
    for (std::size_t i = 0; i < g.size();) {
        const Op op = decode(g[i]);
        if (op == Op::GLEN || op == Op::GREAD)
            return true;
        i += static_cast<std::size_t>(op_size(op));
    }
    return false;
}

static void show_result(const char *tag, const Result &r) {
    std::printf("%s: halt=%s steps=%zu inputs_read=%zu offspring=%zu "
                "uncommitted=%zu regs=[%lld %lld %lld %lld]\n  output: ",
                tag, to_string(r.halt), r.steps, r.inputs_read,
                r.offspring.size(), r.uncommitted_child_genes,
                static_cast<long long>(r.registers[0]),
                static_cast<long long>(r.registers[1]),
                static_cast<long long>(r.registers[2]),
                static_cast<long long>(r.registers[3]));
    for (char c : r.output)
        std::printf("%c", (c >= 32 && c < 127) ? c : '.');
    std::printf("\n");
}

static int mode_decomp(std::size_t count, std::uint64_t seed,
                       std::size_t repro = static_cast<std::size_t>(-1)) {
    std::mt19937_64 rng(seed);
    std::size_t skipped = 0, diverged = 0, shown = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Genome g = random_genome_any(rng);
        if (uses_self_inspection(g)) {
            ++skipped;
            continue;
        }
        Limits lim;
        lim.max_steps = 20'000;
        lim.max_child_genes = 100'000;
        VM vm(lim);
        const std::vector<Value> input = random_input(rng);
        const std::uint64_t vmseed = rng();
        const Result r1 = vm.run(g, input, vmseed);
        if (r1.halt != Halt::Completed) {
            ++skipped;
            continue;
        }

        const std::string dec = savvy::decompile(g);
        savvy::Error err;
        const auto g2 = savvy::compile(dec, &err);
        if (!g2) {
            std::printf("== DECOMPILED DOES NOT COMPILE case %zu ==\n%s\n", i,
                        err.format().c_str());
            return 1;
        }
        Limits lim2 = lim;
        lim2.max_steps = 50 * r1.steps + 100'000;
        VM vm2(lim2);
        const Result r2 = vm2.run(*g2, input, vmseed);
        std::string why;
        if (!same_behaviour(r1, r2, why)) {
            ++diverged;
            if (repro == static_cast<std::size_t>(-1) && ++shown <= 8) {
                std::printf("case %zu diverges (%s): genome %s\n", i,
                            why.c_str(), to_gene_list(g).c_str());
            }
        }
        if (i == repro) {
            std::printf("=== case %zu ===\ngenome: %s\ninput:", i,
                        to_gene_list(g).c_str());
            for (Value v : input)
                std::printf(" %lld", static_cast<long long>(v));
            std::printf("\nseed: %llu\n\n--- assembly:\n%s\n--- decompiled:\n"
                        "%s\n",
                        static_cast<unsigned long long>(vmseed),
                        to_assembly(g).c_str(), dec.c_str());
            show_result("original  ", r1);
            show_result("decompiled", r2);
            return 0;
        }
    }
    std::printf("decomp: %zu cases, %zu skipped, %zu diverged\n", count,
                skipped, diverged);
    return 0;
}

static int mode_parser(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    static const char *const bits[] = {
        "var",
        "if",
        "else",
        "while",
        "do",
        "for",
        "fn",
        "break",
        "return",
        "{",
        "}",
        "(",
        ")",
        "[",
        "]",
        ";",
        ",",
        "=",
        "==",
        "!=",
        "<=",
        ">=",
        "&&",
        "||",
        "+",
        "-",
        "*",
        "/",
        "%",
        "!",
        "<",
        ">",
        "x",
        "y",
        "mem",
        "self",
        "child",
        ".",
        "length",
        "input",
        "rand",
        "puts",
        "print",
        "putchar",
        "emit",
        "spawn",
        "r0",
        "r3",
        "0",
        "1",
        "42",
        "999999999999999999999999",
        "\"s\"",
        "'a'",
        "\"unterminated",
        "'",
        "\\",
        "\x01",
        "\xff",
        "/*",
        "*/",
        "//",
        "\n",
        "9223372036854775807",
        "_",
    };
    std::size_t compiled = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::string src;
        const int n = std::uniform_int_distribution<int>(0, 60)(rng);
        for (int k = 0; k < n; ++k) {
            src += bits[std::uniform_int_distribution<std::size_t>(
                0, std::size(bits) - 1)(rng)];
            src += ' ';
        }
        savvy::Error err;
        if (auto g = savvy::compile(src, &err)) {
            ++compiled;
            savvy::decompile(*g);
        }
    }
    std::printf("parser: %zu cases survived, %zu compiled\n", count, compiled);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: fuzz <vmhash|savvy|decomp|parser> "
                             "<count> <seed> [repro-case]\n");
        return 2;
    }
    const std::size_t count = std::strtoull(argv[2], nullptr, 10);
    const std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);
    if (argc == 5 && !std::strcmp(argv[1], "decomp"))
        return mode_decomp(count, seed, std::strtoull(argv[4], nullptr, 10));
    if (!std::strcmp(argv[1], "vmhash"))
        return mode_vmhash(count, seed);
    if (!std::strcmp(argv[1], "savvy"))
        return mode_savvy(count, seed);
    if (!std::strcmp(argv[1], "decomp"))
        return mode_decomp(count, seed);
    if (!std::strcmp(argv[1], "parser"))
        return mode_parser(count, seed);
    std::fprintf(stderr, "unknown mode\n");
    return 2;
}
