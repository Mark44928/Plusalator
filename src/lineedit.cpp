#include "lineedit.h"
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <iostream>

enum {
    KEY_ENTER = 256,
    KEY_UP = 257,
    KEY_DOWN = 258,
    KEY_RIGHT = 259,
    KEY_LEFT = 260,
    KEY_HOME = 261,
    KEY_END = 262,
    KEY_DELETE = 263,
    KEY_BACKSPACE = 264,
    KEY_CTRL_C = 265,
    KEY_CTRL_D = 266,
    KEY_CTRL_U = 267,
    KEY_CTRL_K = 268,
    KEY_CTRL_A = 269,
    KEY_CTRL_E = 270,
    KEY_CTRL_L = 271,
    KEY_CTRL_F = 272,
    KEY_PASTE = 273,
    KEY_IGNORE = 274,
    KEY_EOF = -1
};

LineEditor::LineEditor() : isTty_(isatty(STDIN_FILENO) != 0) {
    char* home = getenv("HOME");
    histPath_ = (home ? home : std::string(".")) + "/.plusalator_history";
    tcgetattr(STDIN_FILENO, &origTty_);
    loadHistory();
}

LineEditor::~LineEditor() {
    leaveRaw();
    saveHistory();
}

void LineEditor::loadHistory() {
    history_.clear();
    FILE* f = fopen(histPath_.c_str(), "r");
    if (!f) return;
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
        if (len > 0) history_.push_back(buf);
    }
    fclose(f);
    while (history_.size() > 200) history_.erase(history_.begin());
}

void LineEditor::saveHistory() {
    FILE* f = fopen(histPath_.c_str(), "w");
    if (!f) return;
    size_t start = history_.size() > 200 ? history_.size() - 200 : 0;
    for (size_t i = start; i < history_.size(); i++)
        fprintf(f, "%s\n", history_[i].c_str());
    fclose(f);
}

void LineEditor::addHistory(const std::string& line) {
    if (line.empty()) return;
    if (!history_.empty() && history_.back() == line) return;
    history_.push_back(line);
    while (history_.size() > 200) history_.erase(history_.begin());
    saveHistory();
}

int LineEditor::termWidth() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) return (int)w.ws_col;
    return 80;
}

size_t LineEditor::visibleLen(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !((unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7e)) i++;
            continue;
        }
        n++;
    }
    return n;
}

void LineEditor::enterRaw() {
    if (!isTty_) return;
    struct termios raw = origTty_;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | IXOFF);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    ttyRaw_ = true;
    fputs("\033[?2004h", stdout);
    fflush(stdout);
}

void LineEditor::leaveRaw() {
    if (!ttyRaw_) return;
    fputs("\033[?2004l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &origTty_);
    ttyRaw_ = false;
}

std::string LineEditor::ghostSuffix(const std::string& buf) const {
    // most recent history entry that starts with buf and is longer than buf
    for (int i = (int)history_.size() - 1; i >= 0; i--) {
        if (hasPrefix(history_[i], buf) && history_[i].size() > buf.size())
            return history_[i].substr(buf.size());
    }
    return "";
}

bool LineEditor::hasPrefix(const std::string& entry, const std::string& prefix) {
    if (prefix.size() > entry.size()) return false;
    return entry.compare(0, prefix.size(), prefix) == 0;
}

void LineEditor::redraw(const std::string& prompt, const std::string& buf, size_t cur) {
    std::string out;
    out.reserve(prompt.size() + buf.size() + rightStatus_.size() + 64);
    out += "\r";
    out += "\033[K";
    out += prompt;
    out += buf;

    size_t ghostLen = 0;
    if (cur == buf.size() && !buf.empty()) {
        std::string suf = ghostSuffix(buf);
        if (!suf.empty()) {
            out += "\033[2m";
            out += suf;
            out += "\033[0m";
            ghostLen = suf.size();
        }
    }

    // position cursor at end of the typed buffer (before the ghost)
    if (ghostLen > 0) {
        char tmp[32];
        snprintf(tmp, sizeof tmp, "\033[%zuD", ghostLen);
        out += tmp;
    }

    // right-aligned, dim status line via save-cursor / column / restore
    if (!rightStatus_.empty()) {
        size_t slen = visibleLen(rightStatus_);
        int w = termWidth();
        int col = (int)w - (int)slen;
        if (col < 0) col = 0;
        out += "\033[s"; // save cursor
        {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "\033[%dG", col + 1);
            out += tmp;
        }
        out += "\033[2m";
        out += rightStatus_;
        out += "\033[0m";
        out += "\033[u"; // restore cursor
    }

    size_t left = buf.size() - cur;
    if (left > 0) {
        char tmp[32];
        snprintf(tmp, sizeof tmp, "\033[%zuD", left);
        out += tmp;
    }

    fputs(out.c_str(), stdout);
    fflush(stdout);
}

