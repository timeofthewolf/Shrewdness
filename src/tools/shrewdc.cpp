#include "savvy/savvy.hpp"
#include "shrewd/asm.hpp"
#include "shrewd/shrewd.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace shrewd;

namespace {

const char *kUsage = R"(shrewdc -- the Shrewd toolchain

usage:
  shrewdc build  <file.savvy> [-o out.shrewd]   compile Savvy to a genome
  shrewdc run    <file.savvy|file.shrewd>       compile if needed, then run
  shrewdc asm    <file.savvy|file.shrewd>       show Shrewd assembly
  shrewdc savvy  <file.savvy|file.shrewd>       decompile a genome to Savvy
  shrewdc genes  <file.savvy|file.shrewd>       show the raw gene list

run options:
  --seed N          seed for rand() (default 0; runs are reproducible per seed)
  --steps N         stop after N steps (default: no limit -- loops run forever)
  --trace           report steps, halt reason and offspring on stderr
  --offspring DIR   write each committed child to DIR/childN.shrewd
                    (created if needed, overwritten if present)

`run` reads stdin and feeds it to input() one character code at a time.
)";

[[noreturn]] void die(const std::string &msg) {
    std::cerr << "shrewdc: " << msg << "\n";
    std::exit(1);
}

std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        die("cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<savvy::Source> resolve_include(const std::string &name,
                                             const std::string &from) {
    namespace fs = std::filesystem;
    const fs::path base = fs::path(from).parent_path();
    for (const fs::path &cand : {base / name, base / (name + ".savvy")}) {
        std::ifstream f(cand, std::ios::binary);
        if (!f)
            continue;
        std::ostringstream ss;
        ss << f.rdbuf();
        return savvy::Source{cand.lexically_normal().string(), ss.str()};
    }
    return std::nullopt;
}

Genome load(const std::string &path) {
    const std::string src = read_file(path);

    if (ends_with(path, ".savvy")) {
        savvy::Options opts;
        opts.entry = path;
        opts.resolver = resolve_include;
        savvy::Error err;
        if (auto g = savvy::compile(src, opts, &err))
            return *g;
        die(err.format(path));
    }

    std::string err;
    if (auto g = read_genes(src, &err))
        return *g;
    die(path + ": " + err);
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        std::cout << kUsage;
        return args.empty() ? 1 : 0;
    }

    const std::string cmd = args[0];
    std::string path, out_path, offspring_dir;
    std::uint64_t seed = 0;
    std::size_t steps = 0;
    bool trace = false;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string &a = args[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= args.size())
                die(std::string(what) + " needs a value");
            return args[++i];
        };
        if (a == "-o")
            out_path = next("-o");
        else if (a == "--seed")
            seed = std::stoull(next("--seed"));
        else if (a == "--steps")
            steps = std::stoull(next("--steps"));
        else if (a == "--trace")
            trace = true;
        else if (a == "--offspring")
            offspring_dir = next("--offspring");
        else if (!a.empty() && a[0] == '-')
            die("unknown option " + a);
        else if (path.empty())
            path = a;
        else
            die("unexpected argument " + a);
    }
    if (path.empty())
        die("no input file");

    const Genome g = load(path);

    if (cmd == "build") {
        if (out_path.empty()) {
            out_path = ends_with(path, ".savvy")
                           ? path.substr(0, path.size() - 6) + ".shrewd"
                           : path + ".shrewd";
        }
        std::ofstream f(out_path);
        if (!f)
            die("cannot write " + out_path);
        f << write_genes(g);
        std::cerr << out_path << ": " << g.size() << " genes\n";
        return 0;
    }
    if (cmd == "asm") {
        std::cout << to_assembly(g);
        return 0;
    }
    if (cmd == "savvy") {
        std::cout << savvy::decompile(g);
        return 0;
    }
    if (cmd == "genes") {
        std::cout << to_gene_list(g) << "\n";
        return 0;
    }
    if (cmd == "run") {
        Limits limits;
        limits.max_steps = steps;
        VM vm(limits);
        StreamIo io(std::cin, std::cout);
        const Result r = vm.run(g, io, seed);
        std::cout.flush();

        if (!offspring_dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(offspring_dir, ec);
            if (ec)
                die("cannot create " + offspring_dir + ": " + ec.message());
            for (std::size_t i = 0; i < r.offspring.size(); ++i) {
                const std::string p =
                    (std::filesystem::path(offspring_dir) /
                     ("child" + std::to_string(i) + ".shrewd"))
                        .string();
                std::ofstream f(p);
                if (!f)
                    die("cannot write " + p);
                f << write_genes(r.offspring[i]);
                std::cerr << p << ": " << r.offspring[i].size() << " genes\n";
            }
        }

        if (trace) {
            std::cerr << "\n-- " << r.steps << " steps, " << to_string(r.halt)
                      << ", " << r.offspring.size() << " offspring";
            if (r.uncommitted_child_genes) {
                std::cerr << " (" << r.uncommitted_child_genes
                          << " genes emitted but not spawned)";
            }
            std::cerr << "\n";
        }
        return r.halt == Halt::OutOfGas ? 2 : 0;
    }

    die("unknown command '" + cmd + "'");
}
