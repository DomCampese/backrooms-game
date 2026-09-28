#pragma once
// Core's contract: generate the golden answers and compare them with a
// directory of golden files (docs/migration.md, "Contract tests").
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace contract {

// One golden file's name and the text core produces for it.
struct Golden {
    std::string name, text;
    void put(const char *fmt, ...) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        text += buf;
    }
};

// Every golden file's text. A probe the generator no longer produces (no locked
// door in the window, say) exits the process with status 2: core has changed
// too much for the file to be compared.
std::vector<Golden> produce();

// Compares `gs` with the files in `dir` and appends one line per file, and the
// first differing lines of each, to `report`. Returns the number of files that
// differ or are missing.
int check(const std::string &dir, const std::vector<Golden> &gs, std::string &report);

}  // namespace contract
