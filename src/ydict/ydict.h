#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ydict {

struct Config {
    std::string dat_path;
    std::string idx_path;

    /*
     * Optional debug: write the loaded .idx word table to a text file.
     * If empty (default), no dump is produced.
     *
     * Format: idx<TAB>datOffset<TAB>word<NL>
     */
    std::string idx_dump_path;
};

struct WordEntry {
    std::string word;
    std::uint32_t dat_offset = 0;
};

struct IdxDumpStatus
{
    bool requested = false;
    bool ok = false;          // meaningful only if requested==true
    std::string path;         // meaningful only if requested==true
};

class Dictionary {
public:
    bool init(const Config& cfg);
    std::string version() const;

    int wordCount() const;
    const WordEntry* wordAt(int index) const;

    // Read raw RTF-like stream from .dat for the given entry index.
    std::string readRtf(int defIndex) const;

    // Read plain UTF-8 text, produced by a minimal RTF-to-plain converter.
    std::string readPlainText(int defIndex) const;
    std::string readPlainText(std::string_view word) const;

    // Find exact word in the loaded index. Returns -1 if not found.
    int findWord(std::string_view word) const;

    // For prefix search/suggestions (left-pane behavior in ydpdict).
    int lowerBound(std::string_view key) const;
    int findFirstWithPrefix(std::string_view prefix) const;
    std::vector<int> suggest(std::string_view prefix, size_t maxResults = 15) const;

    // Debug/CLI diagnostics: tells whether idx dump was requested and whether it succeeded.
    const IdxDumpStatus& idxDumpStatus() const { return idx_dump_status_; }

private:
    bool initialized_ = false;
    std::string dat_path_;
    std::vector<WordEntry> words_;
    IdxDumpStatus idx_dump_status_;
};

/*
 * RTF -> CLI renderer
 * ------------------
 * The dictionary definitions are stored as a compact RTF-like stream.
 * This helper renders that stream to UTF-8 text suitable for console output
 * (no colors), preserving key semantic cues (indentation, phonetics mapping,
 * hidden blocks) while keeping line breaks close to ydpdict.
 */
std::string renderRtfForCli(std::string_view rtf);

} // namespace ydict
