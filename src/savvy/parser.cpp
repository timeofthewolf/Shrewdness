#include "savvy/parser.hpp"

#include <cctype>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace savvy {
namespace {

enum class Tok {
    End,
    Number,
    String,
    Ident,
    Punct,
    Keyword,
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    shrewd::Value number = 0;
    std::size_t line = 1, column = 1;
};

const char *const kKeywords[] = {"var", "if", "else",  "while",  "do",
                                 "for", "fn", "break", "return", "include"};

bool is_keyword(std::string_view s) {
    for (const char *k : kKeywords) {
        if (s == k)
            return true;
    }
    return false;
}

bool ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

class Lexer {
  public:
    Lexer(std::string_view src) : src_(src) {}

    Token next() {
        skip_trivia();
        Token t;
        t.line = line_;
        t.column = column_;
        if (i_ >= src_.size()) {
            t.kind = Tok::End;
            return t;
        }

        const char c = src_[i_];

        if (std::isdigit(static_cast<unsigned char>(c))) {
            constexpr shrewd::Value kMax =
                std::numeric_limits<shrewd::Value>::max();
            shrewd::Value v = 0;
            while (i_ < src_.size() &&
                   std::isdigit(static_cast<unsigned char>(src_[i_]))) {
                const shrewd::Value d = src_[i_] - '0';
                if (v > (kMax - d) / 10)
                    fail(t, "number literal is too large");
                v = v * 10 + d;
                advance();
            }
            t.kind = Tok::Number;
            t.number = v;
            return t;
        }

        if (c == '\'') {
            advance();
            if (i_ >= src_.size())
                fail(t, "unterminated character literal");
            const char ch = read_char(t);
            if (i_ >= src_.size() || src_[i_] != '\'')
                fail(t, "unterminated character literal");
            advance();
            t.kind = Tok::Number;
            t.number = static_cast<unsigned char>(ch);
            return t;
        }

        if (c == '"') {
            advance();
            std::string s;
            while (i_ < src_.size() && src_[i_] != '"') {
                if (src_[i_] == '\n')
                    fail(t, "unterminated string literal");
                s.push_back(read_char(t));
            }
            if (i_ >= src_.size())
                fail(t, "unterminated string literal");
            advance();
            t.kind = Tok::String;
            t.text = std::move(s);
            return t;
        }

        if (ident_start(c)) {
            const std::size_t start = i_;
            while (i_ < src_.size() && ident_char(src_[i_]))
                advance();
            t.text = std::string(src_.substr(start, i_ - start));
            t.kind = is_keyword(t.text) ? Tok::Keyword : Tok::Ident;
            return t;
        }

        static const char *const kTwo[] = {"==", "!=", "<=", ">=", "&&", "||"};
        for (const char *op : kTwo) {
            if (src_.compare(i_, 2, op) == 0) {
                advance();
                advance();
                t.kind = Tok::Punct;
                t.text = op;
                return t;
            }
        }

        advance();
        t.kind = Tok::Punct;
        t.text = std::string(1, c);
        return t;
    }

  private:
    char read_char(const Token &t) {
        char ch = src_[i_];
        advance();
        if (ch != '\\')
            return ch;

        if (i_ >= src_.size())
            fail(t, "unterminated escape");
        const char e = src_[i_];
        advance();
        switch (e) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '0':
            return '\0';
        case '\\':
            return '\\';
        case '\'':
            return '\'';
        case '"':
            return '"';
        default:
            fail(t, std::string("unknown escape '\\") + e + "'");
        }
    }

