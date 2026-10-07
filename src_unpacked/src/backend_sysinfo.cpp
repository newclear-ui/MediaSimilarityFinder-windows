// See backend_sysinfo.h.
#include "backend_sysinfo.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
#include <chrono>

namespace msf {
void sampleOwnProcess(double& cpuPercent, unsigned long long& rssMB) {
    cpuPercent = 0.0;
    rssMB = 0;
#ifdef _WIN32
    static ULONGLONG prevK = 0, prevU = 0;
    static long long prevMs = 0;
    static int cpuCount = 0;
    if (cpuCount <= 0) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        cpuCount = (int)si.dwNumberOfProcessors;
        if (cpuCount <= 0) cpuCount = 1;
    }
    FILETIME fc, fe, fk, fu;
    if (GetProcessTimes(GetCurrentProcess(), &fc, &fe, &fk, &fu)) {
        ULARGE_INTEGER k{}, u{};
        k.LowPart = fk.dwLowDateTime;
        k.HighPart = fk.dwHighDateTime;
        u.LowPart = fu.dwLowDateTime;
        u.HighPart = fu.dwHighDateTime;
        const long long now =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (prevMs > 0 && now > prevMs) {
            const double cpuMs =
                (double)(long long)(k.QuadPart - prevK) + (double)(long long)(u.QuadPart - prevU);
            double pct = cpuMs / 10000.0 * 100.0 / ((double)(now - prevMs) * cpuCount);
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;
            cpuPercent = pct;
        }
        prevK = k.QuadPart;
        prevU = u.QuadPart;
        prevMs = now;
    }
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        rssMB = (unsigned long long)pmc.WorkingSetSize / (1024ULL * 1024ULL);
#endif
}
} // namespace msf
