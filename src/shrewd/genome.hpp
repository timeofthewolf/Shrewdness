#pragma once

#include "shrewd/isa.hpp"

#include <cstddef>
#include <random>
#include <vector>

namespace shrewd {

using Genome = std::vector<Gene>;

inline constexpr Gene kRandomGeneMax = 4 * kOpCount - 1;

Genome random_genome(std::size_t length, std::mt19937_64 &rng);

struct MutationRates {
    double substitution = 0.010;
    double insertion = 0.002;
    double deletion = 0.002;

    double duplication = 0.05;
    double inversion = 0.02;

    double nudge_share = 0.60;

    std::size_t max_length = 0;
};

Genome mutate(const Genome &g, const MutationRates &rates,
              std::mt19937_64 &rng);

Genome crossover(const Genome &a, const Genome &b, std::mt19937_64 &rng);

} // namespace shrewd