    void advance() {
        if (src_[i_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++i_;
    }

    void skip_trivia() {
        for (;;) {
            while (i_ < src_.size() &&
                   std::isspace(static_cast<unsigned char>(src_[i_])))
                advance();
            if (src_.compare(i_, 2, "//") == 0) {
                while (i_ < src_.size() && src_[i_] != '\n')
                    advance();
                continue;
            }
            if (src_.compare(i_, 2, "/*") == 0) {
                const std::size_t open_line = line_, open_col = column_;
                advance();
                advance();
                while (i_ < src_.size() && src_.compare(i_, 2, "*/") != 0)
                    advance();
                if (i_ >= src_.size()) {
                    throw ParseError{open_line, open_col,
                                     "unterminated /* comment"};
                }
                advance();
                advance();
                continue;
            }
            return;
        }
    }

    [[noreturn]] void fail(const Token &t, std::string msg) {
        throw ParseError{t.line, t.column, std::move(msg)};
    }

    std::string_view src_;
    std::size_t i_ = 0, line_ = 1, column_ = 1;
};

class ParseDriver;

class Parser {
  public:
    Parser(std::string_view src, std::size_t file_id, ParseDriver &driver)
        : lexer_(src), file_id_(file_id), driver_(driver) {
        tok_ = lexer_.next();
    }

    void parse_program_into(Program &p) {
        try {
            while (tok_.kind != Tok::End) {
                if (is_kw("include")) {
                    parse_include(p);
                } else if (is_kw("fn")) {
                    p.functions.push_back(parse_function());
                    p.functions.back().file = file_id_;
                } else {
                    p.main.push_back(parse_statement());
                    p.main.back()->file = file_id_;
                }
            }
        } catch (ParseError &e) {
            if (e.file == static_cast<std::size_t>(-1))
                e.file = file_id_;
            throw;
        }
    }

  private:
    void parse_include(Program &p);
    void bump() { tok_ = lexer_.next(); }

    bool is_punct(std::string_view s) const {
        return tok_.kind == Tok::Punct && tok_.text == s;
    }
    bool is_kw(std::string_view s) const {
        return tok_.kind == Tok::Keyword && tok_.text == s;
    }

    bool accept_punct(std::string_view s) {
        if (!is_punct(s))
            return false;
        bump();
        return true;
    }
    bool accept_kw(std::string_view s) {
        if (!is_kw(s))
            return false;
        bump();
        return true;
    }

    void expect_punct(std::string_view s) {
        if (!accept_punct(s))
            fail("expected '" + std::string(s) + "'");
    }

    std::string expect_ident() {
        if (tok_.kind != Tok::Ident)
            fail("expected a name");
        std::string s = tok_.text;
        bump();
        return s;
    }

    [[noreturn]] void fail(std::string msg) {
        std::string got;
        switch (tok_.kind) {
        case Tok::End:
            got = "end of input";
            break;
        case Tok::Number:
            got = "number " + std::to_string(tok_.number);
            break;
        default:
            got = "'" + tok_.text + "'";
            break;
        }
        throw ParseError{tok_.line, tok_.column, msg + ", found " + got};
    }

    Function parse_function() {
        Function f;
        f.line = tok_.line;
        accept_kw("fn");
        f.name = expect_ident();
        expect_punct("(");
        if (!is_punct(")")) {
            do {
                f.params.push_back(expect_ident());
            } while (accept_punct(","));
        }
        expect_punct(")");
        expect_punct("{");
        while (!is_punct("}")) {
            if (tok_.kind == Tok::End)
                fail("unterminated function body");
            f.body.push_back(parse_statement());
        }
        expect_punct("}");
        return f;
    }

    StmtPtr parse_block_stmt() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Block;
        s->line = tok_.line;
        expect_punct("{");
        while (!is_punct("}")) {
            if (tok_.kind == Tok::End)
                fail("unterminated block");
            s->body_list.push_back(parse_statement());
        }
        expect_punct("}");
        return s;
    }

    StmtPtr parse_statement() {
        const std::size_t line = tok_.line;

        if (is_kw("include"))
            fail("'include' is only allowed at the top level of a file");

        if (is_punct("{"))
            return parse_block_stmt();

        if (accept_kw("var")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::VarDecl;
            s->line = line;
            s->name = expect_ident();

            if (accept_punct("[")) {
                if (tok_.kind != Tok::Number)
                    fail("an array length must be a literal");
                s->array_len = tok_.number;
                if (s->array_len <= 0)
                    fail("an array length must be positive");
                bump();
                expect_punct("]");
                expect_punct(";");
                return s;
            }

            if (accept_punct("="))
                s->value = parse_expr();
            expect_punct(";");
            return s;
        }

        if (accept_kw("if")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::If;
            s->line = line;
            expect_punct("(");
            s->cond = parse_expr();
            expect_punct(")");
            s->body = parse_block_stmt();
            if (accept_kw("else")) {
                s->otherwise =
                    is_kw("if") ? parse_statement() : parse_block_stmt();
            }
            return s;
        }

        if (accept_kw("while")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::While;
            s->line = line;
            expect_punct("(");
            s->cond = parse_expr();
            expect_punct(")");
            s->body = parse_block_stmt();
            return s;
        }

        if (accept_kw("do")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::DoWhile;
            s->line = line;
            s->body = parse_block_stmt();
            if (!accept_kw("while"))
                fail("expected 'while' after 'do' block");
            expect_punct("(");
            s->cond = parse_expr();
            expect_punct(")");
            expect_punct(";");
            return s;
        }

        if (accept_kw("for")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::For;
            s->line = line;
            expect_punct("(");
            if (!is_punct(";")) {
                s->init = parse_statement();
            } else {
                expect_punct(";");
            }
            if (!is_punct(";"))
                s->cond = parse_expr();
            expect_punct(";");
            if (!is_punct(")"))
                s->step = parse_simple_statement();
            expect_punct(")");
            s->body = parse_block_stmt();
            return s;
        }

        if (accept_kw("break")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Break;
            s->line = line;
            expect_punct(";");
            return s;
        }

        if (accept_kw("return")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Return;
            s->line = line;
            if (!is_punct(";"))
                s->value = parse_expr();
            expect_punct(";");
            return s;
        }

        StmtPtr s = parse_simple_statement();
        expect_punct(";");
        return s;
    }

    StmtPtr parse_simple_statement() {
        const std::size_t line = tok_.line;

        if (tok_.kind == Tok::Ident) {
            static const std::unordered_map<std::string, VoidBuiltin> kVoid = {
                {"print", VoidBuiltin::Print},
                {"putchar", VoidBuiltin::Putchar},
                {"puts", VoidBuiltin::Puts},
                {"emit", VoidBuiltin::Emit},
                {"spawn", VoidBuiltin::Spawn},
            };
            const auto it = kVoid.find(tok_.text);
            if (it != kVoid.end()) {
                Lexer probe = lexer_;
                Token after = probe.next();
                if (after.kind == Tok::Punct && after.text == "(") {
                    auto s = std::make_unique<Stmt>();
                    s->kind = Stmt::Kind::VoidCall;
                    s->line = line;
                    s->void_builtin = it->second;
                    bump();
                    bump();
                    if (!is_punct(")"))
                        s->value = parse_expr();
                    expect_punct(")");
                    if (it->second == VoidBuiltin::Spawn && s->value) {
                        throw ParseError{line, 1, "spawn() takes no arguments"};
                    }
                    if (it->second != VoidBuiltin::Spawn && !s->value) {
                        throw ParseError{line, 1,
                                         "this builtin needs one argument"};
                    }
                    return s;
                }
            }
        }

        ExprPtr e = parse_expr();

        if (accept_punct("=")) {
            auto s = std::make_unique<Stmt>();
            s->line = line;
            s->value = parse_expr();

            switch (e->kind) {
            case Expr::Kind::Var:
                s->kind = Stmt::Kind::Assign;
                s->name = e->name;
                s->reg = -1;
                return s;
            case Expr::Kind::Register:
                s->kind = Stmt::Kind::Assign;
                s->reg = e->reg;
                return s;
            case Expr::Kind::Index:
                s->kind = Stmt::Kind::StoreIndex;
                s->base = std::move(e->lhs);
                s->index = std::move(e->rhs);
                return s;
            case Expr::Kind::Builtin:
                if (e->builtin == ValueBuiltin::ChildGene) {
                    s->kind = Stmt::Kind::StoreChild;
                    s->index = std::move(e->lhs);
                    return s;
                }
                if (e->builtin == ValueBuiltin::SelfGene) {
                    throw ParseError{
                        line, 1,
                        "cannot assign to self[]: an organism reads its own "
                        "genes but writes to the child, via emit() or child[]"};
                }
                [[fallthrough]];
            default:
                throw ParseError{line, 1, "cannot assign to this"};
            }
        }

        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::ExprStmt;
        s->line = line;
        s->value = std::move(e);
        return s;
    }

    static int register_index(std::string_view name) {
        if (name.size() == 2 && name[0] == 'r' && name[1] >= '0' &&
            name[1] <= '3') {
            return name[1] - '0';
        }
        return -1;
    }

    ExprPtr parse_expr() { return parse_or(); }

    ExprPtr binary(BinOp op, ExprPtr a, ExprPtr b, std::size_t line) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Binary;
        e->bin_op = op;
        e->line = line;
        e->lhs = std::move(a);
        e->rhs = std::move(b);
        return e;
    }

