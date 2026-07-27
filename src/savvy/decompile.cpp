#include "savvy/savvy.hpp"

#include "shrewd/isa.hpp"
#include "shrewd/vm.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace savvy {
namespace {

using shrewd::ControlMap;
using shrewd::Genome;
using shrewd::Op;

constexpr int kPrecOr = 1;
constexpr int kPrecAnd = 2;
constexpr int kPrecEq = 3;
constexpr int kPrecRel = 4;
constexpr int kPrecAdd = 5;
constexpr int kPrecMul = 6;
constexpr int kPrecUnary = 7;
constexpr int kPrecPrimary = 8;

constexpr char kOpen = '\x01';
constexpr char kClose = '\x02';

std::string mk_cell(long long n) {
    return std::string(1, kOpen) + "S" + std::to_string(n) + kClose;
}
std::string mk_cell_idx(long long base, const std::string &idx) {
    return std::string(1, kOpen) + "X" + std::to_string(base) + "|" + idx +
           kClose;
}
std::string mk_slot(long long k) {
    return std::string(1, kOpen) + "F" + std::to_string(k) + kClose;
}
std::string mk_slot_idx(long long k, const std::string &idx) {
    return std::string(1, kOpen) + "G" + std::to_string(k) + "|" + idx + kClose;
}
std::string mk_slot_addr(long long k) {
    return std::string(1, kOpen) + "A" + std::to_string(k) + kClose;
}
std::string mk_param(std::size_t a) {
    return std::string(1, kOpen) + "P" + std::to_string(a) + kClose;
}

struct Frag {
    std::string text;
    int prec = kPrecPrimary;
    bool effects = false;
    bool atomic = false;
    std::string neg;
    int neg_prec = kPrecPrimary;
    bool neg_exact = false;
    bool boolish = false;

