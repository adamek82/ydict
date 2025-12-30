#include "ydict/ydict.h"

#include <fstream>
#include <stdexcept>

namespace ydict {

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

} // namespace ydict
