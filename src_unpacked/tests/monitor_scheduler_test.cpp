#include "monitor.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
using namespace msf;
int main(){
    auto dir=std::filesystem::temp_directory_path()/"msf_monitor_scheduler_test";
    std::filesystem::create_directories(dir);
    auto f=dir/"busy.jpg"; { std::ofstream o(f); o<<"a"; }
    StableFileDetector d;
    assert(!d.isStable(f.string(),1));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(d.isStable(f.string(),1));
    { std::ofstream o(f,std::ios::app); o<<"b"; }
    assert(!d.isStable(f.string(),1));
    std::filesystem::remove_all(dir);
}
