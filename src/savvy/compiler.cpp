#include "savvy/parser.hpp"
#include "savvy/savvy.hpp"
#include "shrewd/arith.hpp"
#include "shrewd/isa.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace savvy {
namespace {

using shrewd::Gene;
using shrewd::Op;
using shrewd::Value;

constexpr int kSpCell = 0;
constexpr int kGlobalBase = 1;

struct CompileError {
    std::size_t line = 0;
    std::string message;
    std::size_t file = 0;
};

struct VarInfo {
    bool is_local = false;
    int addr = 0;
    bool is_array = false;
};

bool fits_gene(Value v) {
    return v >= std::numeric_limits<Gene>::min() &&
           v <= std::numeric_limits<Gene>::max();
}

std::optional<Value> fold(const Expr &e) {
    switch (e.kind) {
    case Expr::Kind::Number:
        return e.number;

    case Expr::Kind::Unary: {
        const auto a = fold(*e.lhs);
        if (!a)
            return std::nullopt;
        return e.un_op == UnOp::Neg ? shrewd::wrap_sub(0, *a)
                                    : static_cast<Value>(*a == 0);
    }

    case Expr::Kind::Binary: {
        const auto a = fold(*e.lhs);
        if (!a)
            return std::nullopt;
        const auto b = fold(*e.rhs);
        if (!b)
            return std::nullopt;
        switch (e.bin_op) {
        case BinOp::Add:
            return shrewd::wrap_add(*a, *b);
        case BinOp::Sub:
            return shrewd::wrap_sub(*a, *b);
        case BinOp::Mul:
            return shrewd::wrap_mul(*a, *b);
        case BinOp::Div:
            return shrewd::safe_div(*a, *b);
        case BinOp::Mod:
            return shrewd::safe_mod(*a, *b);
        case BinOp::Lt:
            return static_cast<Value>(*a < *b);
        case BinOp::Gt:
            return static_cast<Value>(*a > *b);
        case BinOp::Le:
            return static_cast<Value>(!(*a > *b));
        case BinOp::Ge:
            return static_cast<Value>(!(*a < *b));
        case BinOp::Eq:
            return static_cast<Value>(*a == *b);
        case BinOp::Ne:
            return static_cast<Value>(*a != *b);
        case BinOp::And:
            return static_cast<Value>(*a != 0 && *b != 0);
        case BinOp::Or:
            return static_cast<Value>(*a != 0 || *b != 0);
        }
        return std::nullopt;
    }

    default:
        return std::nullopt;
    }
}

class Compiler {
  public:
    shrewd::Genome compile(const Program &p) {
        for (const Function &f : p.functions) {
            if (!functions_.emplace(f.name, &f).second) {
                throw CompileError{f.line,
                                   "function '" + f.name + "' is defined twice",
                                   f.file};
            }
            proc_index_.emplace(f.name, proc_index_.size());
        }

        scopes_.push_back({});

        scopes_[0].vars.emplace_back("mem", VarInfo{false, 0, true});
        scopes_[0].cells = 0;

        for (const Function &f : p.functions) {
            for (const StmtPtr &s : f.body)
                collect_strings(*s);
        }
        for (const StmtPtr &s : p.main)
            collect_strings(*s);

        emit_sp_init();
        emit_string_data();

        for (const StmtPtr &s : p.main) {
            cur_file_ = s->file;
            compile_stmt(*s);
        }

        for (const Function &f : p.functions) {
            cur_file_ = f.file;
            compile_function(f);
        }

        return std::move(out_);
    }

  private:
    void emit(Op op) { out_.push_back(static_cast<Gene>(op)); }

    void emit_const(Value v, std::size_t line) {
        switch (v) {
        case 0:
            emit(Op::PUSH0);
            return;
        case 1:
            emit(Op::PUSH1);
            return;
        case 2:
            emit(Op::PUSH2);
            return;
        case 10:
            emit(Op::PUSH10);
            return;
        case -1:
            emit(Op::PUSHN1);
            return;
        default:
            break;
        }
        if (v < std::numeric_limits<Gene>::min() ||
            v > std::numeric_limits<Gene>::max()) {
            throw CompileError{line,
                               "literal " + std::to_string(v) +
                                   " does not fit in a gene",
                               cur_file_};
        }
        emit(Op::PUSHI);
        out_.push_back(static_cast<Gene>(v));
    }

