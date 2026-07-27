#pragma once

#include "shrewd/genome.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace savvy {

struct Error {
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;

    std::string file;

    std::string format(std::string_view filename = "<input>") const;
};

struct Source {
    std::string key;
    std::string text;
};

using Resolver = std::function<std::optional<Source>(const std::string &name,
                                                     const std::string &from)>;

struct Options {
    Resolver resolver;
    std::string entry;
};

std::optional<shrewd::Genome> compile(std::string_view src,
                                      Error *error = nullptr);

std::optional<shrewd::Genome> compile(std::string_view src, const Options &opts,
                                      Error *error = nullptr);

std::string decompile(const shrewd::Genome &g);

} // namespace savvy
