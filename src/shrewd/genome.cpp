#include "shrewd/genome.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace shrewd {
namespace {

Gene random_gene(std::mt19937_64 &rng) {
    std::uniform_int_distribution<int> d(0, static_cast<int>(kRandomGeneMax));
    return static_cast<Gene>(d(rng));
}

bool chance(double p, std::mt19937_64 &rng) {
    if (p <= 0.0)
        return false;
    if (p >= 1.0)
        return true;
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng) < p;
}

Gene nudge(Gene g, std::int32_t delta) {
    return static_cast<Gene>(static_cast<std::uint32_t>(g) +
                             static_cast<std::uint32_t>(delta));
}

std::pair<std::size_t, std::size_t> random_segment(std::size_t n,
                                                   std::mt19937_64 &rng) {
    std::uniform_int_distribution<std::size_t> d(0, n - 1);
    std::size_t a = d(rng);
    std::size_t b = d(rng);
    if (a > b)
        std::swap(a, b);
    return {a, b + 1};
}

} // namespace

Genome random_genome(std::size_t length, std::mt19937_64 &rng) {
    Genome g(length);
    for (Gene &x : g)
        x = random_gene(rng);
    return g;
}

Genome mutate(const Genome &g, const MutationRates &rates,
              std::mt19937_64 &rng) {
    const std::size_t cap = rates.max_length
                                ? rates.max_length
                                : std::numeric_limits<std::size_t>::max();
    Genome out;
    out.reserve(g.size() + 8);

    for (Gene gene : g) {
        if (chance(rates.insertion, rng))
            out.push_back(random_gene(rng));
        if (chance(rates.deletion, rng))
            continue;

        if (chance(rates.substitution, rng)) {
            if (chance(rates.nudge_share, rng)) {
                gene = nudge(gene, chance(0.5, rng) ? 1 : -1);
            } else {
                gene = random_gene(rng);
            }
        }
        out.push_back(gene);
    }

    if (!out.empty() && out.size() < cap && chance(rates.duplication, rng)) {
        auto [lo, hi] = random_segment(out.size(), rng);
        std::size_t len = std::min(hi - lo, cap - out.size());
        if (len > 0) {
            const Genome seg(out.begin() + static_cast<std::ptrdiff_t>(lo),
                             out.begin() +
                                 static_cast<std::ptrdiff_t>(lo + len));
            std::uniform_int_distribution<std::size_t> at(0, out.size());
            const std::size_t where = at(rng);
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(where),
                       seg.begin(), seg.end());
        }
    }

    if (out.size() > 1 && chance(rates.inversion, rng)) {
        auto [lo, hi] = random_segment(out.size(), rng);
        std::reverse(out.begin() + static_cast<std::ptrdiff_t>(lo),
                     out.begin() + static_cast<std::ptrdiff_t>(hi));
    }

    if (out.size() > cap)
        out.resize(cap);

    return out;
}

Genome crossover(const Genome &a, const Genome &b, std::mt19937_64 &rng) {
    std::uniform_int_distribution<std::size_t> da(0, a.size());
    std::uniform_int_distribution<std::size_t> db(0, b.size());
    const std::size_t ca = da(rng);
    const std::size_t cb = db(rng);

    Genome out(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(ca));
    out.insert(out.end(), b.begin() + static_cast<std::ptrdiff_t>(cb), b.end());
    return out;
}

} // namespace shrewd
