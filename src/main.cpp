#include "calc.h"
#include "lineedit.h"
#include "banner.h"
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

static void printCaretError(const std::string& input, const CalcError& err) {
    std::cout << input << "\n";
    size_t col = err.col;
    if (col < 1) col = 1;
    if (col > input.size() + 1) col = input.size() + 1;
    std::cout << std::string(col - 1, ' ') << "^\n";
    std::cout << err.msg << "\n";
}

static bool colorEnabled() {
    if (getenv("NO_COLOR")) return false;
    const char* term = getenv("TERM");
    if (!term || std::string(term) == "dumb") return false;
    if (!isatty(STDOUT_FILENO)) return false;
    return true;
}

static std::string rgb(int r, int g, int b) {
    char buf[32];
    snprintf(buf, sizeof buf, "\033[38;2;%d;%d;%dm", r, g, b);
    return buf;
}

struct Theme {
    std::string name;
    int pr, pg, pb; // prompt
    int rr, rg, rb; // result
    int er, eg, eb; // error
    int hr, hg, hb; // hint
    int vr, vg, vb; // rule
    int ar, ag, ab; // accent
};

static const std::vector<Theme> ALL_THEMES = {
    {"greenit", 0, 208, 64, 208, 208, 208, 255, 68, 68, 138, 138, 138, 74, 74, 74, 0, 208, 64},
    {"mono", 208, 208, 208, 208, 208, 208, 208, 208, 208, 138, 138, 138, 74, 74, 74, 208, 208, 208},
    {"ocean", 0, 136, 255, 208, 208, 208, 255, 68, 68, 138, 138, 138, 74, 74, 74, 0, 136, 255},
    {"ember", 255, 102, 0, 208, 208, 208, 255, 68, 68, 138, 138, 138, 74, 74, 74, 255, 102, 0},
    {"sakura", 255, 153, 204, 208, 208, 208, 255, 68, 68, 138, 138, 138, 74, 74, 74, 255, 153, 204},
};

static const Theme* findTheme(const std::string& name) {
    for (auto& t : ALL_THEMES) if (t.name == name) return &t;
    return nullptr;
}

static std::string esc(int r, int g, int b) {
    return colorEnabled() ? rgb(r, g, b) : "";
}
static std::string escReset() { return colorEnabled() ? "\033[0m" : ""; }
static std::string escBold() { return colorEnabled() ? "\033[1m" : ""; }
static std::string escDim() { return colorEnabled() ? "\033[2m" : ""; }
static std::string escItalic() { return colorEnabled() ? "\033[3m" : ""; }
static std::string rep(int n, const char* s) { std::string r; for (int i = 0; i < n; i++) r += s; return r; }

// Display width in columns: each UTF-8 sequence counts as one column. Adequate for
// the BMP glyphs used here (─ ⟨ ⟩ ➤ ·) and ASCII.
static size_t dispWidth(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i += 1; }
        else if ((c & 0xE0) == 0xC0) { i += 2; }
        else if ((c & 0xF0) == 0xE0) { i += 3; }
        else if ((c & 0xF8) == 0xF0) { i += 4; }
        else { i += 1; }
        n += 1;
    }
    return n;
}

static int termWidth() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) return (int)w.ws_col;
    return 80;
}

struct Styled {
    std::string text;
    const Theme* theme;
    int r, g, b;
    bool bold = false;
    bool dim = false;
};

