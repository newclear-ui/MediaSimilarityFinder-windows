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

// 0.9.4.88: like sampleOwnProcess but covers this process AND every child it
// spawns (e.g. nvidia-smi / ffprobe / ffmpeg via proc_capture), via a Windows
// Job Object assigned to this process. Job accounting accumulates the CPU of
// children even after they exit, which a per-process GetProcessTimes cannot
// see (they are short-lived). Call from the process that OWNS the work (the
// Backend). Never call this from the GUI process, which must not inherit the
// GUI's own children (Explorer etc. are user actions, excluded).
void sampleProcessTree(double& cpuPercent, unsigned long long& rssMB);

// 0.9.4.88: cumulative CPU ticks (kernel + user, 100 ns units) for this
// process's job tree (this process + every child it spawned), creating the
// job once. Falls back to this process alone if the job cannot be created.
// Callers keep their own previous value and derive a rate over their own
// window, so several samplers (health tick 1 s, telemetry 250 ms) can share
// the same job without corrupting each other's deltas. Returns false when
// neither source is available (non-Windows).
bool processTreeCpuTicks(unsigned long long& kernel100ns, unsigned long long& user100ns);
} // namespace msf
