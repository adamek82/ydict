#pragma once

#include <string>

namespace ydict {

struct Config {
    // Placeholder for future paths/settings
    std::string dat_path;
    std::string idx_path;
};

class Dictionary {
public:
    Dictionary() = default;

    // Mock init for now; later: open files, read index, etc.
    bool init(const Config& cfg);

    // Mock: just to validate linkage/calls
    std::string version() const;

private:
    bool initialized_ = false;
};

} // namespace ydict
