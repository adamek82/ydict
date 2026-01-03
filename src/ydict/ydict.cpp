#include "ydict/ydict.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ydict {

/*
 * Byte->UTF-8 mapping for the dictionary's "phonetic" font stream.
 *
 * In ydpdict RTF, phonetic transcription is emitted using font #1 (\f1).
 * In that mode, bytes in the 0x80..0x9F range are *not* CP1250 letters — they
 * are custom glyph slots used for IPA-like symbols. We translate those 32
 * slots to their intended Unicode characters so the output becomes valid UTF-8.
 *
 * Unknown / unused / not-yet-reverse-engineered slots are left as "?" so we
 * don’t silently emit wrong phonetics; it makes missing mappings obvious
 * during testing.
 */
static const char* kPhoneticToUtf8[32] = {
    "?", "?", "ɔ", "ʒ", "?", "ʃ", "ɛ", "ʌ",
    "ə", "θ", "ɪ", "ɑ", "?", "ː", "ˈ", "?",
    "ŋ", "?", "?", "?", "?", "?", "?", "ð",
    "æ", "?", "?", "?", "?", "?", "?", "?"
};

static bool dump_idx_to_file(const std::string& dumpPath, const std::vector<WordEntry>& words)
{
    std::ofstream out(dumpPath, std::ios::binary);
    if (!out)
        return false;

    /*
     * Simple line-based dump, easy to grep/diff/analyze:
     * i<TAB>datOffset<TAB>word<NL>
     */
    for (size_t i = 0; i < words.size(); ++i) {
        out << i << '\t' << words[i].dat_offset << '\t' << words[i].word << '\n';
    }

    return true;
}

static std::uint16_t read_u16_le(std::istream& in)
{
    unsigned char b[2]{};
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<std::uint16_t>(b[0] | (std::uint16_t(b[1]) << 8));
}

static std::uint32_t read_u32_le(std::istream& in)
{
    unsigned char b[4]{};
    in.read(reinterpret_cast<char*>(b), 4);
    return (std::uint32_t(b[0])      ) |
           (std::uint32_t(b[1]) <<  8) |
           (std::uint32_t(b[2]) << 16) |
           (std::uint32_t(b[3]) << 24);
}