    enum class A { None, Lit, Sp, FrameBase } a = A::None;
    long long base = 0;
    std::string idx;
    bool has_idx = false;
};

Frag literal_frag(long long v) {
    Frag f;
    f.text = std::to_string(v);
    f.prec = v < 0 ? kPrecUnary : kPrecPrimary;
    f.atomic = true;
    f.boolish = v == 0 || v == 1;
    f.a = Frag::A::Lit;
    f.base = v;
    return f;
}

std::optional<long long> as_literal_text(const std::string &t) {
    long long v = 0;
    const char *b = t.data();
    const char *e = b + t.size();
    const auto res = std::from_chars(b, e, v);
    if (res.ec != std::errc{} || res.ptr != e)
        return std::nullopt;
    return v;
}

std::optional<long long> as_literal(const Frag &f) {
    if (f.a == Frag::A::Lit)
        return f.base;
    return as_literal_text(f.text);
}

bool is_printable(long long c) { return c >= 32 && c < 127; }
bool char_worthy(long long c) {
    return is_printable(c) || c == '\n' || c == '\t' || c == '\r' || c == 0;
}

std::string char_literal(long long c) {
    switch (c) {
    case '\n':
        return "'\\n'";
    case '\t':
        return "'\\t'";
    case '\r':
        return "'\\r'";
    case 0:
        return "'\\0'";
    case '\'':
        return "'\\''";
    case '\\':
        return "'\\\\'";
    default:
        return std::string("'") + static_cast<char>(c) + "'";
    }
}

std::string escape_string(const std::string &raw) {
    std::string out;
    for (const char ch : raw) {
        switch (ch) {
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\0':
            out += "\\0";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

struct SNode {
    enum class K {
        Plain,
        Comment,
        Store,
        Putchar,
        Print,
        Puts,
        If,
        DoWhile,
        While,
        Return,
        Break,
        SpAdjust,
        SpInit,
        StrData,
    };
    K k = K::Plain;
    std::string a, b;
    long long n1 = 0;
    bool lit_target = false;
    bool lit_value = false;
    long long n2 = 0;
    bool temp_target = false;
    bool approx = false;
    std::vector<SNode> body, els;
    std::vector<long long> data;
};

class Decompiler {
  public:
    explicit Decompiler(const Genome &g)
        : g_(g), cm_(shrewd::build_control_map(g)) {}

    std::string run();

  private:
    std::size_t walk(std::size_t begin, std::size_t end, bool in_proc,
                     std::size_t arity_hint, std::vector<SNode> &out);

    std::size_t body_begin(std::size_t k) const { return cm_.procs[k]; }
    std::size_t body_end(std::size_t k) const {
        const std::size_t proc_pos = cm_.procs[k] - 1;
        const std::size_t past = cm_.jump[proc_pos];
        if (past == shrewd::kNoJump)
            return g_.size();
        if (past >= 1 && past - 1 < g_.size() &&
            cm_.jump[past - 1] == shrewd::kReturnHere)
            return past - 1;
        return past;
    }

    std::string temp_cell() {
        return "mem[mem.length - " + std::to_string(++temp_count_) + "]";
    }

    bool observes_memory() const {
        for (std::size_t i = 0; i < g_.size();) {
            const Op op = shrewd::decode(g_[i]);
            if (op == Op::MLOAD)
                return true;
            i += static_cast<std::size_t>(shrewd::op_size(op));
        }
        return false;
    }

    static void prune_unreachable(std::vector<SNode> &list);
    static void literalize_sp_inits(std::vector<SNode> &list);
    static void whileify(std::vector<SNode> &list);
    static void merge_putchar_runs(std::vector<SNode> &list);
    static void group_string_stores(std::vector<SNode> &list);
    static void recognize_puts_loops(std::vector<SNode> &list);
    static void inline_temp_returns(std::vector<SNode> &list,
                                    const std::vector<SNode> &whole_main,
                                    const std::vector<std::vector<SNode>> &fns);

    struct BodyCtx {
        bool lifted = false;
        long long frame = 0;
        std::map<long long, std::pair<std::string, long long>> slots;
        std::vector<std::string> latches;
        std::string decls;
    };
    BodyCtx analyse_body(std::vector<SNode> &list, std::size_t arity);

    struct Region {
        long long base = 0, len = 1;
        enum class T { Scalar, Str, Buf, Pad } t = T::Scalar;
        std::string name;
    };
    void collect_cells(const std::vector<SNode> &list);
    void collect_cells_text(const std::string &t);
    void build_regions();
    std::string cell_name(long long n) const;
    std::string cell_indexed(long long base, const std::string &idx) const;

    std::string resolve(const std::string &t, const BodyCtx &ctx) const;
    void print_nodes(const std::vector<SNode> &list, int indent,
                     const BodyCtx &ctx, std::string &out) const;

    const Genome &g_;
    ControlMap cm_;
    std::vector<std::size_t> arity_;
    std::size_t temp_count_ = 0;

    std::vector<long long> seen_cells_;
    std::vector<long long> seen_bases_;
    std::vector<std::pair<long long, long long>> str_regions_;
    std::vector<Region> regions_;
    bool renaming_ = true;
};

std::size_t Decompiler::walk(std::size_t begin, std::size_t end, bool in_proc,
                             std::size_t arity_hint, std::vector<SNode> &out) {
    std::vector<Frag> stack;
    std::size_t underflows = 0;

    struct Block {
        Op kind;
        std::size_t depth;
        std::vector<SNode> nodes;
        std::string cond, cond_neg;
    };
    std::vector<Block> blocks;

    auto top_nodes = [&]() -> std::vector<SNode> & {
        return blocks.empty() ? out : blocks.back().nodes;
    };

    auto add = [&](SNode n) { top_nodes().push_back(std::move(n)); };

    auto plain = [&](std::string s) {
        SNode n;
        n.k = SNode::K::Plain;
        n.a = std::move(s);
        add(std::move(n));
    };
    auto comment = [&](std::string s) {
        SNode n;
        n.k = SNode::K::Comment;
        n.a = std::move(s);
        add(std::move(n));
    };

    auto materialize = [&](Frag &f) {
        const std::string t = temp_cell();
        SNode n;
        n.k = SNode::K::Store;
        n.a = t;
        n.b = f.text;
        n.temp_target = true;
        add(std::move(n));
        f = Frag{};
        f.text = t;
        f.atomic = true;
    };

    auto pin_pending = [&]() {
        for (Frag &f : stack) {
            if (!f.atomic)
                materialize(f);
        }
    };

    auto pop = [&]() -> Frag {
        if (!stack.empty()) {
            Frag f = std::move(stack.back());
            stack.pop_back();
            return f;
        }
        ++underflows;
        if (in_proc && underflows <= arity_hint) {
            Frag f;
            f.text = mk_param(arity_hint - underflows);
            f.atomic = true;
            return f;
        }
        return literal_frag(0);
    };
    auto push = [&](Frag f) { stack.push_back(std::move(f)); };

    auto wrap = [](const Frag &f, int prec, bool right_side) {
        if (f.prec < prec || (f.prec == prec && right_side))
            return "(" + f.text + ")";
        return f.text;
    };

    auto bin = [&](const char *op, const char *flip, int prec) {
        const Frag b = pop();
        const Frag a = pop();
        Frag r;
        r.text = wrap(a, prec, false) + " " + op + " " + wrap(b, prec, true);
        if (flip) {
            r.neg =
                wrap(a, prec, false) + " " + flip + " " + wrap(b, prec, true);
            r.neg_prec = prec;
            r.neg_exact = true;
        }
        r.prec = prec;
        r.effects = a.effects || b.effects;
        r.atomic = a.atomic && b.atomic;
        r.boolish = flip != nullptr || prec == kPrecAnd || prec == kPrecOr;

        if (op[0] == '+' && op[1] == 0) {
            if (a.a == Frag::A::Lit && a.base >= 1) {
                r.has_idx = true;
                r.base = a.base;
                r.idx = b.text;
            } else if (a.a == Frag::A::Sp && b.a == Frag::A::Lit) {
                r.a = Frag::A::FrameBase;
                r.base = b.base;
            } else if (a.a == Frag::A::Sp) {
                r.a = Frag::A::FrameBase;
                r.base = 0;
                r.has_idx = true;
                r.idx = b.text;
            } else if (a.a == Frag::A::FrameBase && !a.has_idx) {
                r.a = Frag::A::FrameBase;
                r.base = a.base;
                r.has_idx = true;
                r.idx = b.text;
            }
        }
        if (op[0] == '-' && op[1] == 0 && a.a == Frag::A::Sp &&
            b.a == Frag::A::Lit) {
            r.a = Frag::A::FrameBase;
            r.base = -b.base;
        }
        push(std::move(r));
    };
    auto unary = [&](const char *op) {
        const Frag a = pop();
        Frag r;
        if (op[0] == '!' && a.neg_exact) {
            r.text = a.neg;
            r.prec = a.neg_prec;
            r.neg = a.text;
            r.neg_prec = a.prec;
            r.neg_exact = a.boolish;
            r.boolish = true;
        } else if (op[0] == '!' && a.a == Frag::A::Lit) {
            r = literal_frag(a.base == 0 ? 1 : 0);
        } else {
            r.text = op + wrap(a, kPrecUnary, false);
            r.prec = kPrecUnary;
            if (op[0] == '!') {
                r.neg = a.text;
                r.neg_prec = a.prec;
                r.neg_exact = a.boolish;
                r.boolish = true;
            }
        }
        r.effects = a.effects;
        r.atomic = a.atomic;
        push(std::move(r));
    };

    auto close_block = [&](bool is_do, const std::string &cond, bool approx) {
        Block blk = std::move(blocks.back());
        blocks.pop_back();
        SNode n;
        if (is_do) {
            n.k = SNode::K::DoWhile;
            n.a = cond;
            n.approx = approx;
        } else {
            n.k = SNode::K::If;
            n.a = blk.cond;
            n.b = blk.cond_neg;
        }
        n.body = std::move(blk.nodes);
        add(std::move(n));
    };

    for (std::size_t pc = begin; pc < end;) {
        const Op op = shrewd::decode(g_[pc]);
        const std::size_t jump =
            pc < cm_.jump.size() ? cm_.jump[pc] : shrewd::kNoJump;

        switch (op) {
        case Op::NOP:
            break;

        case Op::PUSHI:
            push(literal_frag((pc + 1 < g_.size())
                                  ? static_cast<long long>(g_[pc + 1])
                                  : 0LL));
            break;
        case Op::PUSH0:
            push(literal_frag(0));
            break;
        case Op::PUSH1:
            push(literal_frag(1));
            break;
        case Op::PUSH2:
            push(literal_frag(2));
            break;
        case Op::PUSH10:
            push(literal_frag(10));
            break;
        case Op::PUSHN1:
            push(literal_frag(-1));
            break;

        case Op::POP: {
            const Frag f = pop();
            if (f.effects) {
                pin_pending();
                plain(f.text + ";");
            }
            break;
        }
        case Op::DUP: {
            Frag x = pop();
            if (!x.atomic)
                materialize(x);
            push(x);
            push(x);
            break;
        }
        case Op::SWAP: {
            Frag y = pop();
            Frag x = pop();
            if (x.effects)
                materialize(x);
            if (y.effects)
                materialize(y);
            push(std::move(y));
            push(std::move(x));
            break;
        }
        case Op::OVER: {
            Frag y = pop();
            Frag x = pop();
            if (!x.atomic)
                materialize(x);
            if (y.effects)
                materialize(y);
            push(x);
            push(std::move(y));
            push(std::move(x));
            break;
        }

        case Op::ADD:
            bin("+", nullptr, kPrecAdd);
            break;
        case Op::SUB:
            bin("-", nullptr, kPrecAdd);
            break;
        case Op::MUL:
            bin("*", nullptr, kPrecMul);
            break;
        case Op::DIV:
            bin("/", nullptr, kPrecMul);
            break;
        case Op::MOD:
            bin("%", nullptr, kPrecMul);
            break;
        case Op::NEG:
            unary("-");
            break;
        case Op::NOT:
            unary("!");
            break;
        case Op::INC: {
            const Frag x = pop();
            Frag r;
            r.text = wrap(x, kPrecAdd, false) + " + 1";
            r.prec = kPrecAdd;
            r.effects = x.effects;
            r.atomic = x.atomic;
            if (x.a == Frag::A::Lit) {
                r = literal_frag(x.base + 1);
            } else if (x.a == Frag::A::Sp) {
                r.a = Frag::A::FrameBase;
                r.base = 1;
            }
            push(std::move(r));
            break;
        }
        case Op::DEC: {
            const Frag x = pop();
            Frag r;
            r.text = wrap(x, kPrecAdd, false) + " - 1";
            r.prec = kPrecAdd;
            r.effects = x.effects;
            r.atomic = x.atomic;
            if (x.a == Frag::A::Lit)
                r = literal_frag(x.base - 1);
            push(std::move(r));
            break;
        }

        case Op::LT:
            bin("<", ">=", kPrecRel);
            break;
        case Op::GT:
            bin(">", "<=", kPrecRel);
            break;
        case Op::EQ:
            bin("==", "!=", kPrecEq);
            break;
        case Op::NEQ:
            bin("!=", "==", kPrecEq);
            break;
        case Op::AND:
            bin("&&", nullptr, kPrecAnd);
            break;
        case Op::OR:
            bin("||", nullptr, kPrecOr);
            break;

        case Op::LD0:
        case Op::LD1:
        case Op::LD2:
        case Op::LD3: {
            Frag f;
            f.text = "r" + std::to_string(static_cast<int>(op) -
                                          static_cast<int>(Op::LD0));
            push(std::move(f));
            break;
        }
        case Op::ST0:
        case Op::ST1:
        case Op::ST2:
        case Op::ST3: {
            const Frag v = pop();
            pin_pending();
            SNode n;
            n.k = SNode::K::Store;
            n.a = "r" + std::to_string(static_cast<int>(op) -
                                       static_cast<int>(Op::ST0));
            n.b = v.text;
            add(std::move(n));
            break;
        }

        case Op::MLOAD: {
            const Frag i = pop();
            Frag f;
            f.effects = i.effects;
            if (i.a == Frag::A::Lit && i.base == 0) {
                f.text = "mem[0]";
                f.a = Frag::A::Sp;
            } else if (i.a == Frag::A::Lit && i.base >= 1) {
                f.text = mk_cell(i.base);
            } else if (i.has_idx && i.a == Frag::A::None) {
                f.text = mk_cell_idx(i.base, i.idx);
            } else if (i.a == Frag::A::FrameBase && !i.has_idx && i.base >= 0) {
                f.text = mk_slot(i.base);
            } else if (i.a == Frag::A::FrameBase && i.has_idx && i.base >= 0) {
                f.text = mk_slot_idx(i.base, i.idx);
            } else if (i.a == Frag::A::Sp) {
                f.text = mk_slot(0);
            } else {
                f.text = "mem[" + i.text + "]";
            }
            push(std::move(f));
            break;
        }
        case Op::MSTORE: {
            const Frag v = pop();
            const Frag i = pop();
            pin_pending();

            if (i.a == Frag::A::Lit && i.base == 0) {
                if (v.text == "mem.length") {
                    SNode n;
                    n.k = SNode::K::SpInit;
                    add(std::move(n));
                    break;
                }
                if (v.a == Frag::A::FrameBase && !v.has_idx) {
                    SNode n;
                    n.k = SNode::K::SpAdjust;
                    n.n1 = v.base;
                    add(std::move(n));
                    break;
                }
                SNode n;
                n.k = SNode::K::Store;
                n.a = "mem[0]";
                n.b = v.text;
                add(std::move(n));
                break;
            }

            SNode n;
            n.k = SNode::K::Store;
            n.b = v.text;
            if (const auto lv = as_literal(v)) {
                n.lit_value = true;
                n.n2 = *lv;
            }
            if (i.a == Frag::A::Lit && i.base >= 1) {
                n.a = mk_cell(i.base);
                n.lit_target = true;
                n.n1 = i.base;
            } else if (i.has_idx && i.a == Frag::A::None) {
                n.a = mk_cell_idx(i.base, i.idx);
            } else if (i.a == Frag::A::FrameBase && !i.has_idx && i.base >= 0) {
                n.a = mk_slot(i.base);
            } else if (i.a == Frag::A::FrameBase && i.has_idx && i.base >= 0) {
                n.a = mk_slot_idx(i.base, i.idx);
            } else if (i.a == Frag::A::Sp) {
                n.a = mk_slot(0);
            } else {
                n.a = "mem[" + i.text + "]";
            }
            add(std::move(n));
            break;
        }
        case Op::MSIZE: {
            Frag f;
            f.text = "mem.length";
            f.atomic = true;
            push(std::move(f));
            break;
        }

        case Op::IF: {
            const Frag cond = pop();
            pin_pending();
            Block blk;
            blk.kind = Op::IF;
            blk.depth = stack.size();
            blk.cond = cond.text;
            blk.cond_neg = cond.neg;
            blocks.push_back(std::move(blk));
            break;
        }
        case Op::ELSE:
            if (jump != shrewd::kNoJump && !blocks.empty() &&
                blocks.back().kind == Op::IF) {
                pin_pending();
                Block &blk = blocks.back();
                SNode n;
                n.k = SNode::K::If;
                n.a = blk.cond;
                n.b = blk.cond_neg;
                n.body = std::move(blk.nodes);
                blk.nodes.clear();
                blk.kind = Op::ELSE;
                blk.cond.clear();
                blk.nodes.push_back(std::move(n));
                blk.nodes.back().els.clear();
            } else {
                comment("unmatched ELSE: does nothing");
            }
            break;

        case Op::DO: {
            static constexpr Op kPutsShape[] = {
                Op::DO,    Op::DUP,   Op::MLOAD, Op::NOT,   Op::IF,
                Op::BREAK, Op::END,   Op::DUP,   Op::MLOAD, Op::OUT,
                Op::INC,   Op::PUSH1, Op::END,   Op::POP};
            constexpr std::size_t kPutsLen = std::size(kPutsShape);
            bool is_puts = pc + kPutsLen <= end;
            for (std::size_t k = 0; is_puts && k < kPutsLen; ++k)
                is_puts = shrewd::decode(g_[pc + k]) == kPutsShape[k];
            if (is_puts) {
                const Frag p = pop();
                pin_pending();
                std::string addr = p.text;
                if (p.a == Frag::A::Sp)
                    addr = mk_slot_addr(0);
                else if (p.a == Frag::A::FrameBase && !p.has_idx && p.base >= 0)
                    addr = mk_slot_addr(p.base);
                plain("puts(" + addr + ");");
                pc += kPutsLen;
                continue;
            }
            pin_pending();
            Block blk;
            blk.kind = Op::DO;
            blk.depth = stack.size();
            blocks.push_back(std::move(blk));
            break;
        }

        case Op::END:
            if (jump == shrewd::kReturnHere) {
                comment("stray procedure END");
            } else if (!blocks.empty() && blocks.back().kind == Op::DO) {
                const std::size_t uf_before = underflows;
                Frag cond = pop();
                const bool from_outside = stack.size() < blocks.back().depth ||
                                          underflows != uf_before;
                if (!from_outside) {
                    const bool approx = stack.size() != blocks.back().depth;
                    pin_pending();
                    close_block(true, cond.text, approx);
                } else if (blocks.back().nodes.empty()) {
                    bool unknown = false;
                    for (;;) {
                        const auto lit = as_literal(cond);
                        if (!lit) {
                            unknown = true;
                            break;
                        }
                        if (*lit == 0)
                            break;
                        cond = pop();
                    }
                    blocks.pop_back();
                    if (unknown)
                        comment("an empty loop here drained values from the "
                                "stack; approximated");
                    pin_pending();
                } else {
                    pin_pending();
                    close_block(true, "0", true);
                }
            } else if (!blocks.empty() && blocks.back().kind == Op::IF) {
                pin_pending();
                close_block(false, "", false);
            } else if (!blocks.empty() && blocks.back().kind == Op::ELSE) {
                pin_pending();
                Block blk = std::move(blocks.back());
                blocks.pop_back();
                SNode ifn = std::move(blk.nodes.front());
                ifn.els.assign(std::make_move_iterator(blk.nodes.begin() + 1),
                               std::make_move_iterator(blk.nodes.end()));
                add(std::move(ifn));
            } else {
                comment("unmatched END: does nothing");
            }
            break;

        case Op::BREAK: {
            const bool local_loop =
                std::any_of(blocks.begin(), blocks.end(),
                            [](const Block &b) { return b.kind == Op::DO; });
            pin_pending();
            if (jump != shrewd::kNoJump && !local_loop && in_proc) {
                SNode c;
                c.k = SNode::K::Comment;
                c.a = "this break escapes the procedure in the original";
                add(std::move(c));
                SNode n;
                n.k = SNode::K::Return;
                n.b = "0";
                add(std::move(n));
                break;
            }
            SNode n;
            n.k = SNode::K::Break;
            if (jump == shrewd::kNoJump)
                n.a = "no enclosing loop: does nothing";
            else if (!local_loop)
                n.a = "exits a loop opened outside this code in the "
                      "original; here it exits nothing";
            add(std::move(n));
            break;
        }

        case Op::PROC:
            pc = (jump != shrewd::kNoJump) ? jump : g_.size();
            continue;

        case Op::CALL: {
            const Frag idx = pop();
            const std::size_t nprocs = cm_.procs.size();
            if (nprocs == 0) {
                if (idx.effects) {
                    pin_pending();
                    plain(idx.text + ";");
                }
                break;
            }
            if (const auto lit = as_literal(idx)) {
                const long long p = static_cast<long long>(nprocs);
                const std::size_t k =
                    static_cast<std::size_t>(((*lit % p) + p) % p);
                std::vector<std::string> args(arity_[k]);
                for (std::size_t a = args.size(); a-- > 0;)
                    args[a] = pop().text;
                std::string call = "proc" + std::to_string(k) + "(";
                for (std::size_t a = 0; a < args.size(); ++a) {
                    if (a)
                        call += ", ";
                    call += args[a];
                }
                call += ")";
                Frag f;
                f.text = std::move(call);
                f.effects = true;
                push(std::move(f));
            } else {
                if (idx.effects) {
                    pin_pending();
                    plain(idx.text + ";");
                }
                comment("a CALL whose target is computed at run time -- "
                        "Savvy cannot express it; 0 assumed");
                push(literal_frag(0));
            }
            break;
        }
        case Op::RET: {
            const Frag v = pop();
            pin_pending();
            SNode n;
            n.k = SNode::K::Return;
            n.b = v.text;
            add(std::move(n));
            break;
        }

        case Op::OUT: {
            const Frag v = pop();
            pin_pending();
            SNode n;
            n.k = SNode::K::Putchar;
            n.b = v.text;
            if (const auto c = as_literal(v); c && char_worthy(*c)) {
                n.lit_value = true;
                n.n2 = *c;
            }
            add(std::move(n));
            break;
        }
        case Op::OUTNUM: {
            const Frag v = pop();
            pin_pending();
            SNode n;
            n.k = SNode::K::Print;
            n.b = v.text;
            add(std::move(n));
            break;
        }
        case Op::IN: {
            Frag f;
            f.text = "input()";
            f.effects = true;
            push(std::move(f));
            break;
        }
        case Op::RAND: {
            Frag f;
            f.text = "rand()";
            f.effects = true;
            push(std::move(f));
            break;
        }

        case Op::GLEN: {
            Frag f;
            f.text = "self.length";
            f.atomic = true;
            push(std::move(f));
            break;
        }
        case Op::GREAD: {
            const Frag i = pop();
            Frag f;
            f.text = "self[" + i.text + "]";
            f.effects = i.effects;
            f.atomic = i.atomic;
            push(std::move(f));
            break;
        }
        case Op::EMIT: {
            const Frag v = pop();
            pin_pending();
            plain("emit(" + v.text + ");");
            break;
        }
        case Op::CLEN: {
            Frag f;
            f.text = "child.length";
            push(std::move(f));
            break;
        }
        case Op::CREAD: {
            const Frag i = pop();
            Frag f;
            f.text = "child[" + i.text + "]";
            f.effects = i.effects;
            push(std::move(f));
            break;
        }
        case Op::CWRITE: {
            const Frag v = pop();
            const Frag i = pop();
            pin_pending();
            plain("child[" + i.text + "] = " + v.text + ";");
            break;
        }
        case Op::SPAWN:
            pin_pending();
            plain("spawn();");
            break;

        case Op::COUNT:
            break;
        }

        pc += static_cast<std::size_t>(shrewd::op_size(op));
    }

    while (!blocks.empty()) {
        pin_pending();
        if (blocks.back().kind == Op::ELSE) {
            Block blk = std::move(blocks.back());
            blocks.pop_back();
            SNode ifn = std::move(blk.nodes.front());
            ifn.els.assign(std::make_move_iterator(blk.nodes.begin() + 1),
                           std::make_move_iterator(blk.nodes.end()));
            SNode c;
            c.k = SNode::K::Comment;
            c.a = "the genome never closed this block";
            ifn.els.push_back(std::move(c));
            add(std::move(ifn));
        } else {
            const bool is_do = blocks.back().kind == Op::DO;
            close_block(is_do, is_do ? "0" : "", false);
            top_nodes().back().approx = false;
            SNode c;
            c.k = SNode::K::Comment;
            c.a = "the genome never closed this block";
            top_nodes().back().body.push_back(std::move(c));
        }
    }

    if (in_proc) {
        if (!stack.empty()) {
            for (std::size_t i = 0; i + 1 < stack.size(); ++i) {
                if (stack[i].effects) {
                    SNode n;
                    n.k = SNode::K::Plain;
                    n.a = stack[i].text + ";";
                    out.push_back(std::move(n));
                }
            }
            SNode n;
            n.k = SNode::K::Return;
            n.b = stack.back().text;
            out.push_back(std::move(n));
        }
    } else {
        for (const Frag &f : stack) {
            if (f.effects) {
                SNode n;
                n.k = SNode::K::Plain;
                n.a = f.text + ";";
                out.push_back(std::move(n));
            }
        }
    }

    return underflows;
}

void Decompiler::prune_unreachable(std::vector<SNode> &list) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        prune_unreachable(list[i].body);
        prune_unreachable(list[i].els);
        if (list[i].k == SNode::K::Return && i + 1 < list.size())
            list.resize(i + 1);
    }
}

void Decompiler::literalize_sp_inits(std::vector<SNode> &list) {
    for (SNode &n : list) {
        literalize_sp_inits(n.body);
        literalize_sp_inits(n.els);
        if (n.k == SNode::K::SpInit) {
            n.k = SNode::K::Store;
            n.a = "mem[0]";
            n.b = "mem.length";
        }
    }
}

void Decompiler::whileify(std::vector<SNode> &list) {
    for (SNode &n : list) {
        whileify(n.body);
        whileify(n.els);
        if (n.k != SNode::K::DoWhile || n.a != "1" || n.body.empty())
            continue;
        SNode &head = n.body.front();
        if (head.k != SNode::K::If || !head.els.empty() ||
            head.body.size() != 1 || head.body[0].k != SNode::K::Break ||
            !head.body[0].a.empty())
            continue;
        n.k = SNode::K::While;
        n.a = head.b.empty() ? "!(" + head.a + ")" : head.b;
        n.body.erase(n.body.begin());
    }
}

void Decompiler::merge_putchar_runs(std::vector<SNode> &list) {
    std::vector<SNode> out;
    for (std::size_t i = 0; i < list.size();) {
        merge_putchar_runs(list[i].body);
        merge_putchar_runs(list[i].els);
        std::size_t j = i;
        while (j < list.size() && list[j].k == SNode::K::Putchar &&
               list[j].lit_value)
            ++j;
        if (j - i >= 2) {
            SNode n;
            n.k = SNode::K::Puts;
            for (std::size_t k = i; k < j; ++k)
                n.a += static_cast<char>(list[k].n2);
            out.push_back(std::move(n));
            i = j;
        } else {
            out.push_back(std::move(list[i]));
            ++i;
        }
    }
    list = std::move(out);
}

void Decompiler::group_string_stores(std::vector<SNode> &list) {
    std::vector<SNode> out;
    for (std::size_t i = 0; i < list.size();) {
        group_string_stores(list[i].body);
        group_string_stores(list[i].els);
        std::size_t j = i;
        while (j < list.size() && list[j].k == SNode::K::Store &&
               list[j].lit_target && list[j].lit_value &&
               char_worthy(list[j].n2) &&
               (j == i || list[j].n1 == list[j - 1].n1 + 1))
            ++j;
        if (j - i >= 3 && list[j - 1].n2 == 0) {
            SNode n;
            n.k = SNode::K::StrData;
            n.n1 = list[i].n1;
            for (std::size_t k = i; k < j; ++k)
                n.data.push_back(list[k].n2);
            out.push_back(std::move(n));
            i = j;
        } else {
            out.push_back(std::move(list[i]));
            ++i;
        }
    }
    list = std::move(out);
}

void Decompiler::recognize_puts_loops(std::vector<SNode> &list) {
    for (SNode &n : list) {
        recognize_puts_loops(n.body);
        recognize_puts_loops(n.els);
    }
    for (std::size_t i = 0; i + 1 < list.size(); ++i) {
        SNode &st = list[i];
        SNode &lp = list[i + 1];
        if (st.k != SNode::K::Store || lp.k != SNode::K::While)
            continue;
        const std::string ptr = "mem[" + st.a + "]";
        if (lp.a != ptr)
            continue;
        if (lp.body.size() != 2)
            continue;
        if (lp.body[0].k != SNode::K::Putchar || lp.body[0].b != ptr)
            continue;
        if (lp.body[1].k != SNode::K::Store || lp.body[1].a != st.a ||
            lp.body[1].b != st.a + " + 1")
            continue;
        SNode n;
        n.k = SNode::K::Plain;
        n.a = "puts(" + st.b + ");";
        st = std::move(n);
        lp.k = SNode::K::Comment;
        lp.a = "";
        lp.body.clear();
    }
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const SNode &n) {
                                  return n.k == SNode::K::Comment &&
                                         n.a.empty();
                              }),
               list.end());
}

