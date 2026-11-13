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
#include <csignal>
#include <set>
#include <algorithm>
#include <cmath>

// Ctrl+C never exits the REPL (law). Inside the editor, ISIG is off so 0x03
// arrives as a keystroke; anywhere else the kernel still raises SIGINT, so a
// handler must resume the loop instead of terminating.
static volatile sig_atomic_t g_sigint_seen = 0;
static void onSigint(int) { g_sigint_seen = 1; }

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
    if (getenv("FORCE_COLOR")) return true; // --eval opt-in: color without a TTY
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
    // Original themes
    {"greenit", 0, 208, 64, 208, 208, 208, 255, 68, 68, 138, 138, 138, 74, 74, 74, 0, 208, 64},
    {"mono", 220, 220, 220, 240, 240, 240, 255, 80, 80, 130, 130, 130, 80, 80, 80, 220, 220, 220},
    {"ocean", 0, 180, 255, 240, 240, 240, 255, 100, 100, 150, 150, 150, 90, 90, 90, 0, 180, 255},
    {"ember", 255, 120, 40, 240, 240, 240, 255, 100, 80, 150, 150, 150, 90, 80, 70, 255, 120, 40},
    {"sakura", 255, 180, 220, 240, 240, 240, 255, 100, 140, 160, 140, 160, 100, 90, 100, 255, 180, 220},
    {"midnight", 150, 180, 255, 240, 240, 240, 255, 120, 150, 120, 120, 120, 60, 60, 80, 150, 180, 255},
    
    // Modern professional themes
    // Nord - Arctic Ice Studio (frost palette, low saturation, high readability)
    {"nord", 143, 188, 187, 236, 239, 244, 191, 97, 106, 216, 222, 233, 59, 66, 82, 136, 192, 208},
    
    // Catppuccin Mocha - Pastel colors, cozy aesthetic
    {"catppuccin", 245, 224, 220, 205, 214, 244, 243, 139, 168, 180, 190, 254, 205, 214, 244, 137, 180, 250},
    
    // Tokyo Night - Dark theme with vibrant accents
    {"tokyonight", 192, 202, 245, 122, 162, 247, 247, 118, 142, 187, 154, 247, 26, 27, 38, 125, 207, 255},
    
    // Dracula - Popular dark theme
    {"dracula", 248, 248, 242, 248, 248, 242, 255, 85, 85, 98, 114, 164, 40, 42, 54, 189, 147, 249},
    
    // Gruvbox Dark - Retro groove with warm colors
    {"gruvbox", 235, 219, 178, 235, 219, 178, 251, 73, 52, 40, 40, 40, 131, 165, 152, 211, 134, 155},
    
    // Solarized Dark - Scientific, colorblind-friendly
    {"solarized", 131, 148, 150, 131, 148, 150, 220, 50, 47, 101, 123, 131, 0, 43, 54, 38, 139, 210},
    
    // Cyberpunk - Neon aesthetic
    {"cyberpunk", 0, 255, 255, 220, 220, 220, 255, 0, 150, 150, 50, 200, 80, 80, 200, 0, 255, 255},
    
    // Matrix - Green on black, classic hacker look
    {"matrix", 0, 255, 43, 0, 255, 0, 0, 255, 0, 0, 100, 0, 0, 50, 50, 0, 255, 43},
    
    // Sunset - Warm orange/pink gradient
    {"sunset", 255, 154, 77, 255, 255, 255, 255, 100, 50, 255, 180, 100, 120, 80, 60, 255, 119, 197},
    
    // Aurora - Purple/blue/pink palette
    {"aurora", 180, 140, 255, 240, 240, 240, 255, 100, 200, 160, 140, 200, 90, 80, 120, 140, 100, 255},
    
    // Neon - Bright glowing colors
    {"neon", 0, 255, 255, 255, 255, 255, 255, 0, 150, 0, 255, 255, 100, 0, 255, 0, 255, 255},
    
    // Retro - CRT monitor aesthetic (amber on dark)
    {"retro", 255, 191, 0, 220, 220, 220, 255, 150, 0, 100, 80, 50, 50, 50, 50, 255, 215, 0},
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

