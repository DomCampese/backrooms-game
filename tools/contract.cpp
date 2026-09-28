// Contract tests for src/core. Generates a fixed set of chunks and asks the
// world fixed questions, and compares the answers with the text files in
// tests/golden. Another engine running the same core must reproduce them
// exactly. Links src/core alone; the tests themselves are in
// tools/contract_lib.cpp.
//
//   tools/sandbox-build.sh contract
//   ./contract --check [DIR]     # compare with DIR (default tests/golden)
//   ./contract --write [DIR]     # regenerate; only for a deliberate generator change
//
// Floats print as %.9g, which round-trips a float, so the comparison is exact.
#include "contract_lib.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
const char *usage = "usage: contract [--check|--write] [DIR]   (default --check tests/golden)\n";
}

int main(int argc, char **argv) {
    bool write = false;
    std::string dir = "tests/golden";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--write") write = true;
        else if (a == "--check") write = false;
        else if (a[0] != '-') dir = a;
        else { fputs(usage, stderr); return 2; }
    }
    std::vector<contract::Golden> gs = contract::produce();
    if (write) {
        for (const contract::Golden &g : gs) {
            std::string path = dir + "/" + g.name;
            FILE *f = fopen(path.c_str(), "wb");
            if (!f) { fprintf(stderr, "contract: cannot write %s\n", path.c_str()); return 2; }
            fwrite(g.text.data(), 1, g.text.size(), f);
            fclose(f);
            printf("wrote %s (%zu bytes)\n", path.c_str(), g.text.size());
        }
        return 0;
    }
    std::string report;
    int bad = contract::check(dir, gs, report);
    fputs(report.c_str(), stdout);
    printf(bad ? "contract: FAILED\n" : "contract: passed\n");
    return bad ? 1 : 0;
}