namespace lift_detail {

void count_occurrences(const std::vector<SNode> &list, const std::string &s,
                       std::size_t &count) {
    for (const SNode &n : list) {
        for (const std::string *t : {&n.a, &n.b}) {
            std::size_t at = 0;
            while ((at = t->find(s, at)) != std::string::npos) {
                ++count;
                at += s.size();
            }
        }
        count_occurrences(n.body, s, count);
        count_occurrences(n.els, s, count);
    }
}

void collect_slots(const std::string &t, std::vector<long long> &scalar,
                   std::vector<long long> &array, bool &raw_sp) {
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t.compare(i, 6, "mem[0]") == 0)
            raw_sp = true;
        if (t[i] != kOpen)
            continue;
        const char kind = t[i + 1];
        if (kind != 'F' && kind != 'G' && kind != 'A')
            continue;
        long long v = 0;
        std::size_t j = i + 2;
        bool neg = j < t.size() && t[j] == '-';
        if (neg)
            ++j;
        while (j < t.size() && t[j] >= '0' && t[j] <= '9')
            v = v * 10 + (t[j++] - '0');
        if (neg)
            v = -v;
        (kind == 'F' ? scalar : array).push_back(v);
    }
}

void collect_slots(const std::vector<SNode> &list,
                   std::vector<long long> &scalar,
                   std::vector<long long> &array, bool &raw_sp) {
    for (const SNode &n : list) {
        collect_slots(n.a, scalar, array, raw_sp);
        collect_slots(n.b, scalar, array, raw_sp);
        collect_slots(n.body, scalar, array, raw_sp);
        collect_slots(n.els, scalar, array, raw_sp);
    }
}

