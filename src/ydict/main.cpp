#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cctype>
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

static std::string_view trim(std::string_view s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) {
        --e;
    }
    return s.substr(b, e - b);
}

static bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static bool endsWithSentencePunct(std::string_view s)
{
    if (s.empty()) return false;
    const char c = s.back();
    return c == '.' || c == '?' || c == '!';
}

static bool isPosTag(std::string_view t)
{
    // Minimal set for now (matches what we see in ydpdict output)
    return t == "n" || t == "adj" || t == "vt" || t == "vi";
}

static bool isExampleLine(std::string_view t)
{
    // Heuristic: sentence-like lines, usually examples under a sense/meaning.
    if (t.empty()) return false;
    if (startsWith(t, "...")) return true;

    const unsigned char c0 = static_cast<unsigned char>(t.front());
    if (std::isupper(c0) && endsWithSentencePunct(t)) {
        return true;
    }
    return false;
}

static bool isPhraseBullet(std::string_view t)
{
    // Heuristic: collocations/idioms ("to ...", "a ...") that are not full sentences.
    if (t.empty()) return false;
    if (endsWithSentencePunct(t)) return false;

    return startsWith(t, "to ") ||
           startsWith(t, "a ")  ||
           startsWith(t, "an ") ||
           startsWith(t, "he's ") ||
           startsWith(t, "she's ") ||
           startsWith(t, "we're ") ||
           startsWith(t, "i'm ");
}

static void ensureBlankLine(std::string& out)
{
    if (out.empty()) return;
    if (out.back() != '\n') out.push_back('\n');
    // make it a *blank* line => two consecutive '\n'
    if (out.size() < 2 || out[out.size() - 2] != '\n') out.push_back('\n');
}

static std::string formatPlainForCli(const std::string& plain)
{
    std::string out;
    out.reserve(plain.size() + 64);

    bool firstContentLineTrimmed = false;

    size_t i = 0;
    while (i <= plain.size()) {
        size_t j = plain.find('\n', i);
        if (j == std::string::npos) j = plain.size();

        std::string_view line(&plain[i], j - i);
        // strip trailing CR if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line = line.substr(0, line.size() - 1);
        }

        std::string_view t = trim(line);

        // (1) Trim leading indentation only for the very first non-empty line
        if (!firstContentLineTrimmed && !t.empty()) {
            firstContentLineTrimmed = true;
            line = t; // remove leading spaces/tabs from the headword line
            t = trim(line);
        }

        if (t.empty()) {
            out.push_back('\n');
        } else if (isPosTag(t)) {
            // (2) POS as headers with a blank line before them
            ensureBlankLine(out);
            out += "== ";
            out += t;
            out += " ==\n";
        } else if (isPhraseBullet(t)) {
            // (3) Bullet-like phrases/collocations
            out += "- ";
            out += t;
            out.push_back('\n');
        } else if (isExampleLine(t)) {
            // (3) Indent example sentences
            out += "  ";
            out += t;
            out.push_back('\n');
        } else {
            // keep original line (including any intentional indentation)
            out.append(line.data(), line.size());
            out.push_back('\n');
        }

        if (j == plain.size()) break;
        i = j + 1;
    }

    return out;
}

struct CliOptions
{
    bool show_plain = false;   // default: pretty
    bool write_plain_file = false; // default: do not write <word>.plain.txt
    bool dump_index = false;        // default: do not dump full index
    std::string index_file = "ydict.index.txt";
    bool help = false;
    std::string_view word;     // first non-option argument
};

static void printUsage(const char* exe)
{
    std::cout
        << "Usage:\n"
        << "  " << exe << " [options] <word>\n"
        << "  " << exe << " --help\n"
        << "\n"
        << "Options:\n"
        << "  --show-plain, --plain             Print raw plain text (instead of pretty)\n"
        << "  --show-pretty, --pretty           Print pretty text (default)\n"
        << "  --write-plain-file, --save-plain  Write <word>.plain.txt to disk\n"
        << "  --dump-index, --dump-idx          Write full index dump to ydict.index.txt\n"
        << "  --index-file <path>               Set index dump path (implies --dump-index)\n"
        << "\n"
        << "Notes:\n"
        << "  - Default output is rendered from the original RTF stream (pretty, no colors).\n"
        << "  - Use --show-plain to print raw plain text instead (debug / regression checks).\n"
        << "  - By default, no files are written.\n"
        << "  - If no <word> is provided, the program runs the existing smoke tests.\n";
}

