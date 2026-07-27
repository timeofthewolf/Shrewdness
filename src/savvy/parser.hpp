#pragma once

#include "savvy/ast.hpp"
#include "savvy/savvy.hpp"

#include <string>
#include <string_view>

namespace savvy {

struct ParseError {
    std::size_t line = 0, column = 0;
    std::string message;
    std::size_t file = static_cast<std::size_t>(-1);
};

Program parse(std::string_view src);

void parse_into(Program &p, std::string_view src, const Resolver &resolver,
                const std::string &entry_key);

} // namespace savvy
