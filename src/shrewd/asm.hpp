#pragma once

#include "shrewd/genome.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace shrewd {

std::string to_gene_list(const Genome &g);

std::string to_assembly(const Genome &g);

std::optional<Genome> assemble(std::string_view src,
                               std::string *error = nullptr);

std::string write_genes(const Genome &g);
std::optional<Genome> read_genes(std::string_view src,
                                 std::string *error = nullptr);

} // namespace shrewd
