#include <iostream>
#include "ydict/ydict.h"

int main()
{
    ydict::Dictionary dict;
    ydict::Config cfg; // empty for now

    const bool ok = dict.init(cfg);
    std::cout << "init() => " << (ok ? "OK" : "FAIL") << "\n";
    std::cout << dict.version() << "\n";

    return ok ? 0 : 1;
}
