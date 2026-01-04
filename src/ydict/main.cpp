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

static std::string_view trim(std::string_view s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
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
    char c = s.back();
    return c == '.' || c == '!' || c == '?';
}

static bool isHeadLine(std::string_view s)
{
    // In our plain format, the first line is typically: " word [phonetic]"
    // and is preceded by "[head]" marker line.
    return startsWith(s, "[head]");
}

static bool isPosLine(std::string_view s)
{
    // Heuristic: part-of-speech lines are short: "n", "vt", "vi", "adj", "adv", "prep", "cpd" etc.
    // Keep it permissive; it's used only for formatting.
    s = trim(s);
    if (s.empty()) return false;

    // Must be mostly letters and at most 4 chars.
    if (s.size() > 4) return false;
    for (char c : s) {
        if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

static bool isExampleLine(std::string_view s)
{
    // Example sentences often start with a capital letter or an opening quote.
    s = trim(s);
    if (s.empty()) return false;

    const unsigned char c0 = static_cast<unsigned char>(s.front());
    if (std::isupper(c0)) return true;
    if (s.front() == '"' || s.front() == '\'') return true;

    // Also allow leading ellipsis.
    if (startsWith(s, "...")) return true;

    return false;
}

static void dumpHeadTail(const std::string& s,
                         size_t headMax,
                         size_t tailMax,
                         const std::string& indent,
                         bool blankLineBeforeTail)
{
    if (s.size() <= headMax + tailMax) {
        std::cout << indent << s << "\n";
        return;
    }

    std::cout << indent << s.substr(0, headMax) << "\n"
              << indent << "  ...\n";

    if (blankLineBeforeTail) {
        std::cout << indent << "  (truncated, total=" << s.size() << ")\n"
                  << indent << "  [tail]\n";
    } else {
        std::cout << indent << "  (truncated, total=" << s.size() << ")\n";
    }

    std::cout << indent << s.substr(s.size() - tailMax) << "\n";
}

static std::string formatPlainForCli(const std::string& plain)
{
    // Convert the library's "plain" format into a pretty CLI format.
    //
    // Input (plain):
    //   [head]
    //    word [phon]
    //
    //   vt
    //    translation...
    //   example.
    //
    // Output (pretty):
    //   word [phon]
    //
    //   vt
    //   translation...
    //     example.
    //
    std::string out;
    out.reserve(plain.size() + 64);

    std::vector<std::string_view> lines;
    {
        std::string_view sv(plain);
        size_t pos = 0;
        while (pos <= sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) {
                lines.push_back(sv.substr(pos));
                break;
            }
            lines.push_back(sv.substr(pos, eol - pos));
            pos = eol + 1;
        }
    }

    bool sawHeadMarker = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string_view t = trim(lines[i]);
        if (t.empty()) {
            out.push_back('\n');
            continue;
        }

        if (isHeadLine(t)) {
            // (1) Skip [head] marker line
            sawHeadMarker = true;
            continue;
        }

        if (sawHeadMarker) {
            // (2) Head content line: print as-is
            // It often has a leading space in the plain output, trim() removed it.
            out += t;
            out.push_back('\n');
            sawHeadMarker = false;
            continue;
        }

        if (isPosLine(t)) {
            // (3) Part-of-speech lines: print as-is, with a blank line before it (unless already).
            if (!out.empty() && out.back() != '\n') out.push_back('\n');
            if (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] != '\n') {
                // ok
            } else if (!out.empty() && out.back() == '\n') {
                // If there isn't already an empty line, insert one.
                if (out.size() >= 2 && out[out.size() - 2] != '\n') {
                    out.push_back('\n');
                }
            }
            out += t;
            out.push_back('\n');
            continue;
        }

        if (isExampleLine(t)) {
            // (4) Indent example sentences
            out += "  ";
            out += t;
            out.push_back('\n');
            continue;
        }

        // (5) Regular translation / note line:
        // keep original indentation minimal (plain had a leading space in many lines).
        out += t;
        out.push_back('\n');
    }

    // Remove trailing blank lines.
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') {
        out.pop_back();
    }
    return out;
}

struct CliOptions
{
    bool show_plain = false;        // default: pretty
    bool write_plain_file = false;  // default: do not write <word>.plain.txt
    bool dump_index = false;        // default: do not dump full index
    bool diagnostics = false;       // default: print definition only
    bool smoke_test = false;        // default: do not run internal smoke tests
    std::string index_file = "ydict.index.txt";
    bool help = false;
    std::string_view word;          // first non-option argument
};