bool frame_shape_ok(const std::vector<SNode> &list, long long frame,
                    bool outermost) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        const SNode &n = list[i];
        if (!frame_shape_ok(n.body, frame, false) ||
            !frame_shape_ok(n.els, frame, false))
            return false;
        if (n.k == SNode::K::SpInit)
            return false;
        if (n.k != SNode::K::SpAdjust)
            continue;
        if (frame == 0 && n.n1 == 0)
            continue;
        if (outermost && i == 0 && n.n1 == -frame && frame > 0)
            continue;
        if (n.n1 != frame || frame == 0)
            return false;
        const bool before_return =
            i + 1 < list.size() && list[i + 1].k == SNode::K::Return;
        const bool trailing = outermost && i + 1 == list.size();
        if (!before_return && !trailing)
            return false;
    }
    return true;
}

void strip_frame_ops(std::vector<SNode> &list) {
    for (SNode &n : list) {
        strip_frame_ops(n.body);
        strip_frame_ops(n.els);
    }
    list.erase(std::remove_if(
                   list.begin(), list.end(),
                   [](const SNode &n) { return n.k == SNode::K::SpAdjust; }),
               list.end());
}

} // namespace lift_detail

Decompiler::BodyCtx Decompiler::analyse_body(std::vector<SNode> &list,
                                             std::size_t arity) {
    BodyCtx ctx;

    std::vector<long long> scalar, array;
    bool raw_sp = false;
    lift_detail::collect_slots(list, scalar, array, raw_sp);

    long long frame = 0;
    if (!list.empty() && list[0].k == SNode::K::SpAdjust && list[0].n1 < 0)
        frame = -list[0].n1;

    bool ok = !raw_sp && lift_detail::frame_shape_ok(list, frame, true);
    for (const long long k : scalar)
        ok = ok && k >= 0 && k < std::max<long long>(frame, 0);
    for (const long long k : array)
        ok = ok && k >= 0 && k < std::max<long long>(frame, 0);
    if (frame == 0)
        ok = ok && scalar.empty() && array.empty();

    if (!ok) {
        ctx.lifted = false;
        for (std::size_t a = 0; a < arity; ++a)
            ctx.latches.push_back(temp_cell());
        return ctx;
    }

    ctx.lifted = true;
    ctx.frame = frame;

    std::sort(array.begin(), array.end());
    array.erase(std::unique(array.begin(), array.end()), array.end());
    std::sort(scalar.begin(), scalar.end());
    scalar.erase(std::unique(scalar.begin(), scalar.end()), scalar.end());

    std::string decls;
    long long at = 0;
    auto flush_pad = [&](long long upto) {
        if (at >= upto)
            return;
        decls += "var pad" + std::to_string(at) + "[" +
                 std::to_string(upto - at) + "]; ";
        at = upto;
    };
    while (at < frame) {
        const auto next_arr = std::lower_bound(array.begin(), array.end(), at);
        const auto next_sca =
            std::lower_bound(scalar.begin(), scalar.end(), at);
        long long next = frame;
        if (next_arr != array.end())
            next = std::min(next, *next_arr);
        if (next_sca != scalar.end())
            next = std::min(next, *next_sca);
        flush_pad(next);
        if (at >= frame)
            break;
        const bool is_arr = next_arr != array.end() && *next_arr == at;
        if (is_arr) {
            long long end = frame;
            for (const long long s : scalar)
                if (s > at) {
                    end = std::min(end, s);
                    break;
                }
            for (const long long a2 : array)
                if (a2 > at) {
                    end = std::min(end, a2);
                    break;
                }
            const std::string name = "a" + std::to_string(at);
            decls += "var " + name + "[" + std::to_string(end - at) + "]; ";
            ctx.slots[at] = {name, at};
            at = end;
        } else {
            const std::string name = "v" + std::to_string(at);
            decls += "var " + name + "; ";
            ctx.slots[at] = {name, at};
            at = at + 1;
        }
    }
    if (!decls.empty() && decls.back() == ' ')
        decls.pop_back();
    ctx.decls = decls;

    lift_detail::strip_frame_ops(list);

    return ctx;
}

