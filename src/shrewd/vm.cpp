#include "shrewd/vm.hpp"

#include "shrewd/arith.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <utility>

#if !defined(SHREWD_NO_COMPUTED_GOTO) &&                                       \
    (defined(__GNUC__) || defined(__clang__))
#define SHREWD_COMPUTED_GOTO 1
#else
#define SHREWD_COMPUTED_GOTO 0
#endif

namespace shrewd {
namespace {

constexpr std::uint8_t kHaltOp = static_cast<std::uint8_t>(Op::COUNT);
constexpr std::uint8_t kEndLoopOp = kHaltOp + 1;
constexpr std::uint8_t kEndRetOp = kHaltOp + 2;
constexpr std::uint8_t kNopOp = static_cast<std::uint8_t>(Op::NOP);

inline std::size_t wrap_index(Value v, std::size_t n) {
    if (static_cast<std::uint64_t>(v) < static_cast<std::uint64_t>(n))
        [[likely]]
        return static_cast<std::size_t>(v);
    Value m = v % static_cast<Value>(n);
    if (m < 0)
        m += static_cast<Value>(n);
    return static_cast<std::size_t>(m);
}

std::uint64_t next_random(std::uint64_t &state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct ReplayIo final : Io {
    ReplayIo(const std::vector<Value> &input, std::string &output,
             std::size_t output_limit)
        : in_(input.data()), n_(input.size()), out_(&output),
          limit_(output_limit ? output_limit
                              : std::numeric_limits<std::size_t>::max()) {}

    Value read() override { return pos_ < n_ ? in_[pos_++] : kEndOfInput; }
    void put(char c) override {
        if (out_->size() < limit_)
            out_->push_back(c);
    }
    std::size_t inputs_read() const { return pos_; }

  private:
    const Value *in_;
    std::size_t n_;
    std::size_t pos_ = 0;
    std::string *out_;
    std::size_t limit_;
};

} // namespace

const char *to_string(Halt h) {
    switch (h) {
    case Halt::Completed:
        return "completed";
    case Halt::OutOfGas:
        return "out-of-gas";
    }
    return "?";
}

void build_control_map_into(const Genome &g, ControlMap &out) {
    const std::size_t n = g.size();
    out.jump.assign(n, kNoJump);
    out.procs.clear();

    out.ops.resize(n + 2);
    std::uint8_t *const ops = out.ops.data();
    ops[n] = kHaltOp;
    ops[n + 1] = kHaltOp;
    const Gene *const genes = g.data();

    auto &open = out.open_scratch;
    auto &breaks = out.break_scratch;
    open.clear();
    breaks.clear();

    auto close_loop_breaks = [&](std::size_t open_index, std::size_t target) {
        while (!breaks.empty() && breaks.back().first == open_index) {
            out.jump[breaks.back().second] = target;
            breaks.pop_back();
        }
    };

    for (std::size_t i = 0; i < n; ++i)
        ops[i] = static_cast<std::uint8_t>(decode(genes[i]));

    static_assert(static_cast<int>(Op::PROC) - static_cast<int>(Op::IF) == 5,
                  "the walk below assumes IF..PROC are contiguous");
    for (std::size_t i = 0; i < n;) {
        const Op op = static_cast<Op>(ops[i]);
        const std::size_t at = i;
        i += static_cast<std::size_t>(op_size(op));
        if (static_cast<unsigned>(op) - static_cast<unsigned>(Op::IF) > 5u)
            [[likely]]
            continue;

        switch (op) {
        case Op::IF:
            open.push_back({op, at, kNoJump});
            break;

        case Op::DO:
            open.push_back({op, at, kNoJump});
            ops[at] = kNopOp;
            break;

        case Op::PROC:
            open.push_back({op, at, kNoJump});
            out.procs.push_back(at + 1);
            break;

        case Op::ELSE:
            if (!open.empty() && open.back().kind == Op::IF &&
                open.back().else_index == kNoJump) {
                open.back().else_index = at;
            } else {
                ops[at] = kNopOp;
            }
            break;

        case Op::BREAK: {
            bool bound = false;
            for (std::size_t k = open.size(); k-- > 0;) {
                if (open[k].kind == Op::DO) {
                    breaks.emplace_back(k, at);
                    bound = true;
                    break;
                }
            }
            if (!bound)
                ops[at] = kNopOp;
            break;
        }

        case Op::END:
            if (open.empty()) {
                ops[at] = kNopOp;
            } else {
                const ControlMap::OpenBlock o = open.back();
                open.pop_back();
                if (o.kind == Op::IF) {
                    out.jump[o.index] =
                        (o.else_index != kNoJump) ? o.else_index + 1 : at + 1;
                    if (o.else_index != kNoJump)
                        out.jump[o.else_index] = at + 1;
                    ops[at] = kNopOp;
                } else if (o.kind == Op::PROC) {
                    out.jump[o.index] = at + 1;
                    out.jump[at] = kReturnHere;
                    ops[at] = kEndRetOp;
                } else {
                    out.jump[at] = o.index;
                    ops[at] = kEndLoopOp;
                    close_loop_breaks(open.size(), at + 1);
                }
            }
            break;

        default:
            break;
        }
    }

    for (std::size_t k = open.size(); k-- > 0;) {
        const ControlMap::OpenBlock &o = open[k];
        if (o.kind == Op::IF) {
            out.jump[o.index] =
                (o.else_index != kNoJump) ? o.else_index + 1 : n;
            if (o.else_index != kNoJump)
                out.jump[o.else_index] = n;
        } else if (o.kind == Op::PROC) {
            out.jump[o.index] = n;
        } else {
            close_loop_breaks(k, n);
        }
    }
    open.clear();
    breaks.clear();
}

ControlMap build_control_map(const Genome &g) {
    ControlMap cm;
    build_control_map_into(g, cm);
    return cm;
}

VM::VM(Limits limits) : limits_(limits) {
    limits_.memory_size = std::max<std::size_t>(limits_.memory_size, 1);
}

Result VM::run(const Genome &g, const std::vector<Value> &input,
               std::uint64_t seed) {
    Result r;
    run_into(r, g, input, seed);
    return r;
}

Result VM::run(const Genome &g, Io &io, std::uint64_t seed) {
    Result r;
    run_into(r, g, io, seed);
    return r;
}

void VM::run_into(Result &r, const Genome &g, const std::vector<Value> &input,
                  std::uint64_t seed) {
    ReplayIo io(input, r.output, limits_.output_limit);
    run_into(r, g, io, seed);
    r.inputs_read = io.inputs_read();
}

#if SHREWD_COMPUTED_GOTO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

void VM::run_into(Result &r, const Genome &g, Io &io, std::uint64_t seed) {
    r.halt = Halt::Completed;
    r.steps = 0;
    r.output.clear();
    r.registers.fill(0);
    r.stack.clear();
    r.inputs_read = 0;
    for (Genome &og : r.offspring) {
        og.clear();
        if (genome_pool_.size() < 8)
            genome_pool_.push_back(std::move(og));
    }
    r.offspring.clear();
    r.uncommitted_child_genes = 0;
    if (trace_) trace_->clear();

    const std::size_t n = g.size();

    build_control_map_into(g, cm_);
    const std::size_t *const jump = cm_.jump.data();
    const std::size_t *const procs = cm_.procs.data();
    const std::size_t procs_n = cm_.procs.size();
    const std::uint8_t *const ops = cm_.ops.data();
    const Gene *const genes = g.data();

    std::vector<Value> &stack = stack_;
    std::vector<std::size_t> &returns = returns_;
    Genome &child = child_;
    stack.clear();
    returns.clear();
    child.clear();

    if (mem_.size() != limits_.memory_size) {
        mem_.assign(limits_.memory_size, 0);
        mem_stamp_.assign(limits_.memory_size, 0);
        mem_dirty_.clear();
        run_stamp_ = 0;
    } else if (!mem_dirty_.empty()) {
        Value *const m = mem_.data();
        for (const std::size_t i : mem_dirty_)
            m[i] = 0;
        mem_dirty_.clear();
    }
    if (++run_stamp_ == 0) {
        std::fill(mem_stamp_.begin(), mem_stamp_.end(), 0);
        run_stamp_ = 1;
    }
    const std::uint32_t stamp = run_stamp_;
    Value *const mem = mem_.data();
    std::uint32_t *const mem_stamp = mem_stamp_.data();
    std::vector<std::size_t> &dirty = mem_dirty_;
    const std::size_t mem_n = mem_.size();

    std::uint64_t rng = seed;

    const auto uncapped = [](std::size_t v) {
        return v ? v : std::numeric_limits<std::size_t>::max();
    };
    const std::size_t step_cap = uncapped(limits_.max_steps);
    const std::size_t stack_cap = uncapped(limits_.stack_limit);
    const std::size_t depth_cap = uncapped(limits_.max_call_depth);
    const std::size_t child_cap = uncapped(limits_.max_child_genes);
    const std::size_t spawn_cap = uncapped(limits_.max_offspring);

    Value *const regs = r.registers.data();

    std::vector<TraceStep> *const tracer = trace_;
    auto rec = [&](std::size_t p, std::uint8_t o) {
        if (tracer->size() >= trace_cap_)
            return;
        TraceStep ts;
        ts.pc = static_cast<std::uint32_t>(p);
        ts.op = o;
        ts.depth = static_cast<std::uint32_t>(stack.size());
        ts.out_len = static_cast<std::uint32_t>(r.output.size());
        for (int i = 0; i < kRegisterCount; ++i) ts.regs[i] = regs[i];
        const std::size_t d = std::min(stack.size(), trace_depth_);
        ts.stack.assign(stack.end() - static_cast<std::ptrdiff_t>(d),
                        stack.end());
        tracer->push_back(std::move(ts));
    };

    auto pop = [&]() -> Value {
        if (stack.empty())
            return 0;
        Value v = stack.back();
        stack.pop_back();
        return v;
    };
    auto push = [&](Value v) {
        if (stack.size() < stack_cap)
            stack.push_back(v);
    };

    auto binary = [&](auto fn) {
        if (stack.size() >= 2) [[likely]] {
            const Value b = stack.back();
            stack.pop_back();
            Value &a = stack.back();
            a = fn(a, b);
        } else {
            const Value b = pop();
            const Value a = pop();
            push(fn(a, b));
        }
    };
    auto replace_top = [&](auto fn) {
        if (!stack.empty()) [[likely]] {
            Value &a = stack.back();
            a = fn(a);
        } else {
            push(fn(Value{0}));
        }
    };

    constexpr std::uint8_t kPushiOp = static_cast<std::uint8_t>(Op::PUSHI);

    std::size_t steps = 0;
    std::size_t pc = 0;
    std::size_t next = 0;

#if SHREWD_COMPUTED_GOTO
    static const void *const kLabels[] = {
#define SHREWD_OP_LABEL(name) &&vm_##name,
        SHREWD_OP_LIST(SHREWD_OP_LABEL)
#undef SHREWD_OP_LABEL
            && vm_halt,
        &&vm_end_loop,
        &&vm_end_ret,
    };
    static_assert(sizeof(kLabels) / sizeof(kLabels[0]) == kOpCount + 3,
                  "dispatch table must cover every opcode plus halt and the "
                  "legalised END forms");

#define VM_CASE(name) vm_##name:
#define VM_CASE_SYNTH(label, id) vm_##label:
#define VM_NEXT                                                                \
    do {                                                                       \
        pc = next;                                                             \
        ++steps;                                                               \
        if (steps > step_cap) [[unlikely]]                                     \
            goto vm_gas;                                                       \
        const std::uint8_t vm_op = ops[pc];                                    \
        if (tracer) [[unlikely]]                                               \
            rec(pc, vm_op);                                                    \
        next = pc + 1 + static_cast<std::size_t>(vm_op == kPushiOp);           \
        goto *kLabels[vm_op];                                                  \
    } while (0)

    VM_NEXT;
#else
#define VM_CASE(name) case static_cast<std::uint8_t>(Op::name):
#define VM_CASE_SYNTH(label, id) case id:
#define VM_NEXT break

    for (;;) {
        pc = next;
        if (pc >= n)
            break;
        if (steps >= step_cap) {
            r.halt = Halt::OutOfGas;
            break;
        }
        ++steps;
        const std::uint8_t op = ops[pc];
        if (tracer) [[unlikely]]
            rec(pc, op);
        next = pc + 1 + static_cast<std::size_t>(op == kPushiOp);

        switch (op) {
#endif

    VM_CASE(NOP)
    VM_NEXT;

    VM_CASE(PUSHI)
    push((pc + 1 < n) ? static_cast<Value>(genes[pc + 1]) : 0);
    VM_NEXT;

    VM_CASE(PUSH0)
    push(0);
    VM_NEXT;

    VM_CASE(PUSH1)
    push(1);
    VM_NEXT;

    VM_CASE(PUSH2)
    push(2);
    VM_NEXT;

    VM_CASE(PUSH10)
    push(10);
    VM_NEXT;

    VM_CASE(PUSHN1)
    push(-1);
    VM_NEXT;

    VM_CASE(POP)
    pop();
    VM_NEXT;

    VM_CASE(DUP) {
        const std::size_t sz = stack.size();
        if (sz != 0 && sz < stack_cap) [[likely]] {
            stack.push_back(stack.back());
        } else {
            Value a = pop();
            push(a);
            push(a);
        }
    }
    VM_NEXT;

    VM_CASE(SWAP) {
        const std::size_t sz = stack.size();
        if (sz >= 2) [[likely]] {
            std::swap(stack[sz - 1], stack[sz - 2]);
        } else {
            Value b = pop();
            Value a = pop();
            push(b);
            push(a);
        }
    }
    VM_NEXT;

    VM_CASE(OVER) {
        const std::size_t sz = stack.size();
        if (sz >= 2 && sz < stack_cap) [[likely]] {
            stack.push_back(stack[sz - 2]);
        } else {
            Value b = pop();
            Value a = pop();
            push(a);
            push(b);
            push(a);
        }
    }
    VM_NEXT;

    VM_CASE(ADD)
    binary(wrap_add);
    VM_NEXT;

    VM_CASE(SUB)
    binary(wrap_sub);
    VM_NEXT;

    VM_CASE(MUL)
    binary(wrap_mul);
    VM_NEXT;

    VM_CASE(DIV)
    binary(safe_div);
    VM_NEXT;

    VM_CASE(MOD)
    binary(safe_mod);
    VM_NEXT;

    VM_CASE(NEG)
    replace_top([](Value a) { return wrap_sub(0, a); });
    VM_NEXT;

    VM_CASE(INC)
    replace_top([](Value a) { return wrap_add(a, 1); });
    VM_NEXT;

    VM_CASE(DEC)
    replace_top([](Value a) { return wrap_sub(a, 1); });
    VM_NEXT;

    VM_CASE(LT)
    binary([](Value a, Value b) -> Value { return a < b; });
    VM_NEXT;

    VM_CASE(GT)
    binary([](Value a, Value b) -> Value { return a > b; });
    VM_NEXT;

    VM_CASE(EQ)
    binary([](Value a, Value b) -> Value { return a == b; });
    VM_NEXT;

    VM_CASE(NEQ)
    binary([](Value a, Value b) -> Value { return a != b; });
    VM_NEXT;

    VM_CASE(NOT)
    replace_top([](Value a) -> Value { return a == 0 ? 1 : 0; });
    VM_NEXT;

    VM_CASE(AND)
    binary([](Value a, Value b) -> Value { return (a != 0) && (b != 0); });
    VM_NEXT;

    VM_CASE(OR)
    binary([](Value a, Value b) -> Value { return (a != 0) || (b != 0); });
    VM_NEXT;

    VM_CASE(LD0)
    push(regs[0]);
    VM_NEXT;

    VM_CASE(LD1)
    push(regs[1]);
    VM_NEXT;

    VM_CASE(LD2)
    push(regs[2]);
    VM_NEXT;

    VM_CASE(LD3)
    push(regs[3]);
    VM_NEXT;

    VM_CASE(ST0)
    regs[0] = pop();
    VM_NEXT;

    VM_CASE(ST1)
    regs[1] = pop();
    VM_NEXT;

    VM_CASE(ST2)
    regs[2] = pop();
    VM_NEXT;

    VM_CASE(ST3)
    regs[3] = pop();
    VM_NEXT;

    VM_CASE(MLOAD)
    replace_top([&](Value a) -> Value { return mem[wrap_index(a, mem_n)]; });
    VM_NEXT;

    VM_CASE(GREAD)
    replace_top([&](Value a) -> Value {
        return static_cast<Value>(genes[wrap_index(a, n)]);
    });
    VM_NEXT;

    VM_CASE(MSTORE) {
        const Value v = pop();
        const std::size_t i = wrap_index(pop(), mem_n);
        mem[i] = v;
        if (mem_stamp[i] != stamp) {
            mem_stamp[i] = stamp;
            dirty.push_back(i);
        }
        if (tracer && !tracer->empty() && tracer->back().pc == pc) [[unlikely]] {
            tracer->back().wrote_addr = static_cast<std::int64_t>(i);
            tracer->back().wrote_val = v;
        }
    }
    VM_NEXT;

    VM_CASE(MSIZE)
    push(static_cast<Value>(mem_n));
    VM_NEXT;

    VM_CASE(IF)
    if (pop() == 0)
        next = jump[pc];
    VM_NEXT;

    VM_CASE(ELSE)
    next = jump[pc];
    VM_NEXT;

    VM_CASE(DO)
    VM_NEXT;

    VM_CASE(END)
    VM_NEXT;

    VM_CASE(BREAK)
    next = jump[pc];
    VM_NEXT;

    VM_CASE(PROC)
    next = jump[pc];
    VM_NEXT;

    VM_CASE_SYNTH(end_loop, kEndLoopOp)
    if (pop() != 0)
        next = jump[pc];
    VM_NEXT;

    VM_CASE_SYNTH(end_ret, kEndRetOp)
    if (!returns.empty()) {
        next = returns.back();
        returns.pop_back();
    }
    VM_NEXT;

    VM_CASE(CALL) {
        const Value which = pop();
        if (procs_n != 0 && returns.size() < depth_cap) {
            returns.push_back(next);
            next = procs[wrap_index(which, procs_n)];
        }
    }
    VM_NEXT;

    VM_CASE(RET)
    if (returns.empty()) {
        next = n;
    } else {
        next = returns.back();
        returns.pop_back();
    }
    VM_NEXT;

    VM_CASE(OUT)
    io.put(static_cast<char>(static_cast<unsigned char>(pop() & 0xFF)));
    VM_NEXT;

    VM_CASE(OUTNUM) {
        char buf[24];
        const auto res = std::to_chars(buf, buf + sizeof buf, pop());
        for (const char *q = buf; q != res.ptr; ++q)
            io.put(*q);
        io.put(' ');
    }
    VM_NEXT;

    VM_CASE(IN)
    push(io.read());
    VM_NEXT;

    VM_CASE(RAND)
    push(static_cast<Value>(next_random(rng) & 0x7FFFFFFFULL));
    VM_NEXT;

    VM_CASE(GLEN)
    push(static_cast<Value>(n));
    VM_NEXT;

    VM_CASE(EMIT) {
        const Value v = pop();
        if (child.size() < child_cap)
            child.push_back(static_cast<Gene>(v));
    }
    VM_NEXT;

    VM_CASE(CLEN)
    push(static_cast<Value>(child.size()));
    VM_NEXT;

    VM_CASE(CREAD) {
        const Value i = pop();
        push(child.empty()
                 ? 0
                 : static_cast<Value>(child[wrap_index(i, child.size())]));
    }
    VM_NEXT;

    VM_CASE(CWRITE) {
        const Value v = pop();
        const Value i = pop();
        if (!child.empty())
            child[wrap_index(i, child.size())] = static_cast<Gene>(v);
    }
    VM_NEXT;

    VM_CASE(SPAWN)
    if (r.offspring.size() < spawn_cap) {
        r.offspring.push_back(std::move(child));
        if (!genome_pool_.empty()) {
            child = std::move(genome_pool_.back());
            genome_pool_.pop_back();
        }
        child.clear();
    }
    VM_NEXT;

#if SHREWD_COMPUTED_GOTO
vm_gas:
    --steps;
    if (pc < n)
        r.halt = Halt::OutOfGas;
    goto vm_done;
vm_halt:
    --steps;
vm_done:;
#else
        case kHaltOp:
            VM_NEXT;
        }
    }
#endif

#undef VM_CASE
#undef VM_CASE_SYNTH
#undef VM_NEXT

    r.steps = steps;
    r.stack = stack;
    r.uncommitted_child_genes = child.size();

    for (const std::size_t i : dirty)
        mem[i] = 0;
    dirty.clear();
}

#if SHREWD_COMPUTED_GOTO
#pragma GCC diagnostic pop
#endif

} // namespace shrewd
