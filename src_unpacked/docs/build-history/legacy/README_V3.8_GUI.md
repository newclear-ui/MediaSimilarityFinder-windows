# Media Similarity Finder v3.8 — GUI Integration

v3.8 adds the first Qt6 Widgets front-end around the v3.7 core.

## GUI
- Folder selection
- Scan / Pause / Resume / Cancel
- Progress and current file display
- Resource presets: Maximum / Balanced / Gaming / Custom
- CPU/GPU budget controls
- Similar-group navigation pane and file result table foundation
- Context menu: Open / Show in Explorer
- SQLite index stored as `.msf/index.sqlite` under the selected root

## Core changes
- `MediaSearchEngine::ScanControl` supports cooperative pause/cancel and progress callbacks.
- Existing v3.7 SQLite/incremental/FFmpeg pipeline remains the backend.

## Build
Qt6 Widgets is optional. If Qt6 is not installed, CMake builds the core and tests and skips the GUI target.

The current Linux validation environment has no Qt6 and no CUDA toolkit, so the GUI and CUDA targets were not runtime-tested here. Windows + Qt6 + NVIDIA CUDA validation is required on the target PC.
