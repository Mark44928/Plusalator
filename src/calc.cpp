#include "calc.h"
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <algorithm>
#include <sstream>

static bool isIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
static bool isIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

static bool startsValue(TokenType t) {
    return t == TokenType::Number || t == TokenType::Ident || t == TokenType::LParen;
}

// ---------- tokenizer ----------

Parser::Parser(std::string input) : input_(std::move(input)) {}

void Parser::tokenize() {
    tokens_.clear();
    size_t i = 0, n = input_.size();
    size_t col = 1;
    while (i < n) {
        char c = input_[i];
        if (c == ' ' || c == '\t') { col++; i++; continue; }
        if (c == '\n' || c == '\r') { col = 1; i++; continue; }

        size_t tokCol = col;

        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)input_[i + 1]))) {
            std::string lex;
            bool isHex = false, isBin = false, isOct = false;
            if (c == '0' && i + 1 < n) {
                char n2 = input_[i + 1];
                if (n2 == 'x' || n2 == 'X') { isHex = true; lex = "0x"; i += 2; col += 2; }
                else if (n2 == 'b' || n2 == 'B') { isBin = true; lex = "0b"; i += 2; col += 2; }
                else if (n2 == 'o' || n2 == 'O') { isOct = true; lex = "0o"; i += 2; col += 2; }
            }
            if (isHex || isBin || isOct) {
                while (i < n) {
                    char d = input_[i];
                    bool ok;
                    if (isHex) ok = std::isxdigit((unsigned char)d);
                    else if (isBin) ok = (d == '0' || d == '1');
                    else ok = (d >= '0' && d <= '7');
                    if (!ok) break;
                    lex += d; i++; col++;
                }
                // strip the base prefix before converting
                std::string digits = lex.substr(2);
                int base = isHex ? 16 : (isBin ? 2 : 8);
                double v = 0;
                for (char d : digits) {
                    int digit = -1;
                    if (d >= '0' && d <= '9') digit = d - '0';
                    else if (d >= 'a' && d <= 'f') digit = d - 'a' + 10;
                    else if (d >= 'A' && d <= 'F') digit = d - 'A' + 10;
                    if (digit < 0 || digit >= base) break;
                    v = v * base + digit;
                }
                tokens_.push_back({TokenType::Number, lex, tokCol, v});
            } else {
                std::string lex;
                bool hasDot = false;
                if (c == '.') { lex = "."; i++; col++; hasDot = true; }
                while (i < n) {
                    char d = input_[i];
                    if (std::isdigit((unsigned char)d)) { lex += d; i++; col++; }
                    else if (d == '.' && !hasDot) { lex += d; i++; col++; hasDot = true; }
                    else break;
                }
                if (i < n && (input_[i] == 'e' || input_[i] == 'E')) {
                    char expsign = 0;
                    size_t j = i + 1;
                    if (j < n && (input_[j] == '+' || input_[j] == '-')) expsign = input_[j], j++;
                    if (j < n && std::isdigit((unsigned char)input_[j])) {
                        lex += input_[i]; i++; col++;
                        if (expsign) { lex += expsign; i++; col++; }
                        while (i < n && std::isdigit((unsigned char)input_[i])) { lex += input_[i]; i++; col++; }
                    }
                }
                char* end = nullptr;
                errno = 0;
                double v = std::strtod(lex.c_str(), &end);
                if (errno == ERANGE) v = std::numeric_limits<double>::infinity();
                tokens_.push_back({TokenType::Number, lex, tokCol, v});
            }
            continue;
        }

        if (isIdentStart(c)) {
            std::string lex;
            while (i < n && isIdentChar(input_[i])) { lex += input_[i]; i++; col++; }
            tokens_.push_back({TokenType::Ident, lex, tokCol, 0.0});
            continue;
        }

        switch (c) {
            case '+': tokens_.push_back({TokenType::Plus, "+", tokCol, 0.0}); break;
            case '-': tokens_.push_back({TokenType::Minus, "-", tokCol, 0.0}); break;
            case '*': tokens_.push_back({TokenType::Star, "*", tokCol, 0.0}); break;
            case '/': tokens_.push_back({TokenType::Slash, "/", tokCol, 0.0}); break;
            case '^': tokens_.push_back({TokenType::Caret, "^", tokCol, 0.0}); break;
            case '(': tokens_.push_back({TokenType::LParen, "(", tokCol, 0.0}); break;
            case ')': tokens_.push_back({TokenType::RParen, ")", tokCol, 0.0}); break;
            case ',': tokens_.push_back({TokenType::Comma, ",", tokCol, 0.0}); break;
            case ';': tokens_.push_back({TokenType::Semicolon, ";", tokCol, 0.0}); break;
            case '%': tokens_.push_back({TokenType::Percent, "%", tokCol, 0.0}); break;
            case '!': tokens_.push_back({TokenType::Fact, "!", tokCol, 0.0}); break;
            case '=': tokens_.push_back({TokenType::Assign, "=", tokCol, 0.0}); break;
            default: throw CalcError("unexpected character", tokCol);
        }
        i++; col++;
    }
    tokens_.push_back({TokenType::Eof, "", col, 0.0});
    pos_ = 0;
}

