#include <iostream>
#include "ydict/ydict.h"

#ifdef _WIN32
#include <Windows.h>
#endif

int main()
{
    ydict::Dictionary dict;
    ydict::Config cfg;

    cfg.idx_path = "C:/Download/ydpdict/data/dict100.idx";      // TODO: For now, hardcoded for testing
    cfg.dat_path = "C:/Download/ydpdict/data/dict100.dat";      // TODO: For now, hardcoded for testing

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const bool ok = dict.init(cfg);
    std::cout << "init() => " << (ok ? "OK" : "FAIL") << "\n";
    std::cout << dict.version() << "\n";

    if (ok) {
        for (int i = 0; i < dict.wordCount() && i < 25; ++i) {
            const auto* e = dict.wordAt(i);
            std::cout << "  [" << i << "] datOffset=" << e->dat_offset
                      << " word=\"" << e->word << "\"\n";
        }

        const int probe = 24; // e.g. "abdicate" from our EN-PL dictionary
        std::string rtf = dict.readRtf(probe);

        std::cout << "\nreadRtf(" << probe << ") => " << rtf.size() << " bytes\n";
        if (!rtf.empty()) {
            const size_t previewLen = std::min<size_t>(200, rtf.size());
            std::cout << "RTF preview:\n" << rtf.substr(0, previewLen) << "\n";
        } else {
            std::cout << "RTF read failed.\n";
        }

        // plain text smoke test (RTF -> plain, Win-1250/phonetic -> UTF-8)
        std::string text = dict.readPlainText(probe);
        std::cout << "\nplain(" << probe << ") => " << text.size() << " bytes\n";
        std::cout << text.substr(0, std::min<size_t>(400, text.size())) << "\n";
    }

    return ok ? 0 : 1;
}