    std::size_t emit_pushi_slot(Gene v) {
        emit(Op::PUSHI);
        out_.push_back(v);
        return out_.size() - 1;
    }

    void emit_sp_init() {
        emit_const(kSpCell, 0);
        emit(Op::MSIZE);
        emit(Op::MSTORE);
    }

    void emit_string_data() {
        for (const auto &[text, base] : string_addr_) {
            for (std::size_t i = 0; i < text.size(); ++i) {
                emit_const(base + static_cast<int>(i), 0);
                emit_const(static_cast<unsigned char>(text[i]), 0);
                emit(Op::MSTORE);
            }
            emit_const(base + static_cast<int>(text.size()), 0);
            emit(Op::PUSH0);
            emit(Op::MSTORE);
        }
    }

    void emit_sp_load() {
        emit_const(kSpCell, 0);
        emit(Op::MLOAD);
    }

    void emit_sp_adjust(Op op, std::size_t frame_slot_out[1]) {
        emit_const(kSpCell, 0);
        emit_sp_load();
        frame_slot_out[0] = emit_pushi_slot(0);
        emit(op);
        emit(Op::MSTORE);
    }

    void collect_strings(const Expr &e) {
        if (e.kind == Expr::Kind::String && !string_addr_.count(e.text)) {
            const int base = next_global_;
            next_global_ += static_cast<int>(e.text.size()) + 1;
            string_addr_.emplace(e.text, base);
        }
        if (e.lhs)
            collect_strings(*e.lhs);
        if (e.rhs)
            collect_strings(*e.rhs);
        for (const ExprPtr &a : e.args)
            collect_strings(*a);
    }

    void collect_strings(const Stmt &s) {
        const Expr *puts_literal =
            (s.kind == Stmt::Kind::VoidCall &&
             s.void_builtin == VoidBuiltin::Puts && s.value &&
             s.value->kind == Expr::Kind::String)
                ? s.value.get()
                : nullptr;
        for (const Expr *e :
             {s.base.get(), s.index.get(), s.value.get(), s.cond.get()}) {
            if (e && e != puts_literal)
                collect_strings(*e);
        }
        for (const Stmt *c :
             {s.init.get(), s.step.get(), s.body.get(), s.otherwise.get()}) {
            if (c)
                collect_strings(*c);
        }
        for (const StmtPtr &b : s.body_list)
            collect_strings(*b);
    }

    struct Scope {
        std::vector<std::pair<std::string, VarInfo>> vars;
        int cells = 0;
        bool barrier = false;
    };

    void push_scope(bool barrier = false) {
        Scope s;
        s.barrier = barrier;
        scopes_.push_back(std::move(s));
    }

    void pop_scope() {
        if (in_function_)
            next_local_ -= scopes_.back().cells;
        else
            next_global_ -= scopes_.back().cells;
        scopes_.pop_back();
    }

    VarInfo declare(const std::string &name, int cells, bool is_array) {
        VarInfo v;
        v.is_array = is_array;
        v.is_local = in_function_;
        if (in_function_) {
            v.addr = next_local_;
            next_local_ += cells;
            frame_high_ = std::max(frame_high_, next_local_);
        } else {
            v.addr = next_global_;
            next_global_ += cells;
        }
        scopes_.back().vars.emplace_back(name, v);
        scopes_.back().cells += cells;
        return v;
    }

    const VarInfo *lookup(const std::string &name) const {
        for (std::size_t i = scopes_.size(); i-- > 0;) {
            for (const auto &[n, v] : scopes_[i].vars) {
                if (n == name)
                    return &v;
            }
            if (scopes_[i].barrier)
                break;
        }
        for (const auto &[n, v] : scopes_[0].vars) {
            if (n == name)
                return &v;
        }
        return nullptr;
    }

