# Real-Time Media Duplicate Monitor Architecture

## Current implementation — 0.9.2.9

The foundation was introduced in 0.9.2.8. In 0.9.2.9, Windows watching was changed to an event-driven `ReadDirectoryChangesW` design.

```text
Windows File System Event
        ↓
Event Coalescing Queue
        ↓
Stable-file Detector
        ↓
System Load Protection
        ↓
Media Pipeline / Fingerprint
        ↓
Resident Comparison Indexes
        ↓
Similarity Engine
        ↓
Comparison Popup
```

### Core principle

**Protecting the user's active workload takes priority over real-time analysis speed.**

- Windows uses one `ReadDirectoryChangesW` watcher per configured root.
- Repeated notifications for the same file are coalesced into one pending item.
- Analysis waits until file size/write time has stabilized.
- Analysis is deferred when CPU/memory/GPU load is high.
- Comparison indexes are opened at monitor startup and kept resident for the session.
- Non-Windows builds retain polling as a fallback.

### Next design tasks

- Full-root resynchronization after event-buffer overflow
- Safe runtime addition/removal of watch roots
- More granular queue priority and adaptive backoff under load
- Immediate synchronization of index changes during a monitor session
- Stronger protection policies for games and foreground workloads


## 0.9.2.11 Deferred Scheduler

Monitor work that is deferred because a file is unstable or the system is busy is not immediately requeued. The worker waits on a condition variable and deferred items use exponential backoff, capped at 30 seconds. This minimizes repeated wake-ups and retries during downloads, gaming, or other foreground-heavy workloads.

## 0.9.2.13 Resident Comparison Index and Event Recovery

From 0.9.2.13, resident comparisons use separate image/video CandidateIndex caches instead of scanning the full SQLite table. Threshold-derived Hamming distance prunes candidates before the existing exact similarity calculation. Each comparison engine has explicit root ownership, so live synchronization updates only the index that owns a path. If `ReadDirectoryChangesW` reports an overflow/zero-byte result, the affected root is recursively resynchronized, while fingerprinting remains gated by stable-file detection and system-load protection.


## 0.9.2.14 Status Observability
`MediaMonitor::status()` exposes load state and queue/deferred/analyzed/matches/errors counters. The GUI displays these values every second so users can see when analysis is delayed to protect foreground workloads.

## 0.9.2.15 Manual Pause Control

Added `MediaMonitor::setPaused()` so the user can immediately stop real-time analysis. Already detected files remain queued while analysis is paused. Automatic workload protection and manual pause operate independently.


## 0.9.2.17 Monitor Settings GUI

A dedicated settings dialog manages watched roots and comparison roots as separate lists. Monitor-specific threshold, stable-file delay, fallback polling interval, and GPU permission are persisted independently from the normal search controls.

### Windows common compatibility alignment (0.9.2.34)
Windows API/header/path/file-watcher compatibility fixes found during CPU testing are applied to the shared layer independently of the CUDA backend. The CUDA backend processing algorithms are unchanged.
