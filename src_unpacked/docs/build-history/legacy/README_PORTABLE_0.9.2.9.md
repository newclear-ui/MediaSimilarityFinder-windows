# MediaSimilarityFinder 0.9.2.9 Portable

0.9.2.9 changes the resident real-time monitor from polling-first detection to Windows `ReadDirectoryChangesW` event-driven detection, while retaining polling as a non-Windows fallback.

The monitor also keeps comparison indexes open for the lifetime of the monitor session, coalesces duplicate filesystem events, waits for stable files, and prioritizes foreground workload protection.