int LineEditor::readKey(std::string& text) {
    text.clear();
    char b;
    if (read(STDIN_FILENO, &b, 1) <= 0) return KEY_EOF;
    if (b != 0x1b) {
        if (b == 0x03) return KEY_CTRL_C;
        if (b == 0x04) return KEY_CTRL_D;
        if (b == 0x0a || b == 0x0d) return KEY_ENTER;
        if (b == 0x7f) return KEY_BACKSPACE;
        if (b == 0x01) return KEY_CTRL_A;
        if (b == 0x05) return KEY_CTRL_E;
        if (b == 0x15) return KEY_CTRL_U;
        if (b == 0x0b) return KEY_CTRL_K;
        if (b == 0x0c) return KEY_CTRL_L;
        if (b == 0x06) return KEY_CTRL_F;
        if (b == 0x09) return KEY_IGNORE;
        if (b < 0x20) return KEY_IGNORE;
        return (int)(unsigned char)b;
    }
    if (read(STDIN_FILENO, &b, 1) <= 0) return KEY_IGNORE;
    if (b != '[') return KEY_IGNORE;
    std::string params;
    while (true) {
        if (read(STDIN_FILENO, &b, 1) <= 0) return KEY_IGNORE;
        if (b >= 0x40 && b <= 0x7E) {
            if (b == 'A') return KEY_UP;
            if (b == 'B') return KEY_DOWN;
            if (b == 'C') return KEY_RIGHT;
            if (b == 'D') return KEY_LEFT;
            if (b == 'H') return KEY_HOME;
            if (b == 'F') return KEY_END;
            if (b == '~') {
                if (params == "1" || params == "7") return KEY_HOME;
                if (params == "4" || params == "8") return KEY_END;
                if (params == "3") return KEY_DELETE;
                if (params == "200") {
                    std::string collected;
                    while (true) {
                        char c;
                        if (read(STDIN_FILENO, &c, 1) <= 0) break;
                        if (c == 0x1b) {
                            char b2;
                            if (read(STDIN_FILENO, &b2, 1) <= 0) { collected += 0x1b; break; }
                            if (b2 == '[') {
                                std::string p2;
                                while (true) {
                                    if (read(STDIN_FILENO, &b2, 1) <= 0) break;
                                    if (b2 >= 0x40 && b2 <= 0x7E) {
                                        if (b2 == '~' && p2 == "201") {
                                            text = collected;
                                            return KEY_PASTE;
                                        }
                                        break;
                                    }
                                    p2 += b2;
                                }
                                collected += 0x1b;
                                collected += '[';
                                collected += p2;
                                collected += b2;
                                continue;
                            }
                            collected += 0x1b;
                            collected += b2;
                            continue;
                        }
                        if (c >= 0x20) collected += c;
                    }
                    text = collected;
                    return KEY_PASTE;
                }
            }
            return KEY_IGNORE;
        }
        params += b;
    }
}

void LineEditor::historyUp(std::string& buf, size_t& cur) {
    if (histIndex_ == -1) {
        navPrefix_ = buf;
        histIndex_ = (int)history_.size();
    }
    int found = -1;
    for (int i = histIndex_ - 1; i >= 0; i--) {
        if (hasPrefix(history_[i], navPrefix_)) { found = i; break; }
    }
    if (found >= 0) {
        buf = history_[found];
        cur = buf.size();
        histIndex_ = found;
    }
}

