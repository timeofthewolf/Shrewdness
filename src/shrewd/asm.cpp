#include "shrewd/asm.hpp"

#include "shrewd/isa.hpp"
#include "shrewd/vm.hpp"

#include <cctype>
#include <charconv>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace shrewd {
namespace {

std::string literal_at(const Genome &g, std::size_t pc) {
    const long long v =
        (pc + 1 < g.size()) ? static_cast<long long>(g[pc + 1]) : 0LL;
    return std::to_string(v);
}

template <typename OnToken>
bool tokenize(std::string_view src, std::string *error, OnToken &&on_token) {
    std::size_t line = 1;
    std::size_t i = 0;
    const std::size_t n = src.size();

    while (i < n) {
        const char c = src[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == ';' || c == '#' ||
            (c == '/' && i + 1 < n && src[i + 1] == '/')) {
            while (i < n && src[i] != '\n')
                ++i;
            continue;
        }

        const std::size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(src[i])))
            ++i;
        if (!on_token(src.substr(start, i - start), line, error))
            return false;
    }
    return true;
}

bool parse_gene(std::string_view tok, Gene &out) {
    long long v = 0;
    const auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size())
        return false;
    if (v < std::numeric_limits<Gene>::min() ||
        v > std::numeric_limits<Gene>::max())
        return false;
    out = static_cast<Gene>(v);
    return true;
}

} // namespace

std::string to_gene_list(const Genome &g) {
    std::string s = "[";
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (i)
            s += ", ";
        s += std::to_string(g[i]);
    }
    s += "]";
    return s;
}

std::string to_assembly(const Genome &g) {
    const ControlMap cm = build_control_map(g);
    const std::vector<std::size_t> &jump = cm.jump;

    std::vector<Op> blocks;
    std::string out;
    int indent = 0;

    auto emit = [&](const std::string &s) {
        out.append(static_cast<std::size_t>(indent) * 2, ' ');
        out += s;
        out += '\n';
    };

    auto canonical = [&](std::size_t pc) {
        return g[pc] == static_cast<Gene>(decode(g[pc]));
    };
    auto emit_raw = [&](std::size_t pc, const std::string &note) {
        std::string s = std::to_string(g[pc]);
        if (decode(g[pc]) == Op::PUSHI && pc + 1 < g.size()) {
            s += " " + std::to_string(g[pc + 1]);
        }
        s += ' ';
        while (s.size() < 16)
            s += ' ';
        emit(s + "; " + note);
    };

    for (std::size_t pc = 0; pc < g.size();) {
        const Op op = decode(g[pc]);

        if (!canonical(pc)) {
            switch (op) {
            case Op::IF:
            case Op::DO:
            case Op::PROC:
                emit_raw(pc, std::string(info(op).mnemonic));
                blocks.push_back(op);
                ++indent;
                break;
            case Op::END:
                if (!blocks.empty()) {
                    blocks.pop_back();
                    if (indent)
                        --indent;
                }
                emit_raw(pc, "END");
                break;
            default:
                emit_raw(pc, std::string(info(op).mnemonic));
                break;
            }
            pc += static_cast<std::size_t>(op_size(op));
            continue;
        }

        switch (op) {
        case Op::IF:
        case Op::DO:
        case Op::PROC:
            emit(std::string(info(op).mnemonic));
            blocks.push_back(op);
            ++indent;
            break;

        case Op::ELSE:
            if (jump[pc] != kNoJump) {
                if (indent)
                    --indent;
                emit("ELSE");
                ++indent;
            } else {
                emit("ELSE            ; inert");
            }
            break;

        case Op::END:
            if (!blocks.empty()) {
                blocks.pop_back();
                if (indent)
                    --indent;
                emit("END");
            } else {
                emit("END             ; inert");
            }
            break;

        case Op::BREAK:
            emit(jump[pc] != kNoJump ? "BREAK" : "BREAK           ; inert");
            break;

        case Op::PUSHI:
            emit(pc + 1 < g.size() ? "PUSHI " + literal_at(g, pc) : "PUSHI");
            break;

        default:
            emit(std::string(info(op).mnemonic));
            break;
        }

        pc += static_cast<std::size_t>(op_size(op));
    }

    return out;
}

std::optional<Genome> assemble(std::string_view src, std::string *error) {
    static const std::unordered_map<std::string, Gene> kMnemonics = [] {
        std::unordered_map<std::string, Gene> m;
        for (int i = 0; i < kOpCount; ++i) {
            m.emplace(std::string(info(static_cast<Op>(i)).mnemonic),
                      static_cast<Gene>(i));
        }
        return m;
    }();

    Genome out;
    const bool ok =
        tokenize(src, error,
                 [&](std::string_view tok, std::size_t line, std::string *err) {
                     std::string upper(tok);
                     for (char &ch : upper)
                         ch = static_cast<char>(
                             std::toupper(static_cast<unsigned char>(ch)));

                     if (const auto it = kMnemonics.find(upper);
                         it != kMnemonics.end()) {
                         out.push_back(it->second);
                         return true;
                     }
                     Gene g = 0;
                     if (parse_gene(tok, g)) {
                         out.push_back(g);
                         return true;
                     }
                     if (err)
                         *err = "line " + std::to_string(line) +
                                ": unknown token '" + std::string(tok) + "'";
                     return false;
                 });

    if (!ok)
        return std::nullopt;
    return out;
}

std::string write_genes(const Genome &g) {
    std::string s;
    for (std::size_t i = 0; i < g.size(); ++i) {
        s += std::to_string(g[i]);
        s += ((i + 1) % 20 == 0) ? '\n' : ' ';
    }
    if (!s.empty() && s.back() == ' ')
        s.back() = '\n';
    return s;
}

std::optional<Genome> read_genes(std::string_view src, std::string *error) {
    Genome out;
    const bool ok =
        tokenize(src, error,
                 [&](std::string_view tok, std::size_t line, std::string *err) {
                     Gene g = 0;
                     if (parse_gene(tok, g)) {
                         out.push_back(g);
                         return true;
                     }
                     if (err)
                         *err = "line " + std::to_string(line) +
                                ": not a gene: '" + std::string(tok) + "'";
                     return false;
                 });

    if (!ok)
        return std::nullopt;
    return out;
}

} // namespace shrewd
