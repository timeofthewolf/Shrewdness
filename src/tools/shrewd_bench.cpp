#include "savvy/savvy.hpp"
#include "shrewd/shrewd.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace shrewd;
using Clock = std::chrono::steady_clock;

namespace {

Genome must_compile(std::string_view src) {
    savvy::Error err;
    if (auto g = savvy::compile(src, &err))
        return *g;
    std::cerr << err.format("<bench>") << "\n";
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
    return must_compile(ss.str());
}

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::uint64_t g_sink = 0;

void bench(const std::string &name, const Genome &g,
           const std::vector<Value> &input, const Limits &limits) {
    VM vm(limits);

    const Result probe = vm.run(g, input, 1);
    const std::size_t steps = probe.steps;
    g_sink += probe.output.size() + probe.offspring.size();

    const auto t0 = Clock::now();
    vm.run(g, input, 1);
    const double once = std::max(seconds_since(t0), 1e-9);
    const std::size_t iters =
        std::max<std::size_t>(1, static_cast<std::size_t>(0.4 / once));

    double best = 1e100;
    for (int rep = 0; rep < 3; ++rep) {
        const auto t = Clock::now();
        for (std::size_t i = 0; i < iters; ++i) {
            const Result r = vm.run(g, input, 1);
            g_sink += r.steps;
        }
        best = std::min(best, seconds_since(t));
    }

    const double per_run = best / static_cast<double>(iters);
    const double per_step = per_run / static_cast<double>(steps);
    std::cout << "  " << std::left << std::setw(18) << name << std::right
              << std::setw(8) << g.size() << " genes" << std::setw(12) << steps
              << " steps" << std::setw(11) << std::fixed << std::setprecision(2)
              << per_run * 1e6 << " us/run" << std::setw(9)
              << std::setprecision(2) << per_step * 1e9 << " ns/step"
              << std::setw(9) << std::setprecision(1) << 1.0 / per_step / 1e6
              << " Mstep/s\n";
}

void bench_random() {
    std::mt19937_64 rng(20260715);
    std::uniform_int_distribution<std::size_t> len(0, 200);

    constexpr std::size_t kCount = 20'000;
    std::vector<Genome> pop;
    pop.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
        pop.push_back(random_genome(len(rng), rng));

    Limits budget;
    budget.max_steps = 10'000;
    budget.stack_limit = 1'000'000;
    budget.output_limit = 4096;
    budget.max_child_genes = 100'000;
    budget.max_offspring = 8;
    VM vm(budget);

    const auto report = [](const char *name, std::size_t count,
                           std::uint64_t steps, double best) {
        std::cout << "  " << std::left << std::setw(18) << name << std::right
                  << std::setw(8) << count << " runs " << std::setw(12) << steps
                  << " steps" << std::setw(11) << std::fixed
                  << std::setprecision(2)
                  << best * 1e6 / static_cast<double>(count) << " us/run"
                  << std::setw(9) << std::setprecision(2)
                  << best * 1e9 / static_cast<double>(steps) << " ns/step"
                  << std::setw(9) << std::setprecision(1)
                  << static_cast<double>(steps) / best / 1e6 << " Mstep/s\n";
    };

    double best = 1e100;
    std::uint64_t steps = 0;
    for (int rep = 0; rep < 3; ++rep) {
        steps = 0;
        const auto t = Clock::now();
        for (std::size_t i = 0; i < kCount; ++i) {
            const Result r = vm.run(pop[i], {}, i);
            steps += r.steps;
        }
        best = std::min(best, seconds_since(t));
        g_sink += steps;
    }
    report("random-200", kCount, steps, best);

    best = 1e100;
    Result reused;
    for (int rep = 0; rep < 3; ++rep) {
        steps = 0;
        const auto t = Clock::now();
        for (std::size_t i = 0; i < kCount; ++i) {
            vm.run_into(reused, pop[i], {}, i);
            steps += reused.steps;
        }
        best = std::min(best, seconds_since(t));
        g_sink += steps;
    }
    report("random-200 reuse", kCount, steps, best);
}

} // namespace

int main() {
    std::cout << "shrewd_bench -- lower is better, best of 3\n\n";

    const Limits open{};

    bench("tiny-hello",
          *assemble("PUSHI 72 OUT PUSHI 101 OUT PUSHI 108 OUT PUSHI 108 OUT "
                    "PUSHI 111 OUT PUSHI 44 OUT PUSHI 32 OUT PUSHI 87 OUT "
                    "PUSHI 111 OUT PUSHI 114 OUT PUSHI 108 OUT PUSHI 100 OUT"),
          {}, open);

    bench("loop-var",
          must_compile("var n = 0; while (n < 200000) { n = n + 1; }"), {},
          open);

    bench("loop-reg",
          must_compile("r0 = 0; while (r0 < 200000) { r0 = r0 + 1; }"), {},
          open);

    bench("fib-rec",
          must_compile("fn f(n) { if (n < 2) { return n; } "
                       "return f(n - 1) + f(n - 2); } r0 = f(22);"),
          {}, open);

    bench("replicator", must_compile_file("replicator.savvy"), {}, open);

    bench("brainfuck", must_compile_file("brainfuck.savvy"), {}, open);

    bench_random();

    std::cout << "\n(checksum " << g_sink << ")\n";
    return 0;
}
