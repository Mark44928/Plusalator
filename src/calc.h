#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

struct CalcError {
    std::string msg;
    size_t col; // 1-based column of offending token
    explicit CalcError(std::string m, size_t c = 0)
        : msg(std::move(m)), col(c) {}
};

enum class TokenType {
    Number, Ident, Plus, Minus, Star, Slash, Caret,
    LParen, RParen, Comma, Semicolon, Percent, Fact, Assign, Eof
};

struct Token {
    TokenType type = TokenType::Eof;
    std::string text;
    size_t col = 0;
    double number = 0.0;
};

class Context; // forward

class Expr {
public:
    virtual ~Expr() = default;
    virtual double eval(Context& ctx) const = 0;
    virtual std::unique_ptr<Expr> clone() const = 0;
    virtual bool isPercent() const { return false; }
    virtual double percentBase(Context& /*ctx*/) const { return 0.0; }
};

class Num final : public Expr {
    double v_;
public:
    explicit Num(double v) : v_(v) {}
    double eval(Context&) const override { return v_; }
    std::unique_ptr<Expr> clone() const override { return std::make_unique<Num>(v_); }
};

class Var final : public Expr {
    std::string name_;
public:
    explicit Var(std::string n) : name_(std::move(n)) {}
    const std::string& name() const { return name_; }
    double eval(Context& ctx) const override;
    std::unique_ptr<Expr> clone() const override { return std::make_unique<Var>(name_); }
};

class BinOp final : public Expr {
    char op_;
    std::unique_ptr<Expr> l_, r_;
public:
    BinOp(char op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : op_(op), l_(std::move(l)), r_(std::move(r)) {}
    double eval(Context& ctx) const override;
    std::unique_ptr<Expr> clone() const override {
        return std::make_unique<BinOp>(op_, l_->clone(), r_->clone());
    }
    Expr* left() const { return l_.get(); }
    Expr* right() const { return r_.get(); }
    char op() const { return op_; }
};

class UnaryOp final : public Expr {
    char op_;
    std::unique_ptr<Expr> operand_;
public:
    UnaryOp(char op, std::unique_ptr<Expr> operand)
        : op_(op), operand_(std::move(operand)) {}
    double eval(Context& ctx) const override;
    std::unique_ptr<Expr> clone() const override {
        return std::make_unique<UnaryOp>(op_, operand_->clone());
    }
    char op() const { return op_; }
    Expr* operand() const { return operand_.get(); }
};

class PostfixOp final : public Expr {
    char kind_; // '!' or '%'
    std::unique_ptr<Expr> operand_;
public:
    PostfixOp(char kind, std::unique_ptr<Expr> operand)
        : kind_(kind), operand_(std::move(operand)) {}
    double eval(Context& ctx) const override;
    std::unique_ptr<Expr> clone() const override {
        return std::make_unique<PostfixOp>(kind_, operand_->clone());
    }
    bool isPercent() const override { return kind_ == '%'; }
    double percentBase(Context& ctx) const override { return operand_->eval(ctx) / 100.0; }
};

class Call final : public Expr {
    std::string name_;
    std::vector<std::unique_ptr<Expr>> args_;
public:
    Call(std::string name, std::vector<std::unique_ptr<Expr>> args)
        : name_(std::move(name)), args_(std::move(args)) {}
    const std::string& name() const { return name_; }
    const std::vector<std::unique_ptr<Expr>>& args() const { return args_; }
    double eval(Context& ctx) const override;
    std::unique_ptr<Expr> clone() const override {
        std::vector<std::unique_ptr<Expr>> ca;
        ca.reserve(args_.size());
        for (auto& a : args_) ca.push_back(a->clone());
        return std::make_unique<Call>(name_, std::move(ca));
    }
};

struct UserFunc {
    std::vector<std::string> params;
    std::unique_ptr<Expr> body;
};

class Context {
public:
    std::map<std::string, double> vars;
    std::vector<std::map<std::string, double>> locals;
    std::map<std::string, UserFunc> funcs;
    int callDepth = 0;

    Context();
    double lookup(const std::string& name) const;
    void setVar(const std::string& name, double value);
    void pushFrame();
    void popFrame();
};

class Parser {
    std::string input_;
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    void tokenize();
    Token peek() const;
    Token next();
    bool is(TokenType t) const;

    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parseAssign();
    std::unique_ptr<Expr> parseAdd();
    std::unique_ptr<Expr> parseMul();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePower();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();

public:
    explicit Parser(std::string input);
    std::unique_ptr<Expr> parse();
};

// Evaluate a single statement (no top-level ';' splitting). Handles assignment
// and function definition side effects. Returns the statement's value.
double evalStmt(const std::string& input, Context& ctx);

// Evaluate a full input line, splitting on ';', returning the last result.
double evalLine(const std::string& input, Context& ctx);

std::string formatNumber(double v);
