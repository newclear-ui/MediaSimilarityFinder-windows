#include "monitor.h"
#include <cassert>
int main(){
    msf::ResourcePolicy gaming=msf::make_policy(msf::ResourceMode::Gaming);
    msf::SystemLoad idle{}; idle.state=msf::LoadState::Idle; assert(msf::SystemLoadMonitor::classify(idle)==msf::LoadState::Idle); assert(msf::SystemLoadMonitor{}.allowAnalysis(gaming,idle));
    msf::SystemLoad light{}; light.cpuPercent=30; light.state=msf::SystemLoadMonitor::classify(light); assert(light.state==msf::LoadState::Light); assert(!msf::SystemLoadMonitor{}.allowAnalysis(gaming,light));
    msf::SystemLoad busy{}; busy.cpuPercent=60; busy.state=msf::SystemLoadMonitor::classify(busy); assert(busy.state==msf::LoadState::Busy); assert(!msf::SystemLoadMonitor{}.allowAnalysis(gaming,busy));
    msf::SystemLoad critical{}; critical.cpuPercent=93; critical.state=msf::SystemLoadMonitor::classify(critical); assert(critical.state==msf::LoadState::Critical); assert(!msf::SystemLoadMonitor{}.allowAnalysis(gaming,critical));
    msf::ResourcePolicy balanced=msf::make_policy(msf::ResourceMode::Balanced); msf::SystemLoad light2{}; light2.cpuPercent=20; light2.state=msf::SystemLoadMonitor::classify(light2); assert(msf::SystemLoadMonitor{}.allowAnalysis(balanced,light2));
    return 0;
}
