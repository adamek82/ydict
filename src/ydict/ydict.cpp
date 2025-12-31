#include "ydict/ydict.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ydict {

static const char* kPhoneticToUtf8[32] = {
    "?", "?", "ɔ", "ʒ", "?", "ʃ", "ɛ", "ʌ",
    "ə", "θ", "ɪ", "ɑ", "?", "ː", "ˈ", "?",
    "ŋ", "?", "?", "?", "?", "?", "?", "ð",
    "æ", "?", "?", "?", "?", "?", "?", "?"
};

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

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
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

        // font switching:
        // In upstream ydpdict, phonetic transcription is emitted under font #1 (\f1).
        // Our parser reads control word letters into `tok` ("f") and the numeric part
        // into `param` (1). So we must check (tok=="f" && param==1), not tok=="f1".
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

} // namespace ydict
