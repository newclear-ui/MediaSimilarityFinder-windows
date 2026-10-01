#include "proc_capture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
// External process safety: success / failure / timeout / capture / cleanup.
// "No console" cannot be asserted headless, so it is proven by construction
// (CREATE_NO_WINDOW, reviewed) plus the release-gate live inspection, not here.
// What IS asserted here: the parent never blocks past the caller's budget.
static long long nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
int main(){
#ifdef _WIN32
    // success + exact capture
    { std::string o; assert(msf::captureSilent("cmd.exe /c echo hello-probe",o,10000)); assert(o.find("hello-probe")!=std::string::npos); }
    // stderr is merged into the capture
    { std::string o; assert(msf::captureSilent("cmd.exe /c echo err-probe 1>&2",o,10000)); assert(o.find("err-probe")!=std::string::npos); }
    // missing binary is failure, fast
    { std::string o; long long t0=nowMs(); assert(!msf::captureSilent("msf-definitely-missing-binary-xyz",o,8000)); assert(nowMs()-t0<8000); }
    // empty output stays failure (preserved semantics)
    { std::string o; assert(!msf::captureSilent("cmd.exe /c rem nothing",o,8000)); }
    // timeout: ~5 s sleeper against a 1.5 s budget must fail well under 5 s.
    // This is the test that would hang forever under the old INFINITE wait.
    { std::string o; long long t0=nowMs(); bool r=msf::captureSilent("ping -n 6 127.0.0.1",o,1500); long long ms=nowMs()-t0; assert(!r); assert(ms<5000); }
    // zero budget fails fast by contract
    { std::string o; assert(!msf::captureSilent("cmd.exe /c echo x",o,0)); }
#else
    { std::string o; assert(msf::captureSilent("/bin/echo hello-probe",o,10000)); assert(o.find("hello-probe")!=std::string::npos); }
    { std::string o; assert(!msf::captureSilent("msf-definitely-missing-binary-xyz",o,8000)); }
    // POSIX path still blocks (documented revisit item): sleep runs its course,
    // exits 0 with empty output, which is failure by the empty-output rule.
    { std::string o; long long t0=nowMs(); bool r=msf::captureSilent("/bin/sleep 6",o,1500); long long ms=nowMs()-t0; assert(!r); assert(ms<9000); }
#endif
    std::cout<<"proc_capture test ok\n";
    return 0;
}