static std::string read_cstr(std::istream& in)
{
    std::string s;
    for (;;) {
        const int c = in.get();
        if (c == EOF || c == 0) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

bool Dictionary::init(const Config& cfg)
{
    initialized_ = false;
    words_.clear();
    idx_dump_status_ = IdxDumpStatus{};

    // (status will be finalized after we load the index)
    dat_path_.clear();

    if (cfg.idx_path.empty())
        return false;

    if (cfg.dat_path.empty())
        return false;

    dat_path_ = cfg.dat_path;

    // quick sanity check: can we open .dat at all?
    std::ifstream dat(dat_path_, std::ios::binary);
    if (!dat)
        return false;

    std::ifstream idx(cfg.idx_path, std::ios::binary);
    if (!idx)
        return false;

    constexpr std::uint32_t kIdxMagic = 0x8d4e11d5;

    const std::uint32_t magic = read_u32_le(idx);
    if (!idx || magic != kIdxMagic)
        return false;

    idx.seekg(8, std::ios::beg);
    const std::uint16_t count = read_u16_le(idx);
    if (!idx)
        return false;

    idx.seekg(16, std::ios::beg);
    const std::uint32_t tableOffset = read_u32_le(idx);
    if (!idx)
        return false;

    idx.seekg(tableOffset, std::ios::beg);
    if (!idx)
        return false;

    words_.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        idx.seekg(4, std::ios::cur); // skip unknown 4 bytes
        const std::uint32_t datOffset = read_u32_le(idx);
        const std::string word = read_cstr(idx);

        if (!idx)
            return false;

        words_.push_back(WordEntry{word, datOffset});
    }

    // Optional debug artifact (disabled by default).
    // Useful for analyzing collation/sorting/prefix-search issues without doing it unconditionally.
    if (!cfg.idx_dump_path.empty()) {
        idx_dump_status_.requested = true;
        idx_dump_status_.path = cfg.idx_dump_path;
        idx_dump_status_.ok = dump_idx_to_file(cfg.idx_dump_path, words_);
    }

    initialized_ = true;
    return true;
}

std::string Dictionary::version() const
{
    if (!initialized_)
        return "ydict - not initialized";
    return "ydict - idx loaded (" + std::to_string(words_.size()) + " words)";
}

std::string Dictionary::readRtf(int defIndex) const
{
    if (!initialized_ || dat_path_.empty())
        return {};

    if (defIndex < 0 || defIndex >= static_cast<int>(words_.size()))
        return {};

    std::ifstream dat(dat_path_, std::ios::binary);
    if (!dat)
        return {};

    // file size
    dat.seekg(0, std::ios::end);
    const std::streamoff fileSize = dat.tellg();
    if (fileSize <= 0)
        return {};

    const std::uint32_t offset = words_[defIndex].dat_offset;

    // need at least 4 bytes for length
    if (static_cast<std::streamoff>(offset) + 4 > fileSize)
        return {};

    dat.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!dat)
        return {};

    const std::uint32_t len = read_u32_le(dat);
    if (!dat)
        return {};

    // sanity limit (RTF definitions should be reasonably small)
    constexpr std::uint32_t kMaxDefSize = 4u * 1024u * 1024u; // 4 MiB
    if (len == 0 || len > kMaxDefSize)
        return {};

    if (static_cast<std::streamoff>(offset) + 4 + static_cast<std::streamoff>(len) > fileSize)
        return {};

    std::string rtf;
    rtf.resize(len);

    dat.read(rtf.data(), static_cast<std::streamsize>(len));
    if (dat.gcount() != static_cast<std::streamsize>(len))
        return {};

    return rtf;
}

static void append_cp1250_byte_as_utf8(std::string& out, unsigned char b)
{
    if (b == 0x7F) { // upstream mapped this to "~"
        out.push_back('~');
        return;
    }
    if (b < 0x80) {
        out.push_back(static_cast<char>(b));
        return;
    }

#ifdef _WIN32
    wchar_t wbuf[2] = {};
    const char in = static_cast<char>(b);

    const int wlen = MultiByteToWideChar(1250 /*CP1250*/, MB_ERR_INVALID_CHARS, &in, 1, wbuf, 2);
    if (wlen <= 0) {
        out.push_back('?');
        return;
    }

    char ubuf[8] = {};
    const int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, ubuf, int(sizeof(ubuf)), nullptr, nullptr);
    if (ulen <= 0) {
        out.push_back('?');
        return;
    }
    out.append(ubuf, ubuf + ulen);
#else
    // fallback (if you ever compile this outside Windows): without a table it's better than crashing
    out.push_back('?');
#endif
}

static void append_byte_as_utf8(std::string& out, unsigned char b, bool phoneticMode)
{
    if (phoneticMode && b >= 128 && b < 160) {
        out += kPhoneticToUtf8[b - 128];
        return;
    }
    append_cp1250_byte_as_utf8(out, b);
}

/* --- helpers used by BOTH RTF->CLI and RTF->plain --- */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/*
 * Minimal RTF -> CLI renderer (no colors)
 * --------------------------------------
 * We parse a small subset of the RTF-like stream used by ydpdict:
 *   - groups: '{' pushes state, '}' pops state
 *   - '\par'/'\line' => newline; '\pard' => paragraph reset (no newline)
 *   - '\saN' => indentation at beginning of line
 *   - '\cfN' => style bucket; we treat cf==2 as "phrase bullet" (prefix "- ")
 *   - '\f1' => phonetic font stream (0x80..0x9F map via kPhoneticToUtf8)
 *   - '\qc' => hidden blocks (ydpdict convention)
 *   - "\'hh" and '\uN' => proper decoding to UTF-8
 *
 * The point is to keep semantic cues that are lost in plain text conversion,
 * so CLI output matches ydpdict more closely without brittle heuristics.
 */
