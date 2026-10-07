// Own-process resource sampling (P4: 0.9.4.71). The summary panel must show
// the process doing the work: in production that is the Backend process, in
// loopback tests the GUI process itself. Both sample through this helper so
// the metric identity (process CPU%, process Working Set) is identical.
// Qt-free, msf_core.
#pragma once

namespace msf {
// Samples this process: cpuPercent (0-100 of all cores, like Task Manager's
// per-process reading normalized consistently) and working-set MB. First
// call primes the CPU baseline and reports 0.0 CPU. Thread-affine: call from
// one thread only per process (backend ticker / GUI tick respectively).
void sampleOwnProcess(double& cpuPercent, unsigned long long& rssMB);
} // namespace msf