void LineEditor::historyDown(std::string& buf, size_t& cur) {
    if (histIndex_ == -1) return;
    int found = -1;
    for (int i = histIndex_ + 1; i < (int)history_.size(); i++) {
        if (hasPrefix(history_[i], navPrefix_)) { found = i; break; }
    }
    if (found >= 0) {
        buf = history_[found];
        cur = buf.size();
        histIndex_ = found;
    } else {
        buf = navPrefix_;
        cur = buf.size();
        histIndex_ = -1;
    }
}

void LineEditor::insertChar(std::string& buf, size_t& cur, char c) {
    buf.insert(cur, 1, c);
    cur++;
}

void LineEditor::insertStr(std::string& buf, size_t& cur, const std::string& s) {
    buf.insert(cur, s);
    cur += s.size();
}

bool LineEditor::edit(std::string& out, const std::string& prompt) {
    out.clear();
    if (!isTty_) {
        std::cout << prompt << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) return false;
        out = line;
        if (!out.empty()) addHistory(out);
        return true;
    }

    enterRaw();
    std::string buf;
    size_t cur = 0;
    histIndex_ = -1;
    bool submitted = false;
    bool quit = false;

    redraw(prompt, buf, cur);

    while (true) {
        std::string pasteText;
        int key = readKey(pasteText);

        if (key == KEY_EOF) { quit = true; break; }
        if (key == KEY_IGNORE) continue;

        if (key >= 256) {
            switch (key) {
                case KEY_ENTER: {
                    submitted = true;
                    goto done;
                }
                case KEY_CTRL_C: {
                    buf.clear();
                    cur = 0;
                    histIndex_ = -1;
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_CTRL_D: {
                    if (buf.empty()) { quit = true; goto done; }
                    if (cur < buf.size()) buf.erase(cur, 1);
                    histIndex_ = -1;
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_CTRL_U: {
                    buf.erase(0, cur);
                    cur = 0;
                    histIndex_ = -1;
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_CTRL_K: {
                    buf.erase(cur);
                    histIndex_ = -1;
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_CTRL_A: { cur = 0; redraw(prompt, buf, cur); break; }
                case KEY_CTRL_E: { cur = buf.size(); redraw(prompt, buf, cur); break; }
                case KEY_CTRL_L: {
                    fputs("\033[2J\033[H", stdout);
                    fflush(stdout);
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_CTRL_F: {
                    // accept ghost if at end
                    if (cur == buf.size() && !buf.empty()) {
                        std::string suf = ghostSuffix(buf);
                        if (!suf.empty()) { insertStr(buf, cur, suf); }
                    }
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_UP: { historyUp(buf, cur); redraw(prompt, buf, cur); break; }
                case KEY_DOWN: { historyDown(buf, cur); redraw(prompt, buf, cur); break; }
                case KEY_LEFT: {
                    if (cur > 0) cur--;
                    histIndex_ = -1;
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_RIGHT: {
                    if (cur == buf.size() && !buf.empty()) {
                        std::string suf = ghostSuffix(buf);
                        if (!suf.empty()) { insertStr(buf, cur, suf); break; }
                    }
                    if (cur < buf.size()) cur++;
                    histIndex_ = -1;
                    redraw(prompt, buf, cur);
                    break;
                }
                case KEY_HOME: { cur = 0; histIndex_ = -1; redraw(prompt, buf, cur); break; }
                case KEY_END: { cur = buf.size(); histIndex_ = -1; redraw(prompt, buf, cur); break; }
                case KEY_DELETE: { if (cur < buf.size()) buf.erase(cur, 1); histIndex_ = -1; redraw(prompt, buf, cur); break; }
                case KEY_BACKSPACE: { if (cur > 0) { buf.erase(cur - 1, 1); cur--; } histIndex_ = -1; redraw(prompt, buf, cur); break; }
                case KEY_PASTE: {
                    histIndex_ = -1;
                    insertStr(buf, cur, pasteText);
                    redraw(prompt, buf, cur);
                    break;
                }
            }
        } else {
            histIndex_ = -1;
            insertChar(buf, cur, (char)key);
            redraw(prompt, buf, cur);
        }
    }

done:
    leaveRaw();
    if (submitted) {
        out = buf;
        if (!out.empty()) addHistory(out);
    }
    return !quit;
}
