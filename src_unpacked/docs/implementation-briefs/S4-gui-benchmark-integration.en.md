# S4 GUI Benchmark Integration — Historical / Superseded

Status: SUPERSEDED 2026-10-03

This document preserves the previous standalone GUI benchmark execution design as historical provenance. It is not the current design baseline.

Current baseline:
- docs/architecture/gui-diagnostic-logging.ko.md / .en.md
- docs/implementation-briefs/S4-gui-diagnostic-logging.ko.md / .en.md

Superseded concepts:
- GUI Benchmark Run / Stop / Pause
- GUI의 AUTO / CPU / GPU-max 다중 benchmark 선택
- GUI에서 BenchmarkRunner를 별도 benchmark로 실행
- Benchmark/GUI 세 mode snapshot을 benchmark history로 유지
- benchTgl_를 benchmark 사용자 명칭으로 유지

Current design:
- GUI = 사용자 우선 Search/Update + [상세 로그]
- AUTO / CPU / GPU-max = GUI 사용자 실행 자원 전략, 하나 선택
- Maximum / High / Balanced / Gaming / Manual = 별도 CPU Resource Policy
- CLI --benchmark = 개발용 controlled benchmark
- CLI AUTO / CPU / GPU-max = 비교 실험군
- 공통 계측 = TelemetryRecorder

Current implementation instructions must use the new S4 documents only. This file is retained for provenance.