void Decompiler::inline_temp_returns(
    std::vector<SNode> &list, const std::vector<SNode> &whole_main,
    const std::vector<std::vector<SNode>> &fns) {
    for (SNode &n : list) {
        inline_temp_returns(n.body, whole_main, fns);
        inline_temp_returns(n.els, whole_main, fns);
    }
    for (std::size_t i = 0; i + 1 < list.size(); ++i) {
        if (list[i].k != SNode::K::Store || !list[i].temp_target)
            continue;
        if (list[i + 1].k != SNode::K::Return || list[i + 1].b != list[i].a)
            continue;
        std::size_t uses = 0;
        lift_detail::count_occurrences(whole_main, list[i].a, uses);
        for (const auto &f : fns)
            lift_detail::count_occurrences(f, list[i].a, uses);
        if (uses != 2)
            continue;
        list[i + 1].b = list[i].b;
        list[i].k = SNode::K::Comment;
        list[i].a = "";
    }
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const SNode &n) {
                                  return n.k == SNode::K::Comment &&
                                         n.a.empty();
                              }),
               list.end());
}

void Decompiler::collect_cells_text(const std::string &t) {
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] != kOpen)
            continue;
        const char kind = t[i + 1];
        if (kind != 'S' && kind != 'X')
            continue;
        long long v = 0;
        std::size_t j = i + 2;
        while (j < t.size() && t[j] >= '0' && t[j] <= '9')
            v = v * 10 + (t[j++] - '0');
        (kind == 'S' ? seen_cells_ : seen_bases_).push_back(v);
    }
}

