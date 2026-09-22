# MediaSimilarityFinder 0.9.2.8 Portable

0.9.2.8 introduces the first resident real-time duplicate-monitor foundation.

- Multiple watched folders and comparison roots.
- Stable-file detection before analysis.
- Adaptive workload protection with CPU/memory/GPU telemetry where available.
- Lowest-priority Windows monitor thread.
- Persistent-index comparison through the existing MediaSearchEngine.
- System-tray start/stop/configuration controls in the Qt GUI.
- Match popup with open/reveal/recycle/skip actions.

This build uses a conservative polling watcher as the portable baseline. Native Windows filesystem-event watching, richer CandidateIndex reuse, and deeper foreground/game protection remain planned follow-up work.
