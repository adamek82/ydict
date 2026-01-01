#include <iostream>
#include <vector>
#include <algorithm>
#include <string_view>
#include "ydict/ydict.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#include <Windows.h>
#endif

static void dumpHeadTail(const std::string& s,
                         size_t headMax,
                         size_t tailMax,
                         std::string_view indent,
                         bool blankLineBeforeTail)
{
    const size_t headLen = std::min(headMax, s.size());
    std::cout << indent << "[head]\n";
    std::cout << s.substr(0, headLen) << "\n";

    if (s.size() > headLen) {
        std::cout << indent << "...\n"
                  << indent << "(truncated, total=" << s.size() << ")\n";

        const size_t tailLen = std::min(tailMax, s.size());
        if (blankLineBeforeTail) {
            std::cout << "\n";
        }
        std::cout << indent << "[tail]\n"
                  << s.substr(s.size() - tailLen, tailLen) << "\n";
    }
}

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
        dumpHeadTail(text, /*headMax=*/400, /*tailMax=*/120, /*indent=*/"", /*blankLineBeforeTail=*/true);

        // extra smoke: lookup several typical words by spelling (keep old tests above)
        const std::vector<std::string> probes = {
            "abdicate",
            "abandon",
            "abbreviation",
            "abbey",
            "abacus",
            "computer",
            "house",
            "love",
        };

        std::cout << "\n--- lookup tests (findWord + plain) ---\n";
        for (const auto& w : probes) {
            const int idx = dict.findWord(w);
            std::cout << "\nword=\"" << w << "\" => idx=" << idx << "\n";
            if (idx < 0) {
                std::cout << "  NOT FOUND\n";
                continue;
            }

            const auto* e = dict.wordAt(idx);
            std::cout << "  datOffset=" << (e ? e->dat_offset : 0) << "\n";

            const std::string plain = dict.readPlainText(idx);
            const size_t n = std::min<size_t>(300, plain.size());
            std::cout << "  plain(" << plain.size() << " bytes):\n";
            dumpHeadTail(plain, /*headMax=*/300, /*tailMax=*/120, /*indent=*/"  ", /*blankLineBeforeTail=*/false);
        }

        // prefix suggestions smoke test (left-pane behavior in ydpdict)
        const std::vector<std::string> prefixes = {
            "get",
            "get ",
            "to get",
            "hou",
            "comp",
        };

        std::cout << "\n--- prefix suggestions (suggest) ---\n";
        for (const auto& p : prefixes) {
            std::cout << "\nprefix=\"" << p << "\"\n";
            const auto hits = dict.suggest(p, /*limit=*/12);
            if (hits.empty()) {
                std::cout << "  (no matches)\n";
                continue;
            }

            for (int k = 0; k < static_cast<int>(hits.size()); ++k) {
                const int wi = hits[k];
                const auto* e = dict.wordAt(wi);
                std::cout << "  [" << k << "] idx=" << wi
                          << " word=\"" << (e ? e->word : "?") << "\"\n";
            }

            // show definition for the first suggestion (like selecting it in UI)
            const int firstIdx = hits.front();
            const auto* e0 = dict.wordAt(firstIdx);
            std::cout << "  \n  selected=\"" << (e0 ? e0->word : "?") << "\"\n";
            const std::string def = dict.readPlainText(firstIdx);
            dumpHeadTail(def, /*headMax=*/220, /*tailMax=*/120, /*indent=*/"  ", /*blankLineBeforeTail=*/false);
        }
    }

    return ok ? 0 : 1;
}