void Decompiler::collect_cells(const std::vector<SNode> &list) {
    for (const SNode &n : list) {
        collect_cells_text(n.a);
        collect_cells_text(n.b);
        if (n.k == SNode::K::StrData)
            str_regions_.emplace_back(n.n1,
                                      static_cast<long long>(n.data.size()));
        collect_cells(n.body);
        collect_cells(n.els);
    }
}

void Decompiler::build_regions() {
    long long max_cell = 0;
    for (const long long c : seen_cells_)
        max_cell = std::max(max_cell, c);
    for (const long long b : seen_bases_)
        max_cell = std::max(max_cell, b);
    for (const auto &[b, l] : str_regions_)
        max_cell = std::max(max_cell, b + l - 1);

    if (max_cell < 1 || max_cell > 1024) {
        renaming_ = false;
        return;
    }

    std::sort(seen_cells_.begin(), seen_cells_.end());
    seen_cells_.erase(std::unique(seen_cells_.begin(), seen_cells_.end()),
                      seen_cells_.end());
    std::sort(seen_bases_.begin(), seen_bases_.end());
    seen_bases_.erase(std::unique(seen_bases_.begin(), seen_bases_.end()),
                      seen_bases_.end());
    std::sort(str_regions_.begin(), str_regions_.end());

    auto in_string = [&](long long c) {
        for (const auto &[b, l] : str_regions_)
            if (c >= b && c < b + l)
                return true;
        return false;
    };

    long long at = 1;
    while (at <= max_cell) {
        bool made = false;
        for (const auto &[b, l] : str_regions_) {
            if (b == at) {
                regions_.push_back(
                    {b, l, Region::T::Str, "str" + std::to_string(b)});
                at = b + l;
                made = true;
                break;
            }
        }
        if (made)
            continue;

        const bool is_scalar =
            std::binary_search(seen_cells_.begin(), seen_cells_.end(), at) &&
            !in_string(at);
        const bool is_base =
            std::binary_search(seen_bases_.begin(), seen_bases_.end(), at);

        if (is_base && !is_scalar) {
            long long end = max_cell + 1;
            for (const long long c : seen_cells_)
                if (c > at) {
                    end = std::min(end, c);
                    break;
                }
            for (const long long b : seen_bases_)
                if (b > at) {
                    end = std::min(end, b);
                    break;
                }
            for (const auto &[b, l] : str_regions_)
                if (b > at)
                    end = std::min(end, b);
            regions_.push_back(
                {at, end - at, Region::T::Buf, "buf" + std::to_string(at)});
            at = end;
        } else if (is_scalar) {
            regions_.push_back(
                {at, 1, Region::T::Scalar, "g" + std::to_string(at)});
            at += 1;
        } else {
            long long end = max_cell + 1;
            for (const long long c : seen_cells_)
                if (c > at) {
                    end = std::min(end, c);
                    break;
                }
            for (const long long b : seen_bases_)
                if (b > at) {
                    end = std::min(end, b);
                    break;
                }
            for (const auto &[b, l] : str_regions_)
                if (b > at)
                    end = std::min(end, b);
            regions_.push_back(
                {at, end - at, Region::T::Pad, "pad" + std::to_string(at)});
            at = end;
        }
    }
}

