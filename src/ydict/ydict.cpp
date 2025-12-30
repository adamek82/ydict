#include "ydict/ydict.h"

namespace ydict {

bool Dictionary::init(const Config& /*cfg*/)
{
    // Mock: pretend everything is OK
    initialized_ = true;
    return true;
}

std::string Dictionary::version() const
{
    return initialized_ ? "ydict (mock) - initialized" : "ydict (mock) - not initialized";
}

} // namespace ydict
