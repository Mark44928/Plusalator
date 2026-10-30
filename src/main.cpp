#include "calc.h"
#include <iostream>
#include <string>

static void printCaretError(const std::string& input, const CalcError& err) {
    std::cout << input << "\n";
    size_t col = err.col;
    if (col < 1) col = 1;
    if (col > input.size() + 1) col = input.size() + 1;
    std::cout << std::string(col - 1, ' ') << "^\n";
    std::cout << err.msg << "\n";
}

int main(int argc, char** argv) {
    std::string evalExpr;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--eval" && i + 1 < argc) { evalExpr = argv[++i]; }
        else if (a == "--help" || a == "-h") {
            std::cout << "Plusalator - a fancy interactive calculator REPL\n"
                      << "Usage: plusalator [--eval <expr>]\n";
            return 0;
        } else if (a == "--version") {
            std::cout << "plusalator 1.0.0\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 1;
        }
    }

    Context ctx;

    if (!evalExpr.empty()) {
        try {
            double result = evalLine(evalExpr, ctx);
            std::cout << formatNumber(result) << "\n";
        } catch (const CalcError& err) {
            printCaretError(evalExpr, err);
        } catch (const std::exception& err) {
            printCaretError(evalExpr, CalcError(err.what()));
        }
        return 0;
    }

    // REPL will be implemented in later milestones.
    std::cout << "plusalator REPL (use --eval <expr> for now)\n";
    return 0;
}