static void printUsage(const char* exe)
{
    std::cout
        << "Usage:\n"
        << "  " << exe << " [options] <word>\n"
        << "  " << exe << " [options] --smoke-test\n"
        << "  " << exe << " --help\n"
        << "\n"
        << "Options:\n"
        << "  --diagnostics, --verbose, -v      Print diagnostic output (init/version/full dump)\n"
        << "  --show-plain, --plain             Print raw plain text (instead of pretty)\n"
        << "  --show-pretty, --pretty           Print pretty text (default)\n"
        << "  --write-plain-file, --save-plain  Write <word>.plain.txt to disk\n"
        << "  --dump-index, --dump-idx          Write full index dump to ydict.index.txt\n"
        << "  --index-file <path>               Set index dump path (implies --dump-index)\n"
        << "  --smoke-test                       Run internal smoke tests (developer)\n"
        << "\n"
        << "Notes:\n"
        << "  - Default output is rendered from the original RTF stream (pretty, no colors).\n"
        << "  - Use --show-plain to print raw plain text instead (debug / regression checks).\n"
        << "  - By default, no files are written.\n"
        << "  - If no <word> is provided, the program prints a short hint; use -h/--help for usage.\n";
}

static CliOptions parseCli(int argc, char** argv)
{
    CliOptions opt;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];

        if (a == "--diagnostics" || a == "--verbose" || a == "-v") {
            opt.diagnostics = true;
            continue;
        }
        if (a == "--smoke-test" || a == "--smoketest") {
            opt.smoke_test = true;
            continue;
        }

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
            opt.index_file = argv[++i];
            opt.dump_index = true;
            continue;
        }
        if (a == "--help" || a == "-h" || a == "/?") {
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
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.') {
            continue;
        }
        c = '_';
    }
    if (s.empty()) {
        s = "word";
    }
    return s;
}

static void dumpMinimalDefinition(const ydict::Dictionary& dict,
                                  std::string_view word,
                                  bool showPlain,
                                  bool writePlainFile)
{
    const int idx = dict.findWord(word);
    if (idx < 0) {
        // Keep the existing not-found style (but without the full diagnostic dump).
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

    if (showPlain) {
        const std::string plain = dict.readPlainText(idx);
        std::cout << plain;
        if (!plain.empty() && plain.back() != '\n') {
            std::cout << "\n";
        }
    } else {
        const std::string rtf = dict.readRtf(idx);
        std::string pretty = ydict::renderRtfForCli(rtf);

        // Safety fallback: if RTF render yields nothing, fall back to the old plain-based formatter.
        if (pretty.empty()) {
            const std::string plain = dict.readPlainText(idx);
            pretty = formatPlainForCli(plain);
        }

        std::cout << pretty;
        if (!pretty.empty() && pretty.back() != '\n') {
            std::cout << "\n";
        }
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
    std::cout << "word=\"" << word << "\" idx=" << idx << " datOffset=" << (e ? e->dat_offset : 0) << "\n";

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

    if (cli.word.empty() && !cli.smoke_test && !cli.dump_index) {
        std::cerr << "No <word> specified. Use -h or --help for usage.\n";
        return 2;
    }

    ydict::Dictionary dict;
    ydict::Config cfg;

    cfg.idx_path = "C:/Download/ydpdict/data/dict100.idx";      // TODO: for now, hardcoded for testing
    cfg.dat_path = "C:/Download/ydpdict/data/dict100.dat";      // TODO: for now, hardcoded for testing

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Optional debug dump of the loaded idx table (handled by the library).
    if (cli.dump_index) {
        cfg.idx_dump_path = cli.index_file;
    }

    const bool ok = dict.init(cfg);

    if (cli.diagnostics || cli.smoke_test || cli.dump_index) {
        std::cout << "init() => " << (ok ? "OK" : "FAIL") << "\n";
        std::cout << dict.version() << "\n";
    }

    if (ok) {
        if (cli.dump_index) {
            const auto& st = dict.idxDumpStatus();
            if (st.requested) {
                if (st.ok) std::cout << "(saved index to " << st.path << ")\n";
                else       std::cout << "(failed to save index to " << st.path << ")\n";
            }
        }

        // On-demand full dump:
        //   ydict_app.exe get
        //   ydict_app.exe --show-plain get
        if (!cli.word.empty()) {
            if (cli.diagnostics) {
                dumpFullDefinition(dict,
                                   cli.word,
                                   /*showPlain=*/cli.show_plain,
                                   /*writePlainFile=*/cli.write_plain_file);
            } else {
                dumpMinimalDefinition(dict,
                                      cli.word,
                                      /*showPlain=*/cli.show_plain,
                                      /*writePlainFile=*/cli.write_plain_file);
            }
            return 0;
        }

        if (!cli.smoke_test) {
            return 0;
        }

        // --- smoke tests (no <word> provided) ---

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
            std::cout << "  plain(" << plain.size() << " bytes):\n";
            dumpHeadTail(plain, /*headMax=*/300, /*tailMax=*/120, /*indent=*/"  ", /*blankLineBeforeTail=*/false);
        }

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

            const int firstIdx = hits.front();
            const auto* e0 = dict.wordAt(firstIdx);
            std::cout << "  \n  selected=\"" << (e0 ? e0->word : "?") << "\"\n";
            const std::string def = dict.readPlainText(firstIdx);
            dumpHeadTail(def, /*headMax=*/220, /*tailMax=*/120, /*indent=*/"  ", /*blankLineBeforeTail=*/false);
        }
    }

    return ok ? 0 : 1;
}