    const VarInfo &lookup_or_fail(const std::string &name,
                                  std::size_t line) const {
        if (const VarInfo *v = lookup(name))
            return *v;
        throw CompileError{line, "unknown variable '" + name + "'", cur_file_};
    }

    void push_address(const VarInfo &v, std::size_t line) {
        if (!v.is_local) {
            emit_const(v.addr, line);
            return;
        }
        emit_sp_load();
        if (v.addr != 0) {
            emit_const(v.addr, line);
            emit(Op::ADD);
        }
    }

    void compile_expr(const Expr &e) {
        if (e.kind != Expr::Kind::Number) {
            if (const auto v = fold(e); v && fits_gene(*v)) {
                emit_const(*v, e.line);
                return;
            }
        }

        switch (e.kind) {
        case Expr::Kind::Number:
            emit_const(e.number, e.line);
            return;

        case Expr::Kind::String:
            emit_const(string_addr_.at(e.text), e.line);
            return;

        case Expr::Kind::Var: {
            const VarInfo &v = lookup_or_fail(e.name, e.line);
            push_address(v, e.line);
            if (!v.is_array)
                emit(Op::MLOAD);
            return;
        }

        case Expr::Kind::Index:
            compile_expr(*e.lhs);
            compile_expr(*e.rhs);
            emit(Op::ADD);
            emit(Op::MLOAD);
            return;

        case Expr::Kind::Register:
            emit(static_cast<Op>(static_cast<int>(Op::LD0) + e.reg));
            return;

        case Expr::Kind::Unary:
            compile_expr(*e.lhs);
            emit(e.un_op == UnOp::Neg ? Op::NEG : Op::NOT);
            return;

        case Expr::Kind::Binary:
            compile_expr(*e.lhs);
            compile_expr(*e.rhs);
            switch (e.bin_op) {
            case BinOp::Add:
                emit(Op::ADD);
                return;
            case BinOp::Sub:
                emit(Op::SUB);
                return;
            case BinOp::Mul:
                emit(Op::MUL);
                return;
            case BinOp::Div:
                emit(Op::DIV);
                return;
            case BinOp::Mod:
                emit(Op::MOD);
                return;
            case BinOp::Lt:
                emit(Op::LT);
                return;
            case BinOp::Gt:
                emit(Op::GT);
                return;
            case BinOp::Eq:
                emit(Op::EQ);
                return;
            case BinOp::Ne:
                emit(Op::NEQ);
                return;
            case BinOp::Le:
                emit(Op::GT);
                emit(Op::NOT);
                return;
            case BinOp::Ge:
                emit(Op::LT);
                emit(Op::NOT);
                return;
            case BinOp::And:
                emit(Op::AND);
                return;
            case BinOp::Or:
                emit(Op::OR);
                return;
            }
            return;

        case Expr::Kind::Builtin:
            switch (e.builtin) {
            case ValueBuiltin::Input:
                emit(Op::IN);
                return;
            case ValueBuiltin::Rand:
                emit(Op::RAND);
                return;
            case ValueBuiltin::SelfLength:
                emit(Op::GLEN);
                return;
            case ValueBuiltin::ChildLength:
                emit(Op::CLEN);
                return;
            case ValueBuiltin::MemSize:
                emit(Op::MSIZE);
                return;
            case ValueBuiltin::SelfGene:
                compile_expr(*e.lhs);
                emit(Op::GREAD);
                return;
            case ValueBuiltin::ChildGene:
                compile_expr(*e.lhs);
                emit(Op::CREAD);
                return;
            }
            return;

        case Expr::Kind::Call:
            compile_call(e);
            return;
        }
    }

    void compile_call(const Expr &call) {
        const auto it = functions_.find(call.name);
        if (it == functions_.end()) {
            throw CompileError{
                call.line, "unknown function '" + call.name + "'", cur_file_};
        }
        const Function &f = *it->second;
        if (call.args.size() != f.params.size()) {
            throw CompileError{call.line,
                               "'" + f.name + "' takes " +
                                   std::to_string(f.params.size()) +
                                   " argument(s), " +
                                   std::to_string(call.args.size()) + " given",
                               cur_file_};
        }

        for (const ExprPtr &a : call.args)
            compile_expr(*a);

        emit_const(static_cast<Value>(proc_index_.at(f.name)), call.line);
        emit(Op::CALL);
    }