    ExprPtr parse_or() {
        ExprPtr a = parse_and();
        while (is_punct("||")) {
            const std::size_t line = tok_.line;
            bump();
            a = binary(BinOp::Or, std::move(a), parse_and(), line);
        }
        return a;
    }

    ExprPtr parse_and() {
        ExprPtr a = parse_equality();
        while (is_punct("&&")) {
            const std::size_t line = tok_.line;
            bump();
            a = binary(BinOp::And, std::move(a), parse_equality(), line);
        }
        return a;
    }

    ExprPtr parse_equality() {
        ExprPtr a = parse_relational();
        for (;;) {
            const std::size_t line = tok_.line;
            if (is_punct("==")) {
                bump();
                a = binary(BinOp::Eq, std::move(a), parse_relational(), line);
            } else if (is_punct("!=")) {
                bump();
                a = binary(BinOp::Ne, std::move(a), parse_relational(), line);
            } else {
                return a;
            }
        }
    }

    ExprPtr parse_relational() {
        ExprPtr a = parse_additive();
        for (;;) {
            const std::size_t line = tok_.line;
            BinOp op;
            if (is_punct("<"))
                op = BinOp::Lt;
            else if (is_punct(">"))
                op = BinOp::Gt;
            else if (is_punct("<="))
                op = BinOp::Le;
            else if (is_punct(">="))
                op = BinOp::Ge;
            else
                return a;
            bump();
            a = binary(op, std::move(a), parse_additive(), line);
        }
    }