// Persist theme name to config file (legacy single-key shim; the settings
// menu uses saveSettings below which also stores angle + radix).
static bool saveSettings(const std::string& themeName, const std::string& angle, const std::string& radixName);

static std::string esc(int r, int g, int b) {
    return colorEnabled() ? rgb(r, g, b) : "";
}
static std::string escReset() { return colorEnabled() ? "\033[0m" : ""; }
static std::string escBold() { return colorEnabled() ? "\033[1m" : ""; }
static std::string escDim() { return colorEnabled() ? "\033[2m" : ""; }
static std::string escItalic() { return colorEnabled() ? "\033[3m" : ""; }
static std::string rep(int n, const char* s) { std::string r; for (int i = 0; i < n; i++) r += s; return r; }

// Output radix for the result line: 10 (auto), 16, 2, or 0 = scientific.
static int g_radix = 10;
static bool g_has_memory = false;
static double g_memory = 0.0;

// Persist all session settings (theme, angle, radix) so a restart continues
// Persist user-defined variables to ~/.config/plusalator/vars. KEY=VALUE per
// line. Loaded on startup; saved on exit. Builtins (pi, ans, …) are skipped
// so they never shadow fresh constants.
static std::string varsPath() {
    const char* home = getenv("HOME");
    std::string base = home ? home : std::string(".");
    return base + "/.config/plusalator/vars";
}

static void saveUserVars(const Context& ctx) {
    const char* home = getenv("HOME");
    if (!home || !*home) return;
    // mkdir -p semantics: ensure each parent exists before creating the leaf
    std::string dir = std::string(home) + "/.config";
    mkdir(dir.c_str(), 0700);
    dir += "/plusalator";
    mkdir(dir.c_str(), 0700);
    std::ofstream f(varsPath(), std::ios::trunc);
    if (!f) return;
    if (g_has_memory) f << "MEMORY=" << formatNumber(g_memory) << "\n";
    for (auto& kv : ctx.vars) {
        if (ctx.isBuiltin(kv.first)) continue;
        f << kv.first << "=" << formatNumber(kv.second) << "\n";
    }
}

static void loadUserVars(Context& ctx) {
    std::ifstream f(varsPath());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        size_t ks = key.find_first_not_of(" \t\r\n");
        size_t ke = key.find_last_not_of(" \t\r\n");
        if (ks == std::string::npos || ke == std::string::npos) continue;
        key = key.substr(ks, ke - ks + 1);
        if (key.empty()) continue;
        if (key == "MEMORY" || key == "memory") {
            try { g_memory = std::stod(val); g_has_memory = true; } catch (...) {}
            continue;
        }
        if (ctx.isBuiltin(key)) continue;
        try { ctx.vars[key] = std::stod(val); } catch (...) {}
    }
}

// where you left off. Unknown keys are ignored on load; the file stays the
// same simple KEY=VALUE format.
static bool saveSettings(const std::string& themeName, const std::string& angle, const std::string& radixName) {
    std::string path = configPath();
    // ensure every component (.config, plusalator dir) exists — same mkdir -p semantics
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] == '/') {
            mkdir(path.substr(0, i).c_str(), 0700);
        }
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        mkdir(dir.c_str(), 0700);
    }
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "theme=" << themeName << "\n";
    f << "angle=" << angle << "\n";
    f << "mode=" << radixName << "\n";
    return f.good();
}

// Return one persisted setting (theme/angle/mode), or "" when unset.
static std::string loadSetting(const std::string& want) {
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
        if (key != want) continue;
        std::string val = line.substr(eq + 1);
        size_t vs = val.find_first_not_of(" \t");
        size_t ve = val.find_last_not_of(" \t\r\n");
        if (vs == std::string::npos) return "";
        return val.substr(vs, ve - vs + 1);
    }
    return "";
}

