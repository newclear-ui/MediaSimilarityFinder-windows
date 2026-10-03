# S4 GUI Benchmark Integration — Historical / Superseded

Status: SUPERSEDED 2026-10-03

이 문서는 이전 독립 GUI benchmark 실행 설계를 보존하는 역사적 기록이다. 현재 기준 문서가 아니다.

현재 기준:
- docs/architecture/gui-diagnostic-logging.ko.md / .en.md
- docs/implementation-briefs/S4-gui-diagnostic-logging.ko.md / .en.md

폐기된 이전 개념:
- GUI Benchmark Run / Stop / Pause
- GUI의 AUTO / CPU / GPU-max 다중 benchmark 선택
- GUI에서 BenchmarkRunner를 별도 benchmark로 실행
- Benchmark/GUI 세 mode snapshot을 benchmark history로 유지
- benchTgl_를 benchmark 사용자 명칭으로 유지

현재 설계:
- GUI = 사용자 우선 Search/Update + [상세 로그]
- AUTO / CPU / GPU-max = GUI 사용자 실행 자원 전략, 하나 선택
- Maximum / High / Balanced / Gaming / Manual = 별도 CPU Resource Policy
- CLI --benchmark = 개발용 controlled benchmark
- CLI AUTO / CPU / GPU-max = 비교 실험군
- 공통 계측 = TelemetryRecorder

현재 구현 지시는 새 S4 문서만 기준으로 한다. 이 파일은 provenance 보존을 위해 삭제하지 않는다.