std::string Decompiler::cell_name(long long n) const {
    if (renaming_) {
        for (const Region &r : regions_) {
            if (n < r.base || n >= r.base + r.len)
                continue;
            if (r.t == Region::T::Scalar)
                return r.name;
            return r.name + "[" + std::to_string(n - r.base) + "]";
        }
    }
    return "mem[" + std::to_string(n) + "]";
}

std::string Decompiler::cell_indexed(long long base,
                                     const std::string &idx) const {
    if (renaming_) {
        for (const Region &r : regions_) {
            if (base < r.base || base >= r.base + r.len ||
                r.t == Region::T::Scalar)
                continue;
            if (base == r.base)
                return r.name + "[" + idx + "]";
            return r.name + "[" + std::to_string(base - r.base) + " + " + idx +
                   "]";
        }
    }
    return "mem[" + std::to_string(base) + " + " + idx + "]";
}

std::string Decompiler::resolve(const std::string &t,
                                const BodyCtx &ctx) const {
    std::string out;
    out.reserve(t.size());
    for (std::size_t i = 0; i < t.size();) {
        if (t[i] != kOpen) {
            out += t[i++];
            continue;
        }
        const char kind = t[i + 1];
        std::size_t j = i + 2;
        long long v = 0;
        bool neg = j < t.size() && t[j] == '-';
        if (neg)
            ++j;
        while (j < t.size() && t[j] >= '0' && t[j] <= '9')
            v = v * 10 + (t[j++] - '0');
        if (neg)
            v = -v;

        std::string idx;
        if (j < t.size() && t[j] == '|') {
            ++j;
            int depth = 0;
            const std::size_t start = j;
            while (j < t.size()) {
                if (t[j] == kOpen)
                    ++depth;
                else if (t[j] == kClose) {
                    if (depth == 0)
                        break;
                    --depth;
                }
                ++j;
            }
            idx = resolve(t.substr(start, j - start), ctx);
        }
        ++j;

        switch (kind) {
        case 'S':
            out += cell_name(v);
            break;
        case 'X':
            out += cell_indexed(v, idx);
            break;
        case 'P':
            out += (ctx.lifted || ctx.latches.empty())
                       ? "arg" + std::to_string(v)
                       : ctx.latches[static_cast<std::size_t>(v)];
            break;
        case 'F':
        case 'G':
        case 'A': {
            if (ctx.lifted) {
                const auto it = ctx.slots.upper_bound(v);
                if (it != ctx.slots.begin()) {
                    const auto &[name, base] = std::prev(it)->second;
                    const long long off = v - base;
                    const bool is_scalar =
                        std::prev(it)->first == v && off == 0 && name[0] == 'v';
                    if (kind == 'A') {
                        out += off ? name + " + " + std::to_string(off) : name;
                    } else if (kind == 'F') {
                        out += is_scalar
                                   ? name
                                   : name + "[" + std::to_string(off) + "]";
                    } else {
                        out += name + "[" +
                               (off ? std::to_string(off) + " + " : "") + idx +
                               "]";
                    }
                    break;
                }
            }
            if (kind == 'A') {
                out += (v == 0) ? "mem[0]" : "mem[0] + " + std::to_string(v);
            } else if (kind == 'F') {
                out += (v == 0) ? "mem[mem[0]]"
                                : "mem[mem[0] + " + std::to_string(v) + "]";
            } else {
                out += (v == 0) ? "mem[mem[0] + " + idx + "]"
                                : "mem[mem[0] + " + std::to_string(v) + " + " +
                                      idx + "]";
            }
            break;
        }
        default:
            break;
        }
        i = j;
    }
    return out;
}

