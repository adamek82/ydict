#include <iostream>
#include "ydict/ydict.h"

int main()
{
    ydict::Dictionary dict;
    ydict::Config cfg;

    cfg.idx_path = "C:/Download/ydpdict/data/dict100.idx";      // TODO: For now, hardcoded for testing

    const bool ok = dict.init(cfg);
    std::cout << "init() => " << (ok ? "OK" : "FAIL") << "\n";
    std::cout << dict.version() << "\n";

    if (ok) {
        for (int i = 0; i < dict.wordCount() && i < 25; ++i) {
            const auto* e = dict.wordAt(i);
            std::cout << "  [" << i << "] datOffset=" << e->dat_offset
                      << " word=\"" << e->word << "\"\n";
        }
    }

    return ok ? 0 : 1;
}