struct RtfCliState
{
    int  cf = 0;           // \cfN
    bool phonetic = false; // \f1
    bool hide = false;     // \qc
    bool margin = false;   // \saN
};

static bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

std::string renderRtfForCli(std::string_view rtf)
{
    std::vector<RtfCliState> st;
    st.push_back(RtfCliState{});

    std::string out;
    out.reserve(rtf.size());

    bool bol = true; // beginning-of-line
    // Compress newlines: ydpdict output is compact (no empty lines).
    // `nl_run==1` means the last emitted char was '\n'.
    int nl_run = 0;

    auto beginLineIfNeeded = [&]() {
        if (!bol)
            return;

        bol = false;
        const RtfCliState& s = st.back();

        if (s.margin) {
            out += "  "; // minimal indent (tunable)
        }

        // IMPORTANT: bullet is driven by RTF style (\cf2), not by text heuristics.
        if (s.cf == 2) {
            out += "- ";
        }
        nl_run = 0;
    };

    auto newline = [&]() {
        // Avoid leading blank lines and avoid empty lines in general.
        if (out.empty()) {
            bol = true;
            return;
        }
        if (nl_run >= 1) {
            bol = true;
            return;
        }
        out.push_back('\n');
        bol = true;
        nl_run = 1;
    };

    for (size_t i = 0; i < rtf.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(rtf[i]);

        if (ch == '{') {
            st.push_back(st.back());
            continue;
        }
        if (ch == '}') {
            if (st.size() > 1)
                st.pop_back();
            continue;
        }

        if (ch != '\\') {
            if (st.back().hide)
                continue;

            // Trim noisy leading whitespace at BOL; indentation comes from \saN.
            if (bol && (ch == ' ' || ch == '\t' || ch == '\r'))
                continue;

            if (ch == '\n') { newline(); continue; }
            if (ch == '\r') { continue; }

            beginLineIfNeeded();
            append_byte_as_utf8(out, ch, st.back().phonetic);
            nl_run = 0;
            continue;
        }

        // Control sequence
        if (i + 1 >= rtf.size())
            break;

        // Escaped literal: \\ \{ \}
        const char next = rtf[i + 1];
        if (next == '\\' || next == '{' || next == '}') {
            i += 1;
            if (!st.back().hide) {
                const unsigned char lit = static_cast<unsigned char>(next);
                if (bol && (lit == ' ' || lit == '\t' || lit == '\r'))
                    continue;
                beginLineIfNeeded();
                append_byte_as_utf8(out, lit, st.back().phonetic);
                nl_run = 0;
            }
            continue;
        }

        // Hex escape: \'hh
        if (next == '\'' && i + 3 < rtf.size()) {
            const int h1 = hexval(rtf[i + 2]);
            const int h2 = hexval(rtf[i + 3]);
            if (h1 >= 0 && h2 >= 0) {
                const unsigned char b = static_cast<unsigned char>((h1 << 4) | h2);
                i += 3;
                if (!st.back().hide) {
                    if (bol && (b == ' ' || b == '\t' || b == '\r'))
                        continue;
                    beginLineIfNeeded();
                    append_byte_as_utf8(out, b, st.back().phonetic);
                    nl_run = 0;
                }
                continue;
            }
        }

        // Parse control word: \word[+/-num]?
        size_t j = i + 1;
        std::string tok;
        while (j < rtf.size() && is_alpha(rtf[j])) {
            tok.push_back(rtf[j]);
            ++j;
        }

        bool hasParam = false;
        int sign = 1;
        int param = 0;
        if (j < rtf.size() && (rtf[j] == '-' || is_digit(rtf[j]))) {
            hasParam = true;
            if (rtf[j] == '-') { sign = -1; ++j; }
            while (j < rtf.size() && is_digit(rtf[j])) {
                param = param * 10 + (rtf[j] - '0');
                ++j;
            }
            param *= sign;
        }

        // Optional delimiter space after control word
        if (j < rtf.size() && rtf[j] == ' ')
            i = j;
        else
            i = j - 1;

        if (tok == "par" || tok == "line") {
            if (!st.back().hide) newline();
            continue;
        }
        if (tok == "pard") {
            // Paragraph defaults/reset; do NOT create a new line (RTF often does \pard\par).
            st.back().cf = 0;
            st.back().margin = false;
            // phonetic/hide are kept as they are (usually scoped by groups anyway).
            bol = true;
            continue;
        }
        if (tok == "tab") {
            if (!st.back().hide) {
                beginLineIfNeeded();
                out.push_back('\t');
                nl_run = 0;
            }
            continue;
        }
        if (tok == "cf" && hasParam) { st.back().cf = param; continue; }
        if (tok == "sa" && hasParam) { st.back().margin = (param != 0); continue; }
        if (tok == "f"  && hasParam) { st.back().phonetic = (param == 1); continue; }
        if (tok == "qc") { st.back().hide = true; continue; }

        // Minimal RTF \uN support
        if (tok == "u" && hasParam && !st.back().hide) {
#ifdef _WIN32
            wchar_t wc = static_cast<wchar_t>(param);
            char ubuf[8] = {};
            const int ulen = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, ubuf, int(sizeof(ubuf)), nullptr, nullptr);
            beginLineIfNeeded();
            if (ulen > 0) out.append(ubuf, ubuf + ulen);
            else out.push_back('?');
            nl_run = 0;
#else
            beginLineIfNeeded();
            out.push_back('?');
            nl_run = 0;
#endif
            // RTF expects a fallback char right after \uN — skip one if present
            if (i + 1 < rtf.size())
                ++i;
            continue;
        }

        // Everything else ignored for now (bold/italics/etc.).
    }

    return out;
}

