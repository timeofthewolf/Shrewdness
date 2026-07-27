#pragma once

#include "shrewd/genome.hpp"

#include <memory>
#include <string>
#include <vector>

namespace savvy {

enum class BinOp { Add, Sub, Mul, Div, Mod, Lt, Gt, Le, Ge, Eq, Ne, And, Or };
enum class UnOp { Neg, Not };

enum class ValueBuiltin {
    Input,
    Rand,
    SelfLength,
    SelfGene,
    ChildLength,
    ChildGene,
    MemSize,
};

enum class VoidBuiltin {
    Print,
    Putchar,
    Puts,
    Emit,
    Spawn,
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum class Kind {
        Number,
        String,
        Var,
        Register,
        Binary,
        Unary,
        Builtin,
        Call,
        Index
    } kind;
    std::size_t line = 0;

    shrewd::Value number = 0;
    std::string text;
    std::string name;
    int reg = 0;

    BinOp bin_op = BinOp::Add;
    UnOp un_op = UnOp::Neg;
    ValueBuiltin builtin = ValueBuiltin::Input;

    ExprPtr lhs, rhs;
    std::vector<ExprPtr> args;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
    enum class Kind {
        VarDecl,
        Assign,
        StoreIndex,
        StoreChild,
        VoidCall,
        ExprStmt,
        If,
        While,
        DoWhile,
        For,
        Break,
        Return,
        Block,
    } kind;
    std::size_t line = 0;
    std::size_t file = 0;

    std::string name;
    int reg = -1;
    VoidBuiltin void_builtin = VoidBuiltin::Print;

    shrewd::Value array_len = 0;

    ExprPtr base, index, value, cond;
    StmtPtr init, step, body, otherwise;

    std::vector<StmtPtr> body_list;
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    std::vector<StmtPtr> body;
    std::size_t line = 0;
    std::size_t file = 0;
};

struct Program {
    std::vector<Function> functions;
    std::vector<StmtPtr> main;

    std::vector<std::string> files;
};

} // namespace savvy
