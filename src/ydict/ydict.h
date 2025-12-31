#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ydict {

struct Config {
    std::string dat_path;
    std::string idx_path;
};

struct WordEntry {
    std::string word;
    std::uint32_t dat_offset = 0;
};

class Dictionary {
public:
    bool init(const Config& cfg);

    std::string version() const;

    int wordCount() const { return static_cast<int>(words_.size()); }
    const WordEntry* wordAt(int i) const {
        return (i >= 0 && i < static_cast<int>(words_.size())) ? &words_[i] : nullptr;
    }

    // read raw RTF definition from .dat for given entry index
    std::string readRtf(int defIndex) const;

    // read "plain text" converted to UTF-8 (minimal RTF parsing)
    std::string readPlainText(int defIndex) const;

    // lookup by word (returns entry index or -1)
    int findWord(std::string_view word) const;

    // convenience: lookup + read plain text
    std::string readPlainText(std::string_view word) const;

private:
    bool initialized_ = false;
    std::string dat_path_;
    std::vector<WordEntry> words_;
};

} // namespace ydict