static void put(std::ostream& os, const Styled& s) {
    if (!colorEnabled()) { os << s.text; return; }
    if (s.bold) os << "\033[1m";
    if (s.dim) os << "\033[2m";
    os << "\033[38;2;" << s.r << ";" << s.g << ";" << s.b << "m";
    os << s.text;
    os << "\033[0m";
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

    const Theme* cur = &ALL_THEMES[0];
    Context ctx;
    LineEditor edit;

    bool errorFlash = false;

    auto promptText = [&cur, &errorFlash]() -> std::string {
        std::string out;
        // ⟨  accent brackets
        out += escBold() + esc(cur->ar, cur->ag, cur->ab) + "⟨" + escReset();
        out += " ";
        // plusa  hint color, italic
        out += escItalic() + esc(cur->hr, cur->hg, cur->hb) + "plusa" + escReset();
        out += " ";
        // ⟩  accent bracket
        out += esc(cur->ar, cur->ag, cur->ab) + "⟩" + escReset();
        out += " ";
        // ➤  accent, bold — flashes rose once on error
        int ar = cur->ar, ag = cur->ag, ab = cur->ab;
        if (errorFlash) { ar = 255; ag = 90; ab = 120; errorFlash = false; }
        out += escBold() + esc(ar, ag, ab) + "➤" + escReset() + " ";
        return out;
    };

    auto transientPromptText = [&cur]() -> std::string {
        std::string out;
        out += "\033[2m";
        out += esc(cur->ar, cur->ag, cur->ab) + "⟨" + escReset();
        out += " ";
        out += esc(cur->hr, cur->hg, cur->hb) + "plusa" + escReset();
        out += " ";
        out += esc(cur->ar, cur->ag, cur->ab) + "⟩" + escReset();
        out += " ";
        out += "\033[1m" + esc(cur->ar, cur->ag, cur->ab) + "➤" + escReset();
        out += "\033[0m ";
        return out;
    };

    auto printLine1 = [&cur, &ctx]() {
        int w = termWidth();
        std::string status = cur->name + " · " + std::to_string((int)ctx.vars.size()) + " vars";
        size_t slen = dispWidth(status);
        int ruleCells = w - (int)slen - 2;
        if (ruleCells < 0) ruleCells = 0;
        if ((int)slen > w - 2) slen = w - 2; // keep status on one line
        status.resize(slen);
        for (int i = 0; i < ruleCells; i++) {
            double f = (ruleCells > 1) ? (1.0 - std::abs(2.0 * i / (double)(ruleCells - 1) - 1.0)) : 1.0;
            int r = (int)(cur->hr * (1.0 - f) + cur->ar * f);
            int g = (int)(cur->hg * (1.0 - f) + cur->ag * f);
            int b = (int)(cur->hb * (1.0 - f) + cur->ab * f);
            std::cout << esc(r, g, b) << "─";
        }
        std::cout << escReset() << "  " << escDim() << status << escReset() << "\n";
    };

    auto printBanner = [&cur]() {
        std::string text = bannerText();
        // split into rows, trim trailing empty rows
        std::vector<std::string> rows;
        size_t start = 0;
        while (true) {
            size_t nl = text.find('\n', start);
            std::string row = (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
            rows.push_back(row);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        while (!rows.empty() && rows.back().empty()) rows.pop_back();
        int n = (int)rows.size();
        for (int i = 0; i < n; i++) {
            if (rows[i].empty()) { std::cout << "\n"; continue; }
            double f = (n > 1) ? (double)i / (double)(n - 1) : 0.0;
            int r = (int)(cur->ar * (1 - f) + cur->pr * f);
            int g = (int)(cur->ag * (1 - f) + cur->pg * f);
            int b = (int)(cur->ab * (1 - f) + cur->pb * f);
            std::cout << esc( r, g, b) << rows[i] << escReset() << "\n";
        }
    };

    printBanner();
    std::cout << escDim() << "Tip: type " << escBold() << "help" << escReset()
              << escDim() << " for commands, or enter an expression to calculate." << escReset() << "\n";

    while (true) {
        printLine1();
        edit.setTransientPrompt(transientPromptText());
        std::string line;
        if (!edit.edit(line, promptText())) break; // Ctrl+D on empty -> quit
        if (line.empty()) { continue; }
        if (line == "quit" || line == "exit" || line == "q") break;

        std::cout << "\r\033[K" << escDim() << "⟨ ·· ⟩ ➤ " << line << escReset() << "\n";

        // tokenize first word for command dispatch
        size_t a0 = line.find_first_not_of(" \t");
        size_t b0 = line.find_last_not_of(" \t");
        std::string trimmed = (a0 == std::string::npos) ? "" : line.substr(a0, b0 - a0 + 1);
        size_t sp = trimmed.find(' ');
        std::string cmd = (sp == std::string::npos) ? trimmed : trimmed.substr(0, sp);
        std::string arg = (sp == std::string::npos) ? "" : trimmed.substr(sp + 1);
        // trim arg
        size_t a1 = arg.find_first_not_of(" \t");
        if (a1 != std::string::npos) arg = arg.substr(a1);

        if (cmd == "help") {
            const int W = 64, LC = 22, RC = 36;
            std::cout << esc( cur->vr, cur->vg, cur->vb) << "┌" << rep(W - 2, "─") << "┐" << escReset() << "\n";
            std::cout << esc( cur->vr, cur->vg, cur->vb) << "│" << escDim()
                      << std::string(2, ' ') << "Plusalator" << escReset()
                      << std::string(W - 2 - 2 - 10, ' ')
                      << esc( cur->vr, cur->vg, cur->vb) << "│" << escReset() << "\n";
            std::cout << esc( cur->vr, cur->vg, cur->vb) << "├" << rep(LC + 2, "─") << "┬" << rep(RC + 1, "─") << "┤" << escReset() << "\n";
            struct HelpRow { std::string c, d; };
            HelpRow rows[] = {
                {"<expr>", "evaluate an expression"},
                {"help", "show this help box"},
                {"vars", "list variables and values"},
                {"funcs", "list user functions with signatures"},
                {"history", "show command history"},
                {"clear", "clear screen and re-banner"},
                {"theme", "list available themes"},
                {"theme <name>", "switch theme (session-only)"},
                {"quit / exit / q", "exit the REPL"},
            };
            for (auto& row : rows) {
                std::cout << esc( cur->vr, cur->vg, cur->vb) << "│ " << escBold()
                          << esc( cur->pr, cur->pg, cur->pb) << row.c;
                std::cout << std::string(LC - row.c.size(), ' ') << escReset();
                std::cout << esc( cur->vr, cur->vg, cur->vb) << " │ " << row.d;
                std::cout << std::string(RC - row.d.size(), ' ')
                          << esc( cur->vr, cur->vg, cur->vb) << "│" << escReset() << "\n";
            }
            std::cout << esc( cur->vr, cur->vg, cur->vb) << "└" << rep(W - 2, "─") << "┘" << escReset() << "\n";
        } else if (cmd == "vars") {
            for (auto& kv : ctx.vars) {
                std::cout << esc( cur->pr, cur->pg, cur->pb) << kv.first << escReset()
                          << " = " << esc( cur->rr, cur->rg, cur->rb) << formatNumber(kv.second) << escReset() << "\n";
            }
        } else if (cmd == "funcs") {
            if (ctx.funcs.empty()) {
                std::cout << escDim() << "(no user functions)" << escReset() << "\n";
            }
            for (auto& kv : ctx.funcs) {
                std::cout << esc( cur->pr, cur->pg, cur->pb) << kv.first << escReset() << "(";
                for (size_t j = 0; j < kv.second.params.size(); j++) {
                    if (j) std::cout << ", ";
                    std::cout << escDim() << kv.second.params[j] << escReset();
                }
                std::cout << ")\n";
            }
        } else if (cmd == "history") {
            const auto& h = edit.history();
            if (h.empty()) {
                std::cout << escDim() << "(no history)" << escReset() << "\n";
            }
            for (size_t i = 0; i < h.size(); i++) {
                std::cout << escDim() << (i + 1) << escReset() << "  " << h[i] << "\n";
            }
        } else if (cmd == "clear") {
            std::cout << "\033[2J\033[3J\033[H";
            printBanner();
        } else if (cmd == "theme") {
            if (arg.empty()) {
                for (auto& t : ALL_THEMES) {
                    bool isCur = (&t == cur);
                    std::cout << (isCur ? "* " : "  ");
                    put(std::cout, {t.name, &t, t.pr, t.pg, t.pb, true});
                    std::cout << "  " << esc( t.ar, t.ag, t.ab) << "████" << escReset() << " " << t.name << "\n";
                }
                std::cout << escDim() << "current: " << cur->name << " (use: theme <name>)" << escReset() << "\n";
            } else {
                const Theme* nt = findTheme(arg);
                if (nt) { cur = nt; }
                else std::cout << esc( cur->er, cur->eg, cur->eb) << "unknown theme: " << arg << escReset() << "\n";
            }
        } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;
        } else {
            try {
                double result = evalLine(line, ctx);
                std::cout << esc( cur->rr, cur->rg, cur->rb) << "= " << formatNumber(result) << escReset() << "\n";
            } catch (const CalcError& err) {
                errorFlash = true;
                std::cout << line << "\n";
                size_t col = err.col;
                if (col < 1) col = 1;
                if (col > line.size() + 1) col = line.size() + 1;
                std::cout << std::string(col - 1, ' ') << "^\n";
                std::cout << esc( cur->er, cur->eg, cur->eb) << err.msg << escReset() << "\n";
            } catch (const std::exception& err) {
                std::cout << esc( cur->er, cur->eg, cur->eb) << err.what() << escReset() << "\n";
            }
        }
    }
    std::cout << "\n";
    return 0;
}
