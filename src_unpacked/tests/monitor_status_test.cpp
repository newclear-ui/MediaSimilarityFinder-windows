#include "monitor.h"
#include <cassert>
#include <iostream>
int main(){
    msf::MediaMonitor m;
    auto s=m.status();
    assert(!s.running);
    assert(s.pending==0);
    assert(s.deferred==0);
    assert(s.lastErrorPath.empty());
    assert(s.lastError.empty());
    std::cout << "monitor status API PASS\n";
    return 0;
}