    ExprPtr parse_additive() {
        ExprPtr a = parse_multiplicative();
        for (;;) {
            const std::size_t line = tok_.line;
            if (is_punct("+")) {
                bump();
                a = binary(BinOp::Add, std::move(a), parse_multiplicative(),
                           line);
            } else if (is_punct("-")) {
                bump();
                a = binary(BinOp::Sub, std::move(a), parse_multiplicative(),
                           line);
            } else {
                return a;
            }
        }
    }

    ExprPtr parse_multiplicative() {
        ExprPtr a = parse_unary();
        for (;;) {
            const std::size_t line = tok_.line;
            BinOp op;
            if (is_punct("*"))
                op = BinOp::Mul;
            else if (is_punct("/"))
                op = BinOp::Div;
            else if (is_punct("%"))
                op = BinOp::Mod;
            else
                return a;
            bump();
            a = binary(op, std::move(a), parse_unary(), line);
        }
    }

    ExprPtr parse_unary() {
        const std::size_t line = tok_.line;
        if (is_punct("-") || is_punct("!")) {
            const UnOp op = is_punct("-") ? UnOp::Neg : UnOp::Not;
            bump();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Unary;
            e->un_op = op;
            e->line = line;
            e->lhs = parse_unary();
            return e;
        }
        return parse_postfix();
    }

    ExprPtr parse_postfix() {
        ExprPtr e = parse_primary();
        while (is_punct("[")) {
            const std::size_t line = tok_.line;
            bump();
            auto idx = std::make_unique<Expr>();
            idx->kind = Expr::Kind::Index;
            idx->line = line;
            idx->lhs = std::move(e);
            idx->rhs = parse_expr();
            expect_punct("]");
            e = std::move(idx);
        }
        return e;
    }