static CliOptions parseCli(int argc, char** argv)
{
    CliOptions opt;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];

        if (a == "--show-plain" || a == "--plain") {
            opt.show_plain = true;
            continue;
        }
        if (a == "--show-pretty" || a == "--pretty") {
            opt.show_plain = false;
            continue;
        }
        if (a == "--write-plain-file" || a == "--save-plain" || a == "--save-plain-file") {
            opt.write_plain_file = true;
            continue;
        }
        if (a == "--dump-index" || a == "--dump-idx") {
            opt.dump_index = true;
            continue;
        }
        if (a == "--index-file") {
            if (i + 1 >= argc) {
                opt.help = true;
                continue;
            }
            std::string_view p = argv[++i];
            if (!p.empty() && p.front() == '-') {
                // Looks like another option -> treat as error for now.
                opt.help = true;
                continue;
            }
            opt.index_file = std::string(p);
            opt.dump_index = true; // implied
            continue;
        }
        if (a.size() > 12 && a.substr(0, 12) == "--index-file=") {
            std::string_view p = a.substr(12);
            if (p.empty()) {
                opt.help = true;
                continue;
            }
            opt.index_file = std::string(p);
            opt.dump_index = true; // implied
            continue;
        }
        if (a == "-h" || a == "--help") {
            opt.help = true;
            continue;
        }

        if (!a.empty() && a.front() == '-') {
            // Unknown option -> show usage for now.
            opt.help = true;
            continue;
        }

        if (opt.word.empty()) {
            opt.word = a;
        } else {
            // Multiple positional args -> usage error (for now).
            opt.help = true;
        }
    }

    return opt;
}

static std::string sanitizeFilename(std::string s)
{
    for (char& c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
            c = '_';
    }
    if (s.empty()) s = "out";
    return s;
}

static void dumpFullDefinition(const ydict::Dictionary& dict,
                               std::string_view word,
                               bool showPlain,
                               bool writePlainFile)
{
    const int idx = dict.findWord(word);
    if (idx < 0) {
        std::cout << "word=\"" << word << "\" NOT FOUND\n";
        std::cout << "\nSuggestions for prefix \"" << word << "\":\n";
        const auto hits = dict.suggest(word, /*maxResults=*/20);
        if (hits.empty()) {
            std::cout << "  (no matches)\n";
            return;
        }
        for (int k = 0; k < static_cast<int>(hits.size()); ++k) {
            const auto* e = dict.wordAt(hits[k]);
            std::cout << "  [" << k << "] idx=" << hits[k]
                      << " word=\"" << (e ? e->word : "?") << "\"\n";
        }
        return;
    }

    const auto* e = dict.wordAt(idx);
    std::cout << "==== FULL DUMP ====\n";
    std::cout << "word=\"" << word << "\" idx=" << idx
              << " datOffset=" << (e ? e->dat_offset : 0) << "\n";

    if (showPlain) {
        const std::string plain = dict.readPlainText(idx);
        std::cout << "plain bytes=" << plain.size() << "\n";
        std::cout << "---- BEGIN (plain) ----\n";
        std::cout << plain << "\n";
        std::cout << "----  END  (plain) ----\n";
    } else {
        std::cout << "---- BEGIN (pretty) ----\n";

        const std::string rtf = dict.readRtf(idx);
        std::cout << "rtf bytes=" << rtf.size() << "\n";

        // Preferred path: render directly from RTF to preserve semantic cues (bullets/indent/phonetics).
        std::string pretty = ydict::renderRtfForCli(rtf);

        // Safety fallback: if RTF render yields nothing, fall back to the old plain-based formatter.
        if (pretty.empty()) {
            const std::string plain = dict.readPlainText(idx);
            pretty = formatPlainForCli(plain);
        }

        std::cout << pretty << "\n";
        std::cout << "----  END  (pretty) ----\n";
    }

    if (writePlainFile) {
        // Plain-text file remains useful as a debug artifact (RTF->plain conversion).
        const std::string plain = dict.readPlainText(idx);

        const std::string fname = sanitizeFilename(std::string(word)) + ".plain.txt";
        std::ofstream out(fname, std::ios::binary);
        if (out) {
            out.write(plain.data(), static_cast<std::streamsize>(plain.size()));
            std::cout << "(saved to " << fname << ")\n";
        } else {
            std::cout << "(failed to save " << fname << ")\n";
        }
    }
}

int main(int argc, char** argv)
{
    const CliOptions cli = parseCli(argc, argv);
    if (cli.help) {
        printUsage(argv[0]);
        return 0;
    }

    ydict::Dictionary dict;
    ydict::Config cfg;

    cfg.idx_path = "C:/Download/ydpdict/data/dict100.idx";      // TODO: For now, hardcoded for testing
    cfg.dat_path = "C:/Download/ydpdict/data/dict100.dat";      // TODO: For now, hardcoded for testing

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Optional debug dump of the loaded idx table (handled by the library).
    if (cli.dump_index) {
        cfg.idx_dump_path = cli.index_file;
    }

    const bool ok = dict.init(cfg);

    std::cout << "init() => " << (ok ? "OK" : "FAIL") << "\n";
    std::cout << dict.version() << "\n";

    if (ok) {
        if (cli.dump_index) {
            // Best-effort: lib writes it during init(); we just inform what was requested.
            std::cout << "(index dump requested: " << cli.index_file << ")\n";
        }

        // On-demand full dump:
        //   ydict_app.exe get
        //   ydict_app.exe --show-plain get
        if (!cli.word.empty()) {
            dumpFullDefinition(dict,
                               cli.word,
                               /*showPlain=*/cli.show_plain,
                               /*writePlainFile=*/cli.write_plain_file);
            return 0;
        }

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