// Format a result honoring the active radix. Hex/bin only apply to integral
// values; non-integers always print in decimal so no precision is lost.
static std::string formatValue(double v) {
    std::string base = formatNumber(v);
    bool integral = (v == std::floor(v)) && std::abs(v) < 9.2e18;
    if (!integral) return base;
    if (g_radix == 16) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%llx", (long long)v);
        return std::string("0x") + buf;
    }
    if (g_radix == 2) {
        if (v == 0) return "0b0";
        unsigned long long u = (unsigned long long)(v < 0 ? -v : v);
        std::string bits;
        while (u) { bits += (u & 1) ? "1" : "0"; u >>= 1; }
        std::string rev(bits.rbegin(), bits.rend());
        return std::string("0b") + (v < 0 ? "-" : "") + rev;
    }
    if (g_radix == 0) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.6e", v);
        return buf;
    }
    return base;
}

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
    struct sigaction sa{};
    sa.sa_handler = onSigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART: interrupt blocking reads so the loop resumes
    sigaction(SIGINT, &sa, nullptr);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::cout << "Plusalator - a fancy interactive calculator REPL\n"
                      << "Usage: plusalator [--eval <expr>]\n";
            return 0;
        } else if (a == "--version") {
            std::cout << "plusalator 1.2.0\n";
            return 0;
        } else if (a == "--eval" && i + 1 < argc) {
            Context ctx;
            // --eval output stays machine-bare unless FORCE_COLOR=1 opts in;
            // then the number wears the active theme's result color.
            const Theme* evalTheme = &ALL_THEMES[0];
            {
                std::string saved = loadConfigTheme();
                if (!saved.empty()) {
                    const Theme* nt = findTheme(saved);
                    if (nt) evalTheme = nt;
                }
            }
            // Restore angle + radix so one-shot expressions match the session.
            {
                std::string angle = loadSetting("angle");
                if (angle == "deg") ctx.angle = Context::Angle::DEG;
                else if (angle == "grad") ctx.angle = Context::Angle::GRAD;
                std::string mode = loadSetting("mode");
                if (mode == "hex") g_radix = 16;
                else if (mode == "bin") g_radix = 2;
                else if (mode == "sci") g_radix = 0;
            }
            try {
                double result = evalLine(argv[++i], ctx);
                if (colorEnabled())
                    std::cout << esc(evalTheme->rr, evalTheme->rg, evalTheme->rb) << formatValue(result) << escReset() << "\n";
                else
                    std::cout << formatValue(result) << "\n";
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
    loadUserVars(ctx);   // user vars from last session

    // Restore angle unit + output radix.
    std::string angleName = "rad";
    {
        std::string angle = loadSetting("angle");
        if (angle == "deg") { ctx.angle = Context::Angle::DEG; angleName = "deg"; }
        else if (angle == "grad") { ctx.angle = Context::Angle::GRAD; angleName = "grad"; }
        std::string mode = loadSetting("mode");
        if (mode == "hex") g_radix = 16;
        else if (mode == "bin") g_radix = 2;
        else if (mode == "sci") g_radix = 0;
    }

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
        if (errorFlash) { ar = 255; ag = 100; ab = 140; errorFlash = false; }
        out += escBold() + esc(ar, ag, ab) + "➤" + escReset() + " ";
        return out;
    };

    auto transientPromptText = [&cur]() -> std::string {
        std::string out;
        out += escDim();
        out += esc(cur->ar, cur->ag, cur->ab) + "⟨" + escReset();
        out += " ";
        out += esc(cur->hr, cur->hg, cur->hb) + "plusa" + escReset();
        out += " ";
        out += esc(cur->ar, cur->ag, cur->ab) + "⟩" + escReset();
        out += " ";
        out += escBold() + esc(cur->ar, cur->ag, cur->ab) + "➤" + escReset();
        out += escReset() + " ";
        return out;
    };

    auto printLine1 = [&cur, &ctx, &angleName]() {
        // Clear the current line (in case the cursor is mid-prompt from a prior
        // edit), then draw the gradient rule + right-side dim status on its own
        // line, and advance so the prompt lands on the next line.
        std::cout << "\r\033[2K";
        int w = termWidth();
        std::string status = cur->name + " · " + std::to_string((int)ctx.vars.size()) + " vars " + std::to_string((int)ctx.funcs.size()) + " funcs";
        if (angleName != "rad") status += " · " + angleName;
        if (g_radix == 16) status += " · hex";
        else if (g_radix == 2) status += " · bin";
        else if (g_radix == 0) status += " · sci";
        if (g_has_memory) status += " · M";
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
        // Also remove empty rows from the beginning
        while (!rows.empty() && rows.front().empty()) rows.erase(rows.begin());
        int n = (int)rows.size();
        for (int i = 0; i < n; i++) {
            if (rows[i].empty()) { std::cout << "\n"; continue; }
            // Enhanced multi-gradient: accent -> prompt -> result -> accent
            double f = (n > 1) ? (double)i / (double)(n - 1) : 0.0;
            int r, g, b;
            if (f < 0.33) {
                // Top third: accent to prompt
                double t = f * 3.0;
                r = (int)(cur->ar * (1 - t) + cur->pr * t);
                g = (int)(cur->ag * (1 - t) + cur->pg * t);
                b = (int)(cur->ab * (1 - t) + cur->pb * t);
            } else if (f < 0.66) {
                // Middle third: prompt to result
                double t = (f - 0.33) * 3.0;
                r = (int)(cur->pr * (1 - t) + cur->rr * t);
                g = (int)(cur->pg * (1 - t) + cur->rg * t);
                b = (int)(cur->pb * (1 - t) + cur->rb * t);
            } else {
                // Bottom third: result to accent
                double t = (f - 0.66) * 3.0;
                // Clamp t to [0, 1] to avoid negative interpolation
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                r = (int)(cur->rr * (1 - t) + cur->ar * t);
                g = (int)(cur->rg * (1 - t) + cur->ag * t);
                b = (int)(cur->rb * (1 - t) + cur->ab * t);
            }
            std::cout << escBold() << esc(r, g, b) << rows[i] << escReset() << "\n";
        }
    };

    printBanner();
    std::cout << esc(cur->hr, cur->hg, cur->hb) << "Tip: type " << escReset() << escBold()
              << esc(cur->pr, cur->pg, cur->pb) << "help" << escReset()
              << esc(cur->hr, cur->hg, cur->hb) << " for commands, or enter an expression to calculate."
              << escReset() << "\n";

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

        // No-argument commands must not swallow the rest of the line:
        // `ans * 2` is an expression, not the `ans` command with junk after it.
        // Commands that legitimately take arguments (ms/m+/m-/mode/del/settings)
        // are deliberately absent from this set. quit/exit/q too — their extra
        // words are ignored on purpose.
        static const std::set<std::string> noArgCmds = {
            "ans", "vars", "funcs", "history", "clear", "theme",
            "constants", "reset", "config", "version", "about",
            "mr", "mc", "deg", "rad", "grad",
        };
        if (!arg.empty() && noArgCmds.count(cmd)) cmd = "\x01expr"; // sentinel: evaluate as expression

        if (cmd == "help") {
            const int W = 72, LC = 22, RC = 44;
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
                {"primes [n]", "list primes up to n (max 1000)"},
                {"factorize / factors", "prime factorization of n"},
                {"isprime", "1 if n is prime, else 0"},
                {"nextprime/prevprime", "next/previous prime strictly after/before n"},
                {"cbrt", "cube root of x"},
                {"asinh/acosh/atanh", "inverse hyperbolic sine/cosine/tangent"},
                {"gcd/lcm", "greatest common / least common multiple"},
                {"ncr/npr", "combinations / permutations of n things r at a time"},
                {"clamp", "constrain x to [lo, hi]"},
                {"sign/fact", "sign of x (signum) / x! factorial"},
                {"log2", "base-2 logarithm"},
                {"ms <expr>", "store expression into memory"},
                {"mr", "recall memory"},
                {"m+ <expr>", "add expression to memory"},
                {"m- <expr>", "subtract expression from memory"},
                {"mc", "clear memory"},
                {"deg / rad / grad", "set angle unit for trig"},
                {"mode <dec|hex|bin|sci>", "set output radix"},
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
            std::set<std::pair<std::string,std::string>> rows;
            for (auto& kv : ctx.vars) rows.insert({kv.first, formatNumber(kv.second)});
            for (auto& kv : rows) {
                std::cout << esc( cur->pr, cur->pg, cur->pb) << kv.first << escReset()
                          << " = " << esc( cur->rr, cur->rg, cur->rb) << kv.second << escReset() << "\n";
            }
        } else if (cmd == "funcs") {
            if (ctx.funcs.empty()) {
                std::cout << escDim() << "(no user functions)" << escReset() << "\n";
            }
            std::vector<std::reference_wrapper<const std::pair<const std::string, UserFunc>>> funcs;
            funcs.reserve(ctx.funcs.size());
            for (auto& kv : ctx.funcs) funcs.emplace_back(kv);
            std::sort(funcs.begin(), funcs.end(), [](auto& a, auto& b){ return a.get().first < b.get().first; });
            for (auto& fk : funcs) {
                auto& kv = fk.get();
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
        } else if (cmd == "ms") {
            if (arg.empty()) { std::cout << esc( cur->er, cur->eg, cur->eb) << "usage: ms <expr>" << escReset() << "\n"; }
            else {
                try {
                    g_memory = evalLine(arg, ctx); g_has_memory = true;
                    std::cout << esc( cur->pr, cur->pg, cur->pb) << "memory" << escReset()
                              << " = " << esc( cur->rr, cur->rg, cur->rb) << formatValue(g_memory) << escReset() << "\n";
                } catch (const CalcError& err) {
                    std::cout << esc( cur->er, cur->eg, cur->eb) << err.msg << escReset() << "\n";
                }
            }
        } else if (cmd == "mr") {
            if (!g_has_memory) { std::cout << escDim() << "memory is empty (ms <expr> to store)" << escReset() << "\n"; }
            else {
                // Recall feeds `ans` so the value can chain in the next expression.
                ctx.vars["ans"] = g_memory;
                std::cout << esc( cur->pr, cur->pg, cur->pb) << "memory" << escReset()
                          << " = " << esc( cur->rr, cur->rg, cur->rb) << formatValue(g_memory) << escReset() << "\n";
            }
        } else if (cmd == "m+" || cmd == "m-") {
            if (arg.empty()) { std::cout << esc( cur->er, cur->eg, cur->eb) << "usage: " << cmd << " <expr>" << escReset() << "\n"; }
            else {
                try {
                    double v = evalLine(arg, ctx);
                    g_memory += (cmd == "m+") ? v : -v;
                    g_has_memory = true;
                    std::cout << esc( cur->pr, cur->pg, cur->pb) << "memory" << escReset()
                              << " = " << esc( cur->rr, cur->rg, cur->rb) << formatValue(g_memory) << escReset() << "\n";
                } catch (const CalcError& err) {
                    std::cout << esc( cur->er, cur->eg, cur->eb) << err.msg << escReset() << "\n";
                }
            }
        } else if (cmd == "mc") {
            g_memory = 0.0; g_has_memory = false;
            std::cout << escDim() << "memory cleared" << escReset() << "\n";
        } else if (cmd == "deg" || cmd == "rad" || cmd == "grad") {
            if (cmd == "deg") { ctx.angle = Context::Angle::DEG; angleName = "deg"; }
            else if (cmd == "grad") { ctx.angle = Context::Angle::GRAD; angleName = "grad"; }
            else { ctx.angle = Context::Angle::RAD; angleName = "rad"; }
            saveSettings(cur->name, angleName, g_radix == 16 ? "hex" : g_radix == 2 ? "bin" : g_radix == 0 ? "sci" : "dec");
            std::cout << escDim() << "angle unit: " << angleName << " (trig + inverse trig follow it)" << escReset() << "\n";
        } else if (cmd == "mode") {
            if (arg == "hex") g_radix = 16;
            else if (arg == "bin") g_radix = 2;
            else if (arg == "sci") g_radix = 0;
            else if (arg == "dec" || arg.empty()) g_radix = 10;
            else {
                std::cout << esc( cur->er, cur->eg, cur->eb) << "unknown mode: " << arg << " (dec|hex|bin|sci)" << escReset() << "\n";
                continue; // stay in the REPL loop
            }
            const char* name = g_radix == 16 ? "hex" : g_radix == 2 ? "bin" : g_radix == 0 ? "sci" : "dec";
            saveSettings(cur->name, angleName, name);
            std::cout << escDim() << "output mode: " << name << (g_radix == 10 ? " (automatic)" : "") << escReset() << "\n";
        } else if (cmd == "constants") {
            struct ConstGroup { const char* title; std::vector<std::pair<std::string, double>> items; };
            static const std::vector<ConstGroup> groups = {
                {"Geometry", {{"pi", 3.14159265358979323846}, {"tau", 6.28318530717958647692}, {"phi", 1.61803398874989484820}}},
                {"Mechanics", {{"grav", 9.80665}, {"G", 6.67430e-11}}},
                {"Thermodynamics", {{"kB", 1.380649e-23}, {"R", 8.314462618}, {"NA", 6.02214076e23}, {"c", 2.99792458e8}, {"qe", 1.602176634e-19}, {"h", 6.62607015e-34}, {"hbar", 1.054571817e-34}, {"sigma", 5.670374419e-8}}},
                {"Electromagnetism", {{"eps0", 8.8541878128e-12}, {"mu0", 1.25663706212e-6}}},
                {"Atoms & masses", {{"u", 1.66053906660e-27}, {"m_e", 9.1093837015e-31}, {"m_p", 1.67262192369e-27}, {"e", 2.71828182845904523536}}},
            };
            for (auto& g : groups) {
                std::cout << escDim() << "─ " << g.title << " ─" << escReset() << "\n";
                for (auto& c : g.items) {
                    std::cout << esc( cur->pr, cur->pg, cur->pb) << "  " << c.first << escReset()
                              << " = " << esc( cur->rr, cur->rg, cur->rb)
                              << formatNumber(c.second) << escReset() << "\n";
                }
            }
        } else if (cmd == "primes" || cmd == "factorize" || cmd == "factors") {
            long long n = 0;
            try { n = arg.empty() ? 100 : std::stoll(arg); }
            catch (...) { std::cout << esc( cur->er, cur->eg, cur->eb) << "invalid integer: " << arg << escReset() << "\n"; continue; }
            if (n < 2) {
                std::cout << esc( cur->er, cur->eg, cur->eb) << "needs an integer >= 2" << escReset() << "\n";
            } else if (cmd == "primes") {
                // list all primes up to n; cap display to avoid a 10k wall of text
                long long capped = std::min<long long>(n, 1000);
                std::vector<long long> pr;
                for (long long k = 2; k <= capped; k++) {
                    bool p = true;
                    for (long long d = 2; d * d <= k; d++) {
                        if (k % d == 0) { p = false; break; }
                    }
                    if (p) pr.push_back(k);
                }
                int col = 0;
                for (auto q : pr) { std::cout << esc( cur->rr, cur->rg, cur->rb) << q << escReset() << "  "; if (++col % 10 == 0) std::cout << "\n"; }
                if (!pr.empty() && col % 10 != 0) std::cout << "\n";
                std::cout << escDim() << "\n" << pr.size() << " primes <= " << capped << (capped < n ? "  (capped at 1000)" : "") << escReset() << "\n";
            } else if (cmd == "factorize" || cmd == "factors") {
                long long m = n;
                std::cout << esc( cur->pr, cur->pg, cur->pb) << n << escReset() << " = ";
                std::vector<long long> factors;
                long long d = 2;
                while (d * d <= m) {
                    while (m % d == 0) { factors.push_back(d); m /= d; }
                    d += (d == 2 ? 1 : 2);
                }
                if (m > 1) factors.push_back(m);
                if (factors.empty()) { std::cout << escDim() << "(prime)" << escReset() << "\n"; }
                else {
                    for (size_t i = 0; i < factors.size(); i++) {
                        if (i) std::cout << escDim() << " x " << escReset();
                        std::cout << esc( cur->rr, cur->rg, cur->rb) << factors[i] << escReset();
                    }
                    std::cout << "\n";
                }
            }
        } else if (cmd == "reset") {
            for (auto it = ctx.vars.begin(); it != ctx.vars.end(); ) {
                if (ctx.isBuiltin(it->first)) { it->second = (it->first == "ans") ? 0.0 : it->second; ++it; }
                else it = ctx.vars.erase(it);
            }
            ctx.funcs.clear();
            std::cout << escDim() << "state reset" << escReset() << "\n";
        } else if (cmd == "del") {
            if (arg.empty()) {
                std::cout << esc( cur->er, cur->eg, cur->eb) << "usage: del <name>" << escReset() << "\n";
            } else {
                bool protected_ = ctx.isBuiltin(arg);
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
            std::cout << escBold() << esc( cur->pr, cur->pg, cur->pb) << "plusalator 1.2.0" << escReset() << "\n";
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
                std::cout << "  " << esc(t.ar, t.ag, t.ab) << "████" << escReset();
                if (isCur) std::cout << "  ← current";
                std::cout << "\n";
            }
            std::cout << escDim() << "current: " << cur->name << " — use 'settings' to switch theme" << escReset() << "\n";
        } else if (cmd == "settings") {
            while (true) {
                printLine1();
                std::cout << escBold() << esc(cur->pr, cur->pg, cur->pb) << "⚙ settings" << escReset() << "\n\n";
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
                        std::cout << escBold() << esc(cur->pr, cur->pg, cur->pb) << "⚙ settings › theme" << escReset() << "\n\n";
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
                        if (nt) { cur = nt; saveSettings(nt->name, angleName, g_radix == 16 ? "hex" : g_radix == 2 ? "bin" : g_radix == 0 ? "sci" : "dec"); break; }
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
                    // Allow direct theme selection by number in settings menu
                    const Theme* nt = nullptr;
                    try { int n = std::stoi(choice); if (n >= 1 && n <= (int)ALL_THEMES.size()) nt = &ALL_THEMES[n - 1]; } catch (...) {}
                    if (nt) { cur = nt; saveSettings(nt->name, angleName, g_radix == 16 ? "hex" : g_radix == 2 ? "bin" : g_radix == 0 ? "sci" : "dec"); }
                    else {
                        std::cout << "\n" << esc(cur->er, cur->eg, cur->eb) << "unknown setting: " << choice << escReset() << "\n";
                    }
                }
            }
        } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;
        } else {
            try {
                double result = evalLine(line, ctx);
                std::cout << esc( cur->rr, cur->rg, cur->rb) << "= " << formatValue(result) << escReset() << "\n";
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
        if (g_sigint_seen) { // external SIGINT outside the editor: resume, never exit
            g_sigint_seen = 0;
            std::cout << "\n";
        }
    }
    saveUserVars(ctx);   // persist user-defined variables to ~/.config/plusalator/vars
    std::cout << "\n";
    return 0;
}