    ExprPtr value_builtin(ValueBuiltin b, ExprPtr index, std::size_t line) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Builtin;
        e->builtin = b;
        e->line = line;
        e->lhs = std::move(index);
        return e;
    }

    ExprPtr parse_primary() {
        const std::size_t line = tok_.line;

        if (tok_.kind == Tok::Number) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Number;
            e->number = tok_.number;
            e->line = line;
            bump();
            return e;
        }

        if (tok_.kind == Tok::String) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::String;
            e->text = tok_.text;
            e->line = line;
            bump();
            return e;
        }

        if (accept_punct("(")) {
            ExprPtr e = parse_expr();
            expect_punct(")");
            return e;
        }

        if (tok_.kind == Tok::Ident) {
            const std::string name = tok_.text;
            bump();

            if (name == "self" || name == "child") {
                if (accept_punct(".")) {
                    const std::string field = expect_ident();
                    if (field != "length")
                        fail("only .length exists on '" + name + "'");
                    return value_builtin(name == "self"
                                             ? ValueBuiltin::SelfLength
                                             : ValueBuiltin::ChildLength,
                                         nullptr, line);
                }
                expect_punct("[");
                ExprPtr idx = parse_expr();
                expect_punct("]");
                return value_builtin(name == "self" ? ValueBuiltin::SelfGene
                                                    : ValueBuiltin::ChildGene,
                                     std::move(idx), line);
            }
            if (name == "mem" && is_punct(".")) {
                bump();
                const std::string field = expect_ident();
                if (field != "length")
                    fail("only .length exists on 'mem'");
                return value_builtin(ValueBuiltin::MemSize, nullptr, line);
            }
            if (name == "input" || name == "rand") {
                expect_punct("(");
                expect_punct(")");
                return value_builtin(name == "input" ? ValueBuiltin::Input
                                                     : ValueBuiltin::Rand,
                                     nullptr, line);
            }

            if (accept_punct("(")) {
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Kind::Call;
                e->name = name;
                e->line = line;
                if (!is_punct(")")) {
                    do {
                        e->args.push_back(parse_expr());
                    } while (accept_punct(","));
                }
                expect_punct(")");
                return e;
            }

            auto e = std::make_unique<Expr>();
            e->line = line;
            if (const int r = register_index(name); r >= 0) {
                e->kind = Expr::Kind::Register;
                e->reg = r;
            } else {
                e->kind = Expr::Kind::Var;
                e->name = name;
            }
            return e;
        }

        fail("expected an expression");
    }

    Lexer lexer_;
    Token tok_;
    std::size_t file_id_;
    ParseDriver &driver_;
};

class ParseDriver {
  public:
    explicit ParseDriver(const Resolver *resolver) : resolver_(resolver) {}

    void run(Program &p, std::string_view src, const std::string &entry_key) {
        included_.insert(entry_key);
        p.files.push_back(entry_key);
        Parser(src, 0, *this).parse_program_into(p);
    }

    void include(Program &p, const std::string &name, std::size_t from_file,
                 std::size_t line, std::size_t column) {
        if (!resolver_ || !*resolver_) {
            throw ParseError{line, column,
                             "include is not available here (no resolver)",
                             from_file};
        }
        auto s = (*resolver_)(name, p.files[from_file]);
        if (!s) {
            throw ParseError{line, column,
                             "cannot find include \"" + name + "\"", from_file};
        }
        if (!included_.insert(s->key).second)
            return;
        texts_.push_back(std::make_unique<std::string>(std::move(s->text)));
        const std::size_t id = p.files.size();
        p.files.push_back(std::move(s->key));
        Parser(*texts_.back(), id, *this).parse_program_into(p);
    }

  private:
    const Resolver *resolver_;
    std::set<std::string> included_;
    std::vector<std::unique_ptr<std::string>> texts_;
};

void Parser::parse_include(Program &p) {
    const std::size_t line = tok_.line, column = tok_.column;
    bump();
    if (tok_.kind != Tok::String)
        fail("expected a file name string after 'include'");
    const std::string name = tok_.text;
    bump();
    expect_punct(";");
    driver_.include(p, name, file_id_, line, column);
}

} // namespace

Program parse(std::string_view src) {
    Program p;
    ParseDriver d(nullptr);
    d.run(p, src, "");
    return p;
}

void parse_into(Program &p, std::string_view src, const Resolver &resolver,
                const std::string &entry_key) {
    ParseDriver d(&resolver);
    d.run(p, src, entry_key);
}

} // namespace savvy