Token Parser::peek() const { return tokens_[pos_]; }
Token Parser::next() { return tokens_[pos_++]; }
bool Parser::is(TokenType t) const { return tokens_[pos_].type == t; }

// ---------- parser ----------

std::unique_ptr<Expr> Parser::parse() {
    tokenize();
    auto expr = parseExpr();
    if (!is(TokenType::Eof)) {
        Token t = peek();
        throw CalcError("unexpected trailing input", t.col);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseAssign(); }

std::unique_ptr<Expr> Parser::parseAssign() {
    auto left = parseAdd();
    if (is(TokenType::Assign)) {
        next(); // consume '='
        auto right = parseAssign();
        if (dynamic_cast<Var*>(left.get())) {
            return std::make_unique<BinOp>('=', std::move(left), std::move(right));
        }
        if (auto* call = dynamic_cast<Call*>(left.get())) {
            bool allParams = true;
            for (auto& a : call->args()) {
                if (!dynamic_cast<Var*>(a.get())) { allParams = false; break; }
            }
            if (allParams && !call->args().empty()) {
                return std::make_unique<BinOp>('=', std::move(left), std::move(right));
            }
            throw CalcError("invalid assignment target", 0);
        }
        throw CalcError("invalid assignment target", 0);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAdd() {
    auto left = parseMul();
    while (is(TokenType::Plus) || is(TokenType::Minus)) {
        char op = next().text[0];
        auto right = parseMul();
        left = std::make_unique<BinOp>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseMul() {
    auto left = parseUnary();
    while (true) {
        if (is(TokenType::Star) || is(TokenType::Slash)) {
            char op = next().text[0];
            auto right = parseUnary();
            left = std::make_unique<BinOp>(op, std::move(left), std::move(right));
            continue;
        }
        if (startsValue(peek().type)) {
            const Expr* base = left.get();
            if (const auto* un = dynamic_cast<const UnaryOp*>(base)) {
                if (un->op() == '-') base = un->operand();
            }
            if (dynamic_cast<const Num*>(base) && peek().type == TokenType::Number) {
                throw CalcError("implicit multiplication between two numbers is not allowed", peek().col);
            }
            auto right = parseUnary();
            left = std::make_unique<BinOp>('*', std::move(left), std::move(right));
            continue;
        }
        break;
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (is(TokenType::Minus)) {
        next();
        auto operand = parseUnary();
        return std::make_unique<UnaryOp>('-', std::move(operand));
    }
    return parsePower();
}

std::unique_ptr<Expr> Parser::parsePower() {
    auto base = parsePostfix();
    if (is(TokenType::Caret)) {
        next();
        auto exp = parseUnary(); // right associative
        return std::make_unique<BinOp>('^', std::move(base), std::move(exp));
    }
    return base;
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto node = parsePrimary();
    while (is(TokenType::Fact) || is(TokenType::Percent)) {
        char kind = next().text[0];
        node = std::make_unique<PostfixOp>(kind, std::move(node));
    }
    return node;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (is(TokenType::Number)) {
        Token t = next();
        return std::make_unique<Num>(t.number);
    }
    if (is(TokenType::Ident)) {
        Token t = next();
        if (is(TokenType::LParen)) {
            next();
            std::vector<std::unique_ptr<Expr>> args;
            if (!is(TokenType::RParen)) {
                while (true) {
                    args.push_back(parseExpr());
                    if (is(TokenType::Comma)) { next(); continue; }
                    break;
                }
            }
            if (!is(TokenType::RParen)) throw CalcError("expected ')'", peek().col);
            next();
            return std::make_unique<Call>(t.text, std::move(args));
        }
        return std::make_unique<Var>(t.text);
    }
    if (is(TokenType::LParen)) {
        next();
        auto node = parseExpr();
        if (!is(TokenType::RParen)) throw CalcError("expected ')'", peek().col);
        next();
        return node;
    }
    throw CalcError("expected expression", peek().col);
}

// ---------- context ----------

Context::Context() : builtins{"pi", "tau", "e", "phi", "ans", "c", "grav", "h", "hbar", "eps0", "mu0", "G", "NA", "kB", "R", "qe", "u", "m_e", "m_p", "sigma"} {
    vars["pi"] = 3.14159265358979323846;
    vars["tau"] = 6.28318530717958647692;
    vars["e"] = 2.71828182845904523536;
    vars["phi"] = 1.61803398874989484820;
    vars["ans"] = 0.0;
    // scientific constants
    vars["c"] = 2.99792458e8;          // speed of light (m/s)
    vars["grav"] = 9.80665;            // standard gravity (m/s^2)
    vars["h"] = 6.62607015e-34;        // Planck constant (J·s)
    vars["hbar"] = 1.054571817e-34;    // reduced Planck constant (J·s)
    vars["e"] = 2.71828182845904523536; // Euler's number — already set above; kept for clarity
    vars["eps0"] = 8.8541878128e-12;   // vacuum permittivity (F/m)
    vars["mu0"] = 1.25663706212e-6;    // vacuum permeability (N/A^2)
    vars["G"] = 6.67430e-11;           // gravitational constant (m^3/kg/s^2)
    vars["NA"] = 6.02214076e23;        // Avogadro constant (mol^-1)
    vars["kB"] = 1.380649e-23;         // Boltzmann constant (J/K)
    vars["R"] = 8.314462618;           // molar gas constant (J/mol/K)
    vars["qe"] = 1.602176634e-19;      // elementary charge (C)
    vars["u"] = 1.66053906660e-27;     // atomic mass unit (kg)
    vars["m_e"] = 9.1093837015e-31;    // electron mass (kg)
    vars["m_p"] = 1.67262192369e-27;   // proton mass (kg)
    vars["sigma"] = 5.670374419e-8;    // Stefan-Boltzmann constant (W/m^2/K^4)
    vars["phi"] = 1.61803398874989484820; // golden ratio — already set above
}

bool Context::isBuiltin(const std::string& name) const {
    return builtins.count(name) != 0;
}

double Context::lookup(const std::string& name) const {
    for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    auto f = vars.find(name);
    if (f != vars.end()) return f->second;
    throw CalcError("unknown variable: " + name, 0);
}

void Context::setVar(const std::string& name, double value) {
    if (!locals.empty()) locals.back()[name] = value;
    else vars[name] = value;
}

void Context::pushFrame() { locals.emplace_back(); }
void Context::popFrame() { locals.pop_back(); }

// ---------- expression evaluation ----------

static double factorial(double v) {
    if (v < 0 || v != std::floor(v) || v > 20)
        throw CalcError("factorial argument must be a non-negative integer <= 20", 0);
    double r = 1;
    for (int i = 2; i <= (int)v; i++) r *= i;
    return r;
}

static const std::map<std::string, int> builtinArgCount = {
    {"sqrt",1},{"sin",1},{"cos",1},{"tan",1},{"asin",1},{"acos",1},{"atan",1},
    {"cot",1},{"csc",1},{"sec",1},
    {"log",1},{"ln",1},{"log2",1},{"exp",1},{"abs",1},{"floor",1},{"ceil",1},{"round",1},
    {"pow",2},{"mod",2},{"hypot",2},{"gcd",2},{"lcm",2},{"cbrt",1},{"asinh",1},{"acosh",1},{"atanh",1},{"isprime",1},{"nextprime",1},{"prevprime",1},
    {"clamp",3},{"sign",1},{"ncr",2},{"npr",2},{"fact",1}
};

// trial-division primality test: shared by isprime/nextprime/prevprime/factor
static bool isprime_helper(long long n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if ((n & 1) == 0) return false;
    for (long long d = 3; d <= n / d; d += 2) if (n % d == 0) return false;
    return true;
}

static double callBuiltin(Context& ctx, const std::string& name, const std::vector<double>& a) {
    int need = builtinArgCount.at(name);
    if ((int)a.size() != need) throw CalcError("function " + name + " expects " + std::to_string(need) + " argument(s)", 0);
    double a0 = a.size() > 0 ? a[0] : 0;
    double a1 = a.size() > 1 ? a[1] : 0;
    double a2 = a.size() > 2 ? a[2] : 0;
    // Angle unit conversion: argument -> radians
    auto toRad = [&ctx](double x) {
        switch (ctx.angle) {
            case Context::Angle::DEG: return x * 3.14159265358979323846 / 180.0;
            case Context::Angle::GRAD: return x * 3.14159265358979323846 / 200.0;
            default: return x;
        }
    };
    // radians -> current angle unit (for inverse trig)
    auto fromRad = [&ctx](double x) {
        switch (ctx.angle) {
            case Context::Angle::DEG: return x * 180.0 / 3.14159265358979323846;
            case Context::Angle::GRAD: return x * 200.0 / 3.14159265358979323846;
            default: return x;
        }
    };
    if (name == "sqrt") { if (a0 < 0) throw CalcError("sqrt of negative number", 0); return std::sqrt(a0); }
    if (name == "sin") return std::sin(toRad(a0));
    if (name == "cos") return std::cos(toRad(a0));
    if (name == "tan") return std::tan(toRad(a0));
    if (name == "asin") { if (a0 < -1 || a0 > 1) throw CalcError("asin out of range", 0); return fromRad(std::asin(a0)); }
    if (name == "acos") { if (a0 < -1 || a0 > 1) throw CalcError("acos out of range", 0); return fromRad(std::acos(a0)); }
    if (name == "atan") return fromRad(std::atan(a0));
    if (name == "log") { if (a0 <= 0) throw CalcError("log of non-positive number", 0); return std::log10(a0); }
    if (name == "ln") { if (a0 <= 0) throw CalcError("ln of non-positive number", 0); return std::log(a0); }
    if (name == "exp") return std::exp(a0);
    if (name == "abs") return std::abs(a0);
    if (name == "floor") return std::floor(a0);
    if (name == "ceil") return std::ceil(a0);
    if (name == "round") return std::round(a0);
    if (name == "pow") return std::pow(a0, a1);
    if (name == "mod") {
        if (a1 == 0.0) throw CalcError("modulo by zero", 0);
        return a0 - a1 * std::floor(a0 / a1);
    }
    if (name == "hypot") return std::hypot(a0, a1);
    if (name == "log2") { if (a0 <= 0) throw CalcError("log2 of non-positive number", 0); return std::log2(a0); }
    if (name == "cot") { double t = std::tan(toRad(a0)); if (t == 0.0) throw CalcError("cot of a zero tangent point", 0); return 1.0 / t; }
    if (name == "csc") { double t = std::sin(toRad(a0)); if (t == 0.0) throw CalcError("csc of a zero sine point", 0); return 1.0 / t; }
    if (name == "sec") { double t = std::cos(toRad(a0)); if (t == 0.0) throw CalcError("sec of a zero cos point", 0); return 1.0 / t; }
    if (name == "gcd") { double x = std::abs(a0), y = std::abs(a1); while (y > 0.5) { double t = std::fmod(x, y); x = y; y = t; } return x; }
    if (name == "lcm") { double g = 1; { double x = std::abs(a0), y = std::abs(a1); while (y > 0.5) { double t = std::fmod(x, y); x = y; y = t; } g = x; } return (g == 0.0) ? 0.0 : std::abs(a0 * a1) / g; }
    // min/max are variadic — handled in Call::eval above
    if (name == "clamp") { return (a0 < a1) ? a1 : (a0 > a2 ? a2 : a0); }
    if (name == "sign") return (a0 > 0) - (a0 < 0) + 0.0;
    if (name == "ncr") { if (a0 < 0 || a1 < 0 || a0 < a1 || a0 != std::floor(a0) || a1 != std::floor(a1)) throw CalcError("ncr: n and r must be non-negative integers, n >= r", 0); double out = 1; for (int i = 0; i < (int)a1; i++) { out *= (a0 - i); out /= (i + 1); } return out; }
    if (name == "npr") { if (a0 < 0 || a1 < 0 || a0 < a1 || a0 != std::floor(a0) || a1 != std::floor(a1)) throw CalcError("npr: n and r must be non-negative integers, n >= r", 0); double out = 1; for (int i = 0; i < (int)a1; i++) out *= (a0 - i); return out; }
    if (name == "fact") { if (a0 < 0 || a0 != std::floor(a0) || a0 > 170) throw CalcError("fact: n must be a non-negative integer <= 170", 0); return factorial(a0); }
    if (name == "cbrt") return std::cbrt(a0);
    if (name == "asinh") return std::asinh(a0);
    if (name == "acosh") { if (a0 < 1) throw CalcError("acosh of value less than 1", 0); return std::acosh(a0); }
    if (name == "atanh") { if (a0 < -1 || a0 >= 1) throw CalcError("atanh out of (-1, 1)", 0); return std::atanh(a0); }
    if (name == "isprime") { long long n = (long long)std::floor(std::abs(a0)); if (n < 2) return 0.0; if (n == 2) return 1.0; if ((n & 1) == 0) return 0.0; for (long long d = 3; d <= n / d; d += 2) { if (n % d == 0) return 0.0; } return 1.0; }
    if (name == "nextprime") { long long n = (long long)std::floor(std::abs(a0)) + 1; if (n < 2) return 2.0; if ((n & 1) == 0) n++; while (!isprime_helper(n)) { n += 2; if (n > 1000000000) throw CalcError("nextprime out of range", 0); } return (double)n; }
    if (name == "prevprime") { long long n = (long long)std::floor(std::abs(a0)) - 1; if (n < 2) { throw CalcError("prevprime: no prime strictly before argument", 0); } if ((n & 1) == 0) n--; while (!isprime_helper(n)) { n -= 2; if (n < 2) throw CalcError("prevprime: no prime strictly before argument", 0); } return (double)n; }
    throw CalcError("unknown function: " + name, 0);
}

double Var::eval(Context& ctx) const { return ctx.lookup(name_); }

double BinOp::eval(Context& ctx) const {
    if (op_ == '=') {
        double rv = r_->eval(ctx);
        return rv; // side effects handled by evalStmt
    }
    double lv = l_->eval(ctx);
    double rv = r_->eval(ctx);
    switch (op_) {
        case '+': {
            if (r_->isPercent()) return lv + lv * r_->percentBase(ctx);
            return lv + rv;
        }
        case '-': {
            if (r_->isPercent()) return lv - lv * r_->percentBase(ctx);
            return lv - rv;
        }
        case '*': return lv * rv;
        case '/': {
            if (rv == 0.0) throw CalcError("division by zero", 0);
            return lv / rv;
        }
        case '^': return std::pow(lv, rv);
        default: throw CalcError("unknown operator", 0);
    }
}

double UnaryOp::eval(Context& ctx) const {
    if (op_ == '-') return -operand_->eval(ctx);
    throw CalcError("unknown unary operator", 0);
}

double PostfixOp::eval(Context& ctx) const {
    double v = operand_->eval(ctx);
    if (kind_ == '!') return factorial(v);
    return v / 100.0;
}

double Call::eval(Context& ctx) const {
    std::vector<double> argVals;
    argVals.reserve(args_.size());
    for (auto& a : args_) argVals.push_back(a->eval(ctx));

    auto bf = builtinArgCount.find(name_);
    if (bf != builtinArgCount.end()) return callBuiltin(ctx, name_, argVals);

    if (name_ == "min" || name_ == "max") {
        if (argVals.empty()) throw CalcError("function " + name_ + " expects at least 1 argument", 0);
        double r = argVals[0];
        for (size_t i = 1; i < argVals.size(); i++)
            r = (name_ == "min") ? std::min(r, argVals[i]) : std::max(r, argVals[i]);
        return r;
    }

    // implicit multiplication: variable followed by parenthesized args, e.g. x(3) = x * 3
    auto vf = ctx.vars.find(name_);
    if (vf != ctx.vars.end()) {
        double r = vf->second;
        for (double a : argVals) r *= a;
        return r;
    }

    // user function: parameter frame, depth cap 64. The guard restores the
    // frame and depth even when the body throws (e.g. sqrt of a negative) —
    // otherwise every failed call permanently leaks a stack frame.
    auto ff = ctx.funcs.find(name_);
    if (ff != ctx.funcs.end()) {
        const UserFunc& uf = ff->second;
        if ((int)argVals.size() != (int)uf.params.size())
            throw CalcError("function " + name_ + " expects " + std::to_string(uf.params.size()) + " argument(s)", 0);
        if (ctx.callDepth >= 64) throw CalcError("recursion too deep", 0);
        ctx.callDepth++;
        ctx.pushFrame();
        struct FrameGuard {
            Context& c;
            ~FrameGuard() { c.popFrame(); c.callDepth--; }
        } guard{ctx};
        for (size_t i = 0; i < uf.params.size(); i++) ctx.setVar(uf.params[i], argVals[i]);
        return uf.body->eval(ctx);
    }

    throw CalcError("unknown function: " + name_, 0);
}

double evalStmt(const std::string& input, Context& ctx) {
    Parser parser(input);
    auto expr = parser.parse();

    const BinOp* b = dynamic_cast<const BinOp*>(expr.get());
    if (b && b->op() == '=') {
        if (auto* v = dynamic_cast<Var*>(b->left())) {
            if (ctx.isBuiltin(v->name()))
                throw CalcError("cannot assign to built-in constant: " + v->name(), 0);
            double val = b->right()->eval(ctx);
            ctx.setVar(v->name(), val);
            ctx.vars["ans"] = val;
            return val;
        }
        if (auto* call = dynamic_cast<Call*>(b->left())) {
            std::vector<std::string> params;
            for (auto& a : call->args()) {
                auto* var = dynamic_cast<Var*>(a.get());
                if (!var) throw CalcError("function parameters must be identifiers", 0);
                params.push_back(var->name());
            }
            ctx.funcs[call->name()] = {std::move(params), b->right()->clone()};
            return 0.0;
        }
        throw CalcError("invalid assignment target", 0);
    }

    double result = expr->eval(ctx);
    ctx.vars["ans"] = result;
    return result;
}

double evalLine(const std::string& input, Context& ctx) {
    double last = 0.0;
    size_t start = 0;
    while (true) {
        size_t sc = input.find(';', start);
        std::string stmt = (sc == std::string::npos) ? input.substr(start) : input.substr(start, sc - start);
        size_t a = stmt.find_first_not_of(" \t\r\n");
        size_t b = stmt.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) {
            stmt = stmt.substr(a, b - a + 1);
            last = evalStmt(stmt, ctx);
        }
        if (sc == std::string::npos) break;
        start = sc + 1;
    }
    return last;
}

// ---------- formatting ----------

// %g switches to exponent form once it thinks a number is "extreme". The
// display law wants bare decimals wherever a plain reading is sane, and the
// --eval contract wants machine-readable bare numbers, so expand the mantissa
// back into positional notation. Returns the input unchanged when expansion
// would be longer than the exponent form (or absurdly wide).
static std::string expandExponent(std::string s) {
    auto epos = s.find('e');
    if (epos == std::string::npos) epos = s.find('E');
    if (epos == std::string::npos) return s;

    int exp = 0;
    try { exp = std::stoi(s.substr(epos + 1)); } catch (...) { return s; }
    std::string mant = s.substr(0, epos);
    bool neg = !mant.empty() && mant[0] == '-';
    if (neg) mant.erase(0, 1);

    auto dot = mant.find('.');
    std::string digits = (dot == std::string::npos) ? mant : mant.substr(0, dot) + mant.substr(dot + 1);
    int point = (dot == std::string::npos ? static_cast<int>(mant.size()) : static_cast<int>(dot)) + exp;

    std::string out;
    if (point <= 0) {
        out = "0." + std::string(static_cast<size_t>(-point), '0') + digits;
    } else if (point >= static_cast<int>(digits.size())) {
        out = digits + std::string(static_cast<size_t>(point) - digits.size(), '0');
    } else {
        out = digits.substr(0, static_cast<size_t>(point)) + "." + digits.substr(static_cast<size_t>(point));
    }
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty() || out == "0") return "0";
    // Keep the exponent form when the expansion would be unwieldy.
    if (out.size() + (neg ? 1 : 0) > 24) return s;
    return (neg ? "-" : "") + out;
}

std::string formatNumber(double v) {
    if (std::isnan(v) || std::isinf(v)) return std::isnan(v) ? "nan" : (v < 0 ? "-inf" : "inf");
    if (v == 0) return "0"; // never "-0"
    // Integers print in full up to the exact long long range; %.12g silently
    // rounds anything past 12 significant digits into exponent form.
    if (v == std::floor(v) && std::abs(v) < 9.2e18) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.0f", v);
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos &&
        s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
        size_t end = s.find_last_not_of('0');
        if (end != std::string::npos && s[end] == '.') end--;
        if (end != std::string::npos) s = s.substr(0, end + 1);
    } else {
        s = expandExponent(s);
    }
    return s;
}
