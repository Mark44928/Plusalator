#pragma once

#include <string>
#include <vector>
#include <termios.h>

class LineEditor {
public:
    LineEditor();
    ~LineEditor();

    bool edit(std::string& out, const std::string& prompt);

    void addHistory(const std::string& line);
    const std::vector<std::string>& history() const { return history_; }
    bool isTty() const { return isTty_; }

    // Right-aligned status rendered on the active line (dim). Set before each edit().
    void setRightStatus(const std::string& s) { rightStatus_ = s; }

private:
    bool isTty_;
    std::string histPath_;
    std::vector<std::string> history_;
    int histIndex_ = -1;
    std::string navPrefix_;
    std::string rightStatus_;

    struct termios origTty_;
    bool ttyRaw_ = false;

    void loadHistory();
    void saveHistory();
    void enterRaw();
    void leaveRaw();
    static int termWidth();
    static size_t visibleLen(const std::string& s);
    std::string ghostSuffix(const std::string& buf) const;
    static bool hasPrefix(const std::string& entry, const std::string& prefix);
    void redraw(const std::string& prompt, const std::string& buf, size_t cur);
    int readKey(std::string& text);
    void insertChar(std::string& buf, size_t& cur, char c);
    void insertStr(std::string& buf, size_t& cur, const std::string& s);
    void historyUp(std::string& buf, size_t& cur);
    void historyDown(std::string& buf, size_t& cur);
};
