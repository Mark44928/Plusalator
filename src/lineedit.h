#pragma once

#include <string>
#include <vector>
#include <termios.h>

class LineEditor {
public:
    LineEditor();
    ~LineEditor();

    // Edit a line. Returns true when Enter submits a line (out filled, without newline).
    // Returns false when the user quit (Ctrl+D on an empty buffer).
    bool edit(std::string& out, const std::string& prompt);

    void addHistory(const std::string& line);
    const std::vector<std::string>& history() const { return history_; }
    bool isTty() const { return isTty_; }

private:
    bool isTty_;
    std::string histPath_;
    std::vector<std::string> history_;
    int histIndex_ = -1;      // -1 = not navigating history
    std::string navPrefix_;   // prefix used when history navigation started

    struct termios origTty_;
    bool ttyRaw_ = false;

    void loadHistory();
    void saveHistory();
    void enterRaw();
    void leaveRaw();
    static void redraw(const std::string& prompt, const std::string& buf, size_t cur);
    int readKey(std::string& pasteTail); // blocking; returns key code or 0 on EOF
    void insertChar(std::string& buf, size_t& cur, char c);
    void insertStr(std::string& buf, size_t& cur, const std::string& s);
    void historyUp(std::string& buf, size_t& cur);
    void historyDown(std::string& buf, size_t& cur);
    static bool hasPrefix(const std::string& entry, const std::string& prefix);
};