    void compile_function(const Function &f) {
        emit(Op::PROC);

        in_function_ = true;
        next_local_ = 0;
        frame_high_ = 0;
        frame_fixups_.clear();
        push_scope(true);

        std::size_t slot[1];
        emit_sp_adjust(Op::SUB, slot);
        frame_fixups_.push_back(slot[0]);

        std::vector<VarInfo> params;
        params.reserve(f.params.size());
        for (const std::string &p : f.params)
            params.push_back(declare(p, 1, false));

        for (std::size_t i = f.params.size(); i-- > 0;) {
            push_address(params[i], f.line);
            emit(Op::SWAP);
            emit(Op::MSTORE);
        }

        for (const StmtPtr &s : f.body)
            compile_stmt(*s);

        emit(Op::PUSH0);
        emit_frame_restore();
        emit(Op::END);

        pop_scope();
        in_function_ = false;

        for (std::size_t s : frame_fixups_)
            out_[s] = static_cast<Gene>(frame_high_);
    }

    void emit_frame_restore() {
        std::size_t slot[1];
        emit_sp_adjust(Op::ADD, slot);
        frame_fixups_.push_back(slot[0]);
    }

    void compile_stmt(const Stmt &s) {
        switch (s.kind) {
        case Stmt::Kind::VarDecl: {
            if (s.array_len > 0) {
                declare(s.name, static_cast<int>(s.array_len), true);
                return;
            }
            const Expr *init = s.value.get();
            const VarInfo v = declare(s.name, 1, false);
            push_address(v, s.line);
            if (init)
                compile_expr(*init);
            else
                emit(Op::PUSH0);
            emit(Op::MSTORE);
            return;
        }

        case Stmt::Kind::Assign: {
            if (s.reg >= 0) {
                compile_expr(*s.value);
                emit(static_cast<Op>(static_cast<int>(Op::ST0) + s.reg));
                return;
            }
            const VarInfo &v = lookup_or_fail(s.name, s.line);
            if (v.is_array) {
                throw CompileError{
                    s.line, "cannot assign to the array '" + s.name + "'",
                    cur_file_};
            }
            push_address(v, s.line);
            compile_expr(*s.value);
            emit(Op::MSTORE);
            return;
        }

        case Stmt::Kind::StoreIndex:
            compile_expr(*s.base);
            compile_expr(*s.index);
            emit(Op::ADD);
            compile_expr(*s.value);
            emit(Op::MSTORE);
            return;

        case Stmt::Kind::StoreChild:
            compile_expr(*s.index);
            compile_expr(*s.value);
            emit(Op::CWRITE);
            return;

        case Stmt::Kind::VoidCall:
            if (s.void_builtin == VoidBuiltin::Spawn) {
                emit(Op::SPAWN);
                return;
            }
            if (s.void_builtin == VoidBuiltin::Puts) {
                compile_puts(*s.value);
                return;
            }
            compile_expr(*s.value);
            switch (s.void_builtin) {
            case VoidBuiltin::Print:
                emit(Op::OUTNUM);
                return;
            case VoidBuiltin::Putchar:
                emit(Op::OUT);
                return;
            case VoidBuiltin::Emit:
                emit(Op::EMIT);
                return;
            case VoidBuiltin::Puts:
                return;
            case VoidBuiltin::Spawn:
                return;
            }
            return;

        case Stmt::Kind::ExprStmt:
            if (fold(*s.value))
                return;
            compile_expr(*s.value);
            emit(Op::POP);
            return;

        case Stmt::Kind::Block:
            push_scope();
            for (const StmtPtr &b : s.body_list)
                compile_stmt(*b);
            pop_scope();
            return;

        case Stmt::Kind::If:
            if (const auto c = fold(*s.cond)) {
                if (*c != 0)
                    compile_stmt(*s.body);
                else if (s.otherwise)
                    compile_stmt(*s.otherwise);
                return;
            }
            compile_expr(*s.cond);
            emit(Op::IF);
            compile_stmt(*s.body);
            if (s.otherwise) {
                emit(Op::ELSE);
                compile_stmt(*s.otherwise);
            }
            emit(Op::END);
            return;

        case Stmt::Kind::While:
            if (const auto c = fold(*s.cond)) {
                if (*c == 0)
                    return;
                emit(Op::DO);
                compile_stmt(*s.body);
                emit(Op::PUSH1);
                emit(Op::END);
                return;
            }
            emit(Op::DO);
            compile_expr(*s.cond);
            emit(Op::NOT);
            emit(Op::IF);
            emit(Op::BREAK);
            emit(Op::END);
            compile_stmt(*s.body);
            emit(Op::PUSH1);
            emit(Op::END);
            return;

        case Stmt::Kind::DoWhile:
            emit(Op::DO);
            compile_stmt(*s.body);
            compile_expr(*s.cond);
            emit(Op::END);
            return;

        case Stmt::Kind::For: {
            push_scope();
            if (s.init)
                compile_stmt(*s.init);
            const auto c =
                s.cond ? fold(*s.cond) : std::optional<Value>(Value{1});
            if (c && *c == 0) {
                pop_scope();
                return;
            }
            emit(Op::DO);
            if (s.cond && !c) {
                compile_expr(*s.cond);
                emit(Op::NOT);
                emit(Op::IF);
                emit(Op::BREAK);
                emit(Op::END);
            }
            compile_stmt(*s.body);
            if (s.step)
                compile_stmt(*s.step);
            emit(Op::PUSH1);
            emit(Op::END);
            pop_scope();
            return;
        }

        case Stmt::Kind::Break:
            emit(Op::BREAK);
            return;

        case Stmt::Kind::Return:
            if (s.value)
                compile_expr(*s.value);
            else
                emit(Op::PUSH0);
            if (in_function_)
                emit_frame_restore();
            emit(Op::RET);
            return;
        }
    }

