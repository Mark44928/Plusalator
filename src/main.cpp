#include "calc.h"
#include "lineedit.h"
#include "banner.h"
#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cerrno>
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

static std::string configPath() {
    const char* home = getenv("HOME");
    std::string base = home ? home : std::string(".");
    return base + "/.config/plusalator/config";
}

// Return persisted theme name from config file, or "" if missing/invalid.
static std::string loadConfigTheme() {
    std::ifstream f(configPath());
    if (!f) return "";
    std::string line;
    while (std::getline(f, line)) {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        if (line[a] == '#' || line[a] == '/') continue; // comment
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        size_t ks = key.find_first_not_of(" \t");
        size_t ke = key.find_last_not_of(" \t");
        if (ks == std::string::npos) continue;
        key = key.substr(ks, ke - ks + 1);
        if (key != "theme") continue;
        std::string val = line.substr(eq + 1);
        size_t vs = val.find_first_not_of(" \t");
        size_t ve = val.find_last_not_of(" \t\r\n");
        if (vs == std::string::npos) return "";
        return val.substr(vs, ve - vs + 1);
    }
    return "";
}

// Persist theme name to config file (creates dir as needed).
static bool saveConfigTheme(const std::string& name) {
    std::string path = configPath();
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "theme=" << name << "\n";
    return f.good();
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

    // Load persisted theme from config file (falls back to greenit default).
    {
        std::string saved = loadConfigTheme();
        if (!saved.empty()) {
            const Theme* nt = findTheme(saved);
            if (nt) cur = nt;
        }
    }

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
        // Clear the current line (in case the cursor is mid-prompt from a prior
        // edit), then draw the gradient rule + right-side dim status on its own
        // line, and advance so the prompt lands on the next line.
        std::cout << "\r\033[2K";
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
                {"settings", "interactive settings menu (switch theme)"},
                {"ans", "show last answer"},
                {"constants", "list built-in constants"},
                {"reset", "clear user vars & functions"},
                {"del <name>", "delete a variable or function"},
                {"config", "show config path and active theme"},
                {"version / about", "show version and build info"},
                {"quit / exit / q", "exit the REPL"},
            };
            for (auto& row : rows) {
                std::cout << esc( cur->vr, cur->vg, cur->vb) << "│ " << escBold()
                          << esc( cur->pr, cur->pg, cur->pb) << row.c;
                size_t lcPad = row.c.size() < (size_t)LC ? (size_t)LC - row.c.size() : 0;
                std::cout << std::string(lcPad, ' ') << escReset();
                std::string disp = row.d.size() > (size_t)RC ? row.d.substr(0, RC - 1) + "~" : row.d;
                std::cout << esc( cur->vr, cur->vg, cur->vb) << " │ " << disp;
                size_t rcPad = disp.size() < (size_t)RC ? (size_t)RC - disp.size() : 0;
                std::cout << std::string(rcPad, ' ')
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
        } else if (cmd == "ans") {
            std::cout << esc( cur->pr, cur->pg, cur->pb) << "ans" << escReset()
                      << " = " << esc( cur->rr, cur->rg, cur->rb)
                      << formatNumber(ctx.vars.at("ans")) << escReset() << "\n";
        } else if (cmd == "constants") {
            static const std::vector<std::pair<std::string, double>> consts = {
                {"pi", 3.14159265358979323846},
                {"tau", 6.28318530717958647692},
                {"e", 2.71828182845904523536},
                {"phi", 1.61803398874989484820},
            };
            for (auto& c : consts) {
                std::cout << esc( cur->pr, cur->pg, cur->pb) << c.first << escReset()
                          << " = " << esc( cur->rr, cur->rg, cur->rb)
                          << formatNumber(c.second) << escReset() << "\n";
            }
        } else if (cmd == "reset") {
            static const std::vector<std::string> keep = {"pi", "tau", "e", "phi", "ans"};
            for (auto it = ctx.vars.begin(); it != ctx.vars.end(); ) {
                bool protected_ = false;
                for (auto& k : keep) if (it->first == k) { protected_ = true; break; }
                if (protected_) { it->second = (it->first == "ans") ? 0.0 : it->second; ++it; }
                else it = ctx.vars.erase(it);
            }
            ctx.funcs.clear();
            std::cout << escDim() << "state reset" << escReset() << "\n";
        } else if (cmd == "del") {
            if (arg.empty()) {
                std::cout << esc( cur->er, cur->eg, cur->eb) << "usage: del <name>" << escReset() << "\n";
            } else {
                static const std::vector<std::string> protect = {"pi", "tau", "e", "phi", "ans"};
                bool protected_ = false;
                for (auto& k : protect) if (arg == k) { protected_ = true; break; }
                if (protected_) {
                    std::cout << esc( cur->er, cur->eg, cur->eb) << "cannot delete built-in: " << arg << escReset() << "\n";
                } else if (ctx.vars.erase(arg)) {
                    std::cout << escDim() << "deleted variable: " << arg << escReset() << "\n";
                } else if (ctx.funcs.erase(arg)) {
                    std::cout << escDim() << "deleted function: " << arg << escReset() << "\n";
                } else {
                    std::cout << esc( cur->er, cur->eg, cur->eb) << "no such variable or function: " << arg << escReset() << "\n";
                }
            }
        } else if (cmd == "config") {
            std::cout << escDim() << "config: " << configPath() << escReset() << "\n";
            std::cout << esc( cur->pr, cur->pg, cur->pb) << "theme" << escReset()
                      << " = " << esc( cur->rr, cur->rg, cur->rb) << cur->name << escReset() << "\n";
        } else if (cmd == "version" || cmd == "about") {
            std::cout << escBold() << esc( cur->pr, cur->pg, cur->pb) << "plusalator 1.0.0" << escReset() << "\n";
            std::cout << escDim() << "Fancy interactive C++17 calculator REPL — stdlib only, Qalculate-flavored engine." << escReset() << "\n";
            std::cout << escDim() << "Theme: " << cur->name << " · config: " << configPath() << escReset() << "\n";
        } else if (cmd == "clear") {
            std::cout << "\033[2J\033[3J\033[H";
            printBanner();
        } else if (cmd == "theme") {
            for (auto& t : ALL_THEMES) {
                bool isCur = (&t == cur);
                std::cout << (isCur ? "* " : "  ");
                put(std::cout, {t.name, &t, t.pr, t.pg, t.pb, true});
                std::cout << "  " << esc( t.ar, t.ag, t.ab) << "████" << escReset() << " " << t.name << "\n";
            }
            std::cout << escDim() << "current: " << cur->name << " — use 'settings' to switch theme" << escReset() << "\n";
        } else if (cmd == "settings") {
            while (true) {
                printLine1();
                std::cout << escBold() << esc(cur->pr, cur->pg, cur->pb) << "⚙ settings" << escReset() << "\n";
                std::cout << escDim() << "  1 · theme    current: " << cur->name << escReset() << "\n";
                std::cout << escDim() << "  2 · config   " << configPath() << escReset() << "\n";
                std::cout << escDim() << "  3 · back" << escReset() << "\n";
                std::string choice;
                if (!edit.edit(choice, promptText())) break;
                size_t c0 = choice.find_first_not_of(" \t");
                size_t c1 = choice.find_last_not_of(" \t");
                if (c0 == std::string::npos) continue;
                choice = choice.substr(c0, c1 - c0 + 1);
                if (choice == "back" || choice == "3" || choice == "q" || choice == "quit") break;
                if (choice == "theme" || choice == "1") {
                    while (true) {
                        printLine1();
                        std::cout << escBold() << esc(cur->pr, cur->pg, cur->pb) << "⚙ settings › theme" << escReset() << "\n";
                        int idx = 0;
                        for (auto& t : ALL_THEMES) {
                            std::cout << escDim() << "  " << (++idx) << escReset() << " ";
                            put(std::cout, {t.name, &t, t.pr, t.pg, t.pb, true});
                            std::cout << escReset() << "  " << esc(t.ar, t.ag, t.ab) << "████" << escReset() << " " << t.name;
                            if (&t == cur) std::cout << escDim() << "  ← current" << escReset();
                            std::cout << "\n";
                        }
                        std::cout << escDim() << "  0 · back" << escReset() << "\n";
                        std::string pick;
                        if (!edit.edit(pick, promptText())) break;
                        size_t p0 = pick.find_first_not_of(" \t");
                        size_t p1 = pick.find_last_not_of(" \t");
                        if (p0 == std::string::npos) continue;
                        pick = pick.substr(p0, p1 - p0 + 1);
                        if (pick == "back" || pick == "0" || pick == "q") break;
                        const Theme* nt = nullptr;
                        try { int n = std::stoi(pick); if (n >= 1 && n <= (int)ALL_THEMES.size()) nt = &ALL_THEMES[n - 1]; } catch (...) {}
                        if (!nt) nt = findTheme(pick);
                        if (nt) { cur = nt; saveConfigTheme(nt->name); break; }
                        std::cout << "\n" << esc(cur->er, cur->eg, cur->eb) << "unknown theme: " << pick << escReset() << "\n";
                    }
                } else if (choice == "config" || choice == "2") {
                    printLine1();
                    std::cout << escDim() << "config: " << configPath() << escReset() << "\n";
                    std::cout << esc(cur->pr, cur->pg, cur->pb) << "theme" << escReset()
                              << " = " << esc(cur->rr, cur->rg, cur->rb) << cur->name << escReset() << "\n";
                    std::string wait;
                    if (!edit.edit(wait, promptText())) break;
                } else {
                    std::cout << "\n" << esc(cur->er, cur->eg, cur->eb) << "unknown setting: " << choice << escReset() << "\n";
                }
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
