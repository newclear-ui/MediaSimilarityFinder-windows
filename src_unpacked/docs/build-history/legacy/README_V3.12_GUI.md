# Media Similarity Finder v3.12.0 — GUI bundle

This release bundles the v3.8–v3.12 GUI work:
- Explorer-like group tree and thumbnail/list view
- image thumbnails through Qt image readers and native file icons for unsupported media
- similar-group display with similarity percentages
- double-click/open and reveal in Explorer
- rename/delete/copy/cut/paste shortcuts and context menu
- scan progress, pause/resume/cancel
- Maximum/Balanced/Gaming/Custom CPU/GPU controls
- adjustable Hamming tolerance
- persistent `.msf/index.sqlite`

The current core still computes compact 64-bit fingerprints. Advanced perceptual embeddings and NVDEC remain later GPU-engine work.