void Decompiler::print_nodes(const std::vector<SNode> &list, int indent,
                             const BodyCtx &ctx, std::string &out) const {
    auto line = [&](const std::string &s) {
        out.append(static_cast<std::size_t>(indent) * 4, ' ');
        out += s;
        out += '\n';
    };

    for (std::size_t i = 0; i < list.size(); ++i) {
        const SNode &n = list[i];
        switch (n.k) {
        case SNode::K::Plain:
            line(resolve(n.a, ctx));
            break;
        case SNode::K::Comment:
            line("// " + n.a);
            break;
        case SNode::K::Store: {
            std::string s = resolve(n.a, ctx) + " = " + resolve(n.b, ctx) + ";";
            if (n.lit_value && is_printable(n.n2))
                s += " // " + char_literal(n.n2);
            line(s);
            break;
        }
        case SNode::K::Putchar:
            line("putchar(" +
                 (n.lit_value ? char_literal(n.n2) : resolve(n.b, ctx)) + ");");
            break;
        case SNode::K::Print:
            line("print(" + resolve(n.b, ctx) + ");");
            break;
        case SNode::K::Puts:
            line("puts(\"" + escape_string(n.a) + "\");");
            break;
        case SNode::K::Return:
            line("return " + resolve(n.b, ctx) + ";");
            break;
        case SNode::K::Break:
            line(n.a.empty() ? "break;" : "break; // " + n.a);
            break;
        case SNode::K::SpAdjust:
            line("mem[0] = mem[0] " + std::string(n.n1 < 0 ? "- " : "+ ") +
                 std::to_string(n.n1 < 0 ? -n.n1 : n.n1) + ";");
            break;
        case SNode::K::SpInit:
            line("mem[0] = mem.length@SPBASE@; // stack pointer: below the "
                 "temporaries");
            break;
        case SNode::K::StrData: {
            std::string text;
            for (const long long c : n.data)
                if (c != 0)
                    text += static_cast<char>(c);
            std::string s;
            for (std::size_t k2 = 0; k2 < n.data.size(); ++k2) {
                const long long cell = n.n1 + static_cast<long long>(k2);
                s += cell_name(cell) + " = " +
                     (char_worthy(n.data[k2]) ? char_literal(n.data[k2])
                                              : std::to_string(n.data[k2])) +
                     "; ";
                if (s.size() > 72 && k2 + 1 < n.data.size()) {
                    line(s);
                    s.clear();
                }
            }
            if (!s.empty() && s.back() == ' ')
                s.pop_back();
            line(s + " // \"" + escape_string(text) + "\"");
            break;
        }
        case SNode::K::If: {
            line("if (" + resolve(n.a, ctx) + ") {");
            print_nodes(n.body, indent + 1, ctx, out);
            const SNode *els_holder = &n;
            while (els_holder->els.size() == 1 &&
                   els_holder->els[0].k == SNode::K::If) {
                const SNode &e = els_holder->els[0];
                line("} else if (" + resolve(e.a, ctx) + ") {");
                print_nodes(e.body, indent + 1, ctx, out);
                els_holder = &e;
            }
            if (!els_holder->els.empty()) {
                line("} else {");
                print_nodes(els_holder->els, indent + 1, ctx, out);
            }
            line("}");
            break;
        }
        case SNode::K::While:
            line("while (" + resolve(n.a, ctx) + ") {");
            print_nodes(n.body, indent + 1, ctx, out);
            line(n.approx ? "} // carries stack values between iterations; "
                            "approximated"
                          : "}");
            break;
        case SNode::K::DoWhile:
            line("do {");
            print_nodes(n.body, indent + 1, ctx, out);
            line("} while (" + resolve(n.a, ctx) + ");" +
                 (n.approx ? " // carries stack values between iterations; "
                             "approximated"
                           : ""));
            break;
        }
    }
}

std::string Decompiler::run() {
    const std::size_t nprocs = cm_.procs.size();
    arity_.assign(nprocs, 0);

    for (int round = 0; round < 2; ++round) {
        std::vector<std::size_t> next(nprocs);
        for (std::size_t k = 0; k < nprocs; ++k) {
            std::vector<SNode> sink;
            next[k] = walk(body_begin(k), body_end(k), true, arity_[k], sink);
        }
        arity_ = std::move(next);
    }

    temp_count_ = 0;

    std::vector<SNode> main_nodes;
    walk(0, g_.size(), false, 0, main_nodes);
    std::vector<std::vector<SNode>> fn_nodes(nprocs);
    for (std::size_t k = 0; k < nprocs; ++k)
        walk(body_begin(k), body_end(k), true, arity_[k], fn_nodes[k]);

    prune_unreachable(main_nodes);
    whileify(main_nodes);
    merge_putchar_runs(main_nodes);
    group_string_stores(main_nodes);
    recognize_puts_loops(main_nodes);
    std::vector<BodyCtx> ctxs(nprocs);
    for (std::size_t k = 0; k < nprocs; ++k) {
        prune_unreachable(fn_nodes[k]);
        whileify(fn_nodes[k]);
        merge_putchar_runs(fn_nodes[k]);
        ctxs[k] = analyse_body(fn_nodes[k], arity_[k]);
        if (ctxs[k].lifted)
            recognize_puts_loops(fn_nodes[k]);
        inline_temp_returns(fn_nodes[k], main_nodes, fn_nodes);
    }

    const bool has_sp_init =
        !main_nodes.empty() && main_nodes[0].k == SNode::K::SpInit;
    for (std::size_t k = 0; k < nprocs; ++k)
        literalize_sp_inits(fn_nodes[k]);
    if (has_sp_init) {
        SNode head = std::move(main_nodes.front());
        main_nodes.erase(main_nodes.begin());
        literalize_sp_inits(main_nodes);
        main_nodes.insert(main_nodes.begin(), std::move(head));
    } else {
        literalize_sp_inits(main_nodes);
        if (observes_memory()) {
            SNode n;
            n.k = SNode::K::Plain;
            n.a = "mem[0] = 0; // this genome never initialises a stack "
                  "pointer; keep cell 0 at zero";
            main_nodes.insert(main_nodes.begin(), std::move(n));
        }
    }

    collect_cells(main_nodes);
    for (const auto &f : fn_nodes)
        collect_cells(f);
    build_regions();

    std::string out =
        "// decompiled from " + std::to_string(g_.size()) + " genes";
    if (nprocs)
        out += ", " + std::to_string(nprocs) + " procedure(s)";
    out += "\n";
    if (renaming_ && !regions_.empty()) {
        out += "// gN, bufN, strN, procN and argN are reconstructed names; "
               "declaration order pins each to its original cell\n";
        std::string decls;
        for (const Region &r : regions_) {
            decls +=
                r.t == Region::T::Scalar
                    ? "var " + r.name + "; "
                    : "var " + r.name + "[" + std::to_string(r.len) + "]; ";
            if (decls.size() > 72) {
                out += decls;
                out += "\n";
                decls.clear();
            }
        }
        if (!decls.empty()) {
            if (decls.back() == ' ')
                decls.pop_back();
            out += decls;
            out += "\n";
        }
    }

    BodyCtx main_ctx;
    print_nodes(main_nodes, 0, main_ctx, out);

    for (std::size_t k = 0; k < nprocs; ++k) {
        out += "\nfn proc" + std::to_string(k) + "(";
        for (std::size_t a = 0; a < arity_[k]; ++a) {
            if (a)
                out += ", ";
            out += "arg" + std::to_string(a);
        }
        out += ") {\n";
        if (ctxs[k].lifted && !ctxs[k].decls.empty())
            out += "    " + ctxs[k].decls + "\n";
        if (!ctxs[k].lifted) {
            for (std::size_t a = 0; a < ctxs[k].latches.size(); ++a) {
                out += "    " + ctxs[k].latches[a] + " = arg" +
                       std::to_string(a) + "; // latched: the frame moves\n";
            }
        }
        print_nodes(fn_nodes[k], 1, ctxs[k], out);
        out += "}\n";
    }

    const std::string rebase =
        temp_count_ ? " - " + std::to_string(temp_count_) : "";
    const std::string key = "@SPBASE@";
    for (std::size_t at = out.find(key); at != std::string::npos;
         at = out.find(key, at)) {
        out.replace(at, key.size(), rebase);
    }
    return out;
}

} // namespace

std::string decompile(const Genome &g) { return Decompiler(g).run(); }

} // namespace savvy