    void compile_puts(const Expr &e) {
        if (e.kind == Expr::Kind::String) {
            for (char c : e.text) {
                emit_const(static_cast<unsigned char>(c), e.line);
                emit(Op::OUT);
            }
            return;
        }
        compile_expr(e);
        emit(Op::DO);
        emit(Op::DUP);
        emit(Op::MLOAD);
        emit(Op::NOT);
        emit(Op::IF);
        emit(Op::BREAK);
        emit(Op::END);
        emit(Op::DUP);
        emit(Op::MLOAD);
        emit(Op::OUT);
        emit(Op::INC);
        emit(Op::PUSH1);
        emit(Op::END);
        emit(Op::POP);
    }

    shrewd::Genome out_;
    std::vector<Scope> scopes_;
    std::unordered_map<std::string, const Function *> functions_;
    std::map<std::string, int> string_addr_;
    std::unordered_map<std::string, std::size_t> proc_index_;
    std::vector<std::size_t> frame_fixups_;

    int next_global_ = kGlobalBase;
    int next_local_ = 0;
    int frame_high_ = 0;
    bool in_function_ = false;
    std::size_t cur_file_ = 0;
};

} // namespace

std::string Error::format(std::string_view filename) const {
    const std::string_view where = file.empty() ? filename : file;
    return std::string(where) + ":" + std::to_string(line) + ":" +
           std::to_string(column) + ": error: " + message;
}

std::optional<shrewd::Genome> compile(std::string_view src, const Options &opts,
                                      Error *error) {
    Program p;
    auto name_of = [&](std::size_t file) {
        return file < p.files.size() ? p.files[file] : std::string();
    };
    try {
        parse_into(p, src, opts.resolver, opts.entry);
        return Compiler().compile(p);
    } catch (const ParseError &e) {
        if (error)
            *error = Error{e.line, e.column, e.message, name_of(e.file)};
    } catch (const CompileError &e) {
        if (error)
            *error = Error{e.line, 1, e.message, name_of(e.file)};
    }
    return std::nullopt;
}

std::optional<shrewd::Genome> compile(std::string_view src, Error *error) {
    return compile(src, Options{}, error);
}

} // namespace savvy