static std::string rtf_to_plain_utf8(const std::string& rtf)
{
    std::string out;
    out.reserve(rtf.size());

    bool phonetic = false;

    for (size_t i = 0; i < rtf.size(); ) {
        const unsigned char ch = static_cast<unsigned char>(rtf[i]);

        if (ch == '{' || ch == '}') {
            ++i;
            continue;
        }

        if (ch != '\\') {
            append_byte_as_utf8(out, ch, phonetic);
            ++i;
            continue;
        }

        // control sequence
        ++i;
        if (i >= rtf.size())
            break;

        // hex escape: \'hh
        if (rtf[i] == '\'' && i + 2 < rtf.size()) {
            const int h1 = hexval(rtf[i + 1]);
            const int h2 = hexval(rtf[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                const unsigned char b = static_cast<unsigned char>((h1 << 4) | h2);
                append_byte_as_utf8(out, b, phonetic);
                i += 3;
                continue;
            }
        }

        // parse control word: letters
        std::string tok;
        while (i < rtf.size()) {
            const char c = rtf[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                break;
            tok.push_back(c);
            ++i;
        }

        // optional numeric parameter (e.g. sa100, li300, u8211)
        bool hasParam = false;
        int param = 0;
        int sign = 1;

        if (i < rtf.size() && (rtf[i] == '-' || (rtf[i] >= '0' && rtf[i] <= '9'))) {
            hasParam = true;
            if (rtf[i] == '-') { sign = -1; ++i; }
            while (i < rtf.size() && (rtf[i] >= '0' && rtf[i] <= '9')) {
                param = param * 10 + (rtf[i] - '0');
                ++i;
            }
            param *= sign;
        }

        // skip one optional space delimiter
        if (i < rtf.size() && rtf[i] == ' ')
            ++i;

        // minimal semantics
        if (tok == "par" || tok == "line") {
            out.push_back('\n');
            continue;
        }
        if (tok == "tab") {
            out.push_back('\t');
            continue;
        }

        /*
         * font switching:
         * In upstream ydpdict, phonetic transcription is emitted under font #1 (\f1).
         * Our parser reads control word letters into `tok` ("f") and the numeric part
         * into `param` (1). So we must check (tok=="f" && param==1), not tok=="f1".
         */
        if (tok == "f" && hasParam) {
            phonetic = (param == 1);
            continue;
        }

        // minimal RTF \uN support (optional; harmless)
        if (tok == "u" && hasParam) {
#ifdef _WIN32
            wchar_t wc = static_cast<wchar_t>(param);
            char ubuf[8] = {};
            const int ulen = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, ubuf, int(sizeof(ubuf)), nullptr, nullptr);
            if (ulen > 0) out.append(ubuf, ubuf + ulen);
            else out.push_back('?');
#else
            out.push_back('?');
#endif
            // RTF expects a fallback char right after \uN — skip one if present
            if (i < rtf.size())
                ++i;
            continue;
        }

        // everything else ignored
    }

    return out;
}

std::string Dictionary::readPlainText(int defIndex) const
{
    const std::string rtf = readRtf(defIndex);
    if (rtf.empty())
        return {};
    return rtf_to_plain_utf8(rtf);
}

int Dictionary::findWord(std::string_view word) const
{
    if (!initialized_ || word.empty())
        return -1;

    // Fast path (assumes .idx word table is sorted in a way compatible with std::string ordering)
    const auto it = std::lower_bound(
        words_.begin(), words_.end(), word,
        [](const WordEntry& e, std::string_view w) { return e.word < w; });

    if (it != words_.end() && it->word == word)
        return static_cast<int>(it - words_.begin());

    // Fallback (robust against collation differences): linear scan
    for (size_t i = 0; i < words_.size(); ++i) {
        if (words_[i].word == word)
            return static_cast<int>(i);
    }

    return -1;
}

std::string Dictionary::readPlainText(std::string_view word) const
{
    const int idx = findWord(word);
    if (idx < 0)
        return {};
    return readPlainText(idx);
}

int Dictionary::lowerBound(std::string_view key) const
{
    if (!initialized_)
        return -1;

    const auto it = std::lower_bound(
        words_.begin(), words_.end(), key,
        [](const WordEntry& e, std::string_view k) { return e.word < k; });

    return static_cast<int>(it - words_.begin()); // may be == wordCount()
}

static bool starts_with_sv(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() &&
           s.substr(0, prefix.size()) == prefix;
}

static unsigned char ascii_tolower(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<unsigned char>(c - 'A' + 'a');
    return c;
}

static bool starts_with_ascii_icase(std::string_view s, std::string_view prefix)
{
    if (s.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char a = ascii_tolower(static_cast<unsigned char>(s[i]));
        const unsigned char b = ascii_tolower(static_cast<unsigned char>(prefix[i]));
        if (a != b)
            return false;
    }
    return true;
}

int Dictionary::findFirstWithPrefix(std::string_view prefix) const
{
    if (!initialized_ || prefix.empty())
        return -1;

    const int pos = lowerBound(prefix);
    if (pos < 0 || pos >= static_cast<int>(words_.size()))
        return -1;

    return starts_with_sv(words_[pos].word, prefix) ? pos : -1;
}

std::vector<int> Dictionary::suggest(std::string_view prefix, size_t maxResults) const
{
    std::vector<int> out;
    if (!initialized_ || prefix.empty() || maxResults == 0)
        return out;

    // Optional: treat "to <verb>" as just "<verb>" (many dictionaries omit "to").
    if (prefix.size() >= 3 &&
        (prefix[0] == 't' || prefix[0] == 'T') &&
        (prefix[1] == 'o' || prefix[1] == 'O') &&
        prefix[2] == ' ') {
        prefix.remove_prefix(3);
        if (prefix.empty())
            return out;
    }

    /*
     * Find the first word >= prefix
     * NOTE:
     * .idx ordering is NOT guaranteed to match std::string ordering used by std::lower_bound
     * (see dump: e.g. "accessory" comes before "access road", etc.).
     * So do a simple linear scan (26k words => fast enough) and keep original order.
     */
    for (size_t i = 0; i < words_.size() && out.size() < maxResults; ++i) {
        if (starts_with_ascii_icase(words_[i].word, prefix))
            out.push_back(static_cast<int>(i));
    }

    return out;
}

} // namespace ydict
