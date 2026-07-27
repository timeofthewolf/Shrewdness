#pragma once

#include "shrewd/genome.hpp"
#include "shrewd/io.hpp"
#include "shrewd/isa.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shrewd {

struct Limits {
    std::size_t max_steps = 0;
    std::size_t stack_limit = 0;
    std::size_t output_limit = 0;
    std::size_t max_call_depth = 0;
    std::size_t max_child_genes = 0;
    std::size_t max_offspring = 0;

    std::size_t memory_size = 1u << 16;
};

enum class Halt {
    Completed,
    OutOfGas,
};

const char *to_string(Halt h);

struct Result {
    Halt halt = Halt::Completed;

    std::size_t steps = 0;

    std::string output;
    std::array<Value, kRegisterCount> registers{};
    std::vector<Value> stack;
    std::size_t inputs_read = 0;

    std::vector<Genome> offspring;

    std::size_t uncommitted_child_genes = 0;
};

struct TraceStep {
    std::uint32_t pc = 0;
    std::uint8_t op = 0;
    std::uint32_t depth = 0;
    std::uint32_t out_len = 0;
    std::array<Value, kRegisterCount> regs{};
    std::vector<Value> stack;
    std::int64_t wrote_addr = -1;
    Value wrote_val = 0;
};

inline constexpr std::size_t kNoJump = static_cast<std::size_t>(-1);

inline constexpr std::size_t kReturnHere = static_cast<std::size_t>(-2);

struct ControlMap {
    std::vector<std::size_t> jump;

    std::vector<std::size_t> procs;

    std::vector<std::uint8_t> ops;

    void clear() {
        jump.clear();
        procs.clear();
        ops.clear();
    }

    struct OpenBlock {
        Op kind;
        std::size_t index;
        std::size_t else_index;
    };
    std::vector<OpenBlock> open_scratch;
    std::vector<std::pair<std::size_t, std::size_t>> break_scratch;
};

ControlMap build_control_map(const Genome &g);

void build_control_map_into(const Genome &g, ControlMap &out);

class VM {
  public:
    explicit VM(Limits limits = {});

    Result run(const Genome &g, const std::vector<Value> &input = {},
               std::uint64_t seed = 0);

    Result run(const Genome &g, Io &io, std::uint64_t seed = 0);

    void run_into(Result &out, const Genome &g,
                  const std::vector<Value> &input = {}, std::uint64_t seed = 0);

    void run_into(Result &out, const Genome &g, Io &io, std::uint64_t seed = 0);

    const Limits &limits() const { return limits_; }

    void set_limits(const Limits &l) {
        limits_ = l;
        if (limits_.memory_size == 0)
            limits_.memory_size = 1;
    }

    void set_trace(std::vector<TraceStep> *sink, std::size_t cap = 8192,
                   std::size_t depth = 64) {
        trace_ = sink;
        trace_cap_ = cap;
        trace_depth_ = depth;
    }

  private:
    Limits limits_;

    std::vector<TraceStep> *trace_ = nullptr;
    std::size_t trace_cap_ = 0;
    std::size_t trace_depth_ = 64;

    ControlMap cm_;
    std::vector<Value> stack_;
    std::vector<std::size_t> returns_;
    Genome child_;
    std::vector<Genome> genome_pool_;

    std::vector<Value> mem_;
    std::vector<std::uint32_t> mem_stamp_;
    std::vector<std::size_t> mem_dirty_;
    std::uint32_t run_stamp_ = 0;
};

} // namespace shrewd
