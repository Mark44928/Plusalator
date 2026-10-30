#include "calc.h"
#include "lineedit.h"
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
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::cout << "Plusalator - a fancy interactive calculator REPL\n"
                      << "Usage: plusalator [--eval <expr>]\n";
            return 0;
        } else if (a == "--version") {
            std::cout << "plusalator 1.0.0\n";
            return 0;
        } else if (a == "--eval" && i + 1 < argc) {
        Context ctx;
        try {
            double result = evalLine(argv[++i], ctx);
            std::cout << formatNumber(result) << "\n";
        } catch (const CalcError& err) {
            printCaretError(argv[i], err);
        } catch (const std::exception& err) {
            printCaretError(argv[i], CalcError(err.what()));
        }
        return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 1;
        }
    }

    Context ctx;
    LineEditor edit;
    const std::string prompt = "plusa\u276f "; // plusa❯

    while (true) {
        std::string line;
        if (!edit.edit(line, prompt)) break; // Ctrl+D on empty -> quit
        if (line.empty()) continue;
        if (line == "quit" || line == "exit" || line == "q") break;

        try {
            double result = evalLine(line, ctx);
            std::cout << "\033[2m= " << formatNumber(result) << "\033[0m\n";
        } catch (const CalcError& err) {
            std::cout << line << "\n";
            size_t col = err.col;
            if (col < 1) col = 1;
            if (col > line.size() + 1) col = line.size() + 1;
            std::cout << std::string(col - 1, ' ') << "^\n";
            std::cout << "\033[31m" << err.msg << "\033[0m\n";
        } catch (const std::exception& err) {
            std::cout << "\033[31m" << err.what() << "\033[0m\n";
        }
    }
    std::cout << "\n";
    return 0;
}
