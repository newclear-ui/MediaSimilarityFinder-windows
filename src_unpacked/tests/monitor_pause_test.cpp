#include "monitor.h"
#include <cassert>
#include <iostream>

int main(){
    msf::MediaMonitor m;
    assert(!m.running());
    assert(!m.paused());
    m.setPaused(true);
    assert(m.paused());
    auto s=m.status();
    assert(s.paused);
    m.setPaused(false);
    assert(!m.paused());
    assert(!m.status().paused);
    std::cout << "monitor pause API PASS\n";
    return 0;
}
