# S4 Phase 3-4 회귀 / 판정 기록 (2026-09-30)

기준 커밋: `4a12fb3` (이 Phase 는 커밋하지 않음)
버전: `0.9.4.43` (`CMakeLists.txt` `project(... VERSION 0.9.4.43)` 단일 source)

---

## 1. 구현된 파일 (Phase 3-1 ~ 3-3)

| Phase | 파일 | 내용 |
| --- | --- | --- |
| 3-1 | `src/benchmark_gui_store.{h,cpp}` | GUI suite 경로, S3 suite lock 재사용, mode 별 snapshot, atomic replace |
| 3-2 | `gui/benchmark_worker.{h,cpp}` | `BenchmarkRunner::run(request, selectedModes)` 단일 호출 worker |
| 3-3 | `gui/mainwindow.{h,cpp}` | mode checkbox 3개, 실행 버튼, 중지 버튼, 상태 라벨, 직렬 실행 게이트 |
| 3-1~3-4 | `tests/benchmark_gui_store_test.cpp` | 저장 계층 |
| | `tests/benchmark_worker_test.cpp` | worker / 실행 경계 |
| | `tests/ui_benchmark_test.cpp` | 실제 MainWindow 컨트롤·게이트 |
| | `tests/ui_benchmark_e2e_test.cpp` | **실제 엔진 E2E** (ffmpeg 생성 미디어) |

## 2. 실제 검증 결과 (예상값 아님)

### benchmark 자체 검사

| 대상 | checks |
| --- | --- |
| `benchmark_core_test` (S2) | 63 |
| `benchmark_journal_test` (S3) | 51 |
| `benchmark_store_test` (S3) | 51 |
| `benchmark_integration_test` (S3) | 137 |
| `benchmark_gui_store_test` (S4) | 100 → **110** (Resource Policy 기록/호환 추가) |
| `benchmark_worker_test` (S4) | 26 → **35** (policy plumbing + fingerprint 추가) |
| `ui_benchmark_test` (S4) | 34 |
| `ui_benchmark_e2e_test` (S4, 실제 엔진) | 31 → **40** (policy/fingerprint 실측 추가) |

### CTest

- **CPU: 94/94 PASS**
- **GPU: 95/95 PASS**

### S1 CLI 회귀 (변경 없음 확인)

- `--version` → `Media Similarity Finder 0.9.4.43 (CUDA/CPU)`
- `--help` → 사용법 출력
- `--smoke` → `smoke: window created` / `smoke: event loop ok`
- 잘못된 옵션 → `Error: unknown option: --nope` (exit 2)
- `--benchmark` → `Error: unknown option: --benchmark` — **S5 전까지 계속 거부 상태**

### 실제 GUI E2E 에서 관측된 값

- **CPU 빌드**: `gpu-max.json` status = **`SKIPPED`** (CUDA backend 부재)
- **GPU 빌드**: `gpu-max.json` status = **`SUCCESS`** (실제 CUDA 실행)

이는 S2 의 `modeAvailable` / effective mode 규칙이 두 빌드에서 올바르게 반영된 결과이며,
GPU 가 없는데 성공으로 기록하지 않는다.

## 3. GUI E2E 가 실제로 검증한 것

`ui_benchmark_e2e_test` 는 fake executor 를 쓰지 않는다. ffmpeg 로 실제 미디어
(이미지 24개 + 비디오 1개)를 만들고 **실제 MainWindow 컨트롤을 클릭**하여
**실제 `ProductionBenchmarkExecutor` → `MediaSearchEngine`** 경로를 실행한다.

검증 항목과 결과:

- AUTO / CPU-only / GPU-max 3개 동시 선택으로 실제 실행 → `auto.json`, `cpu.json`, `gpu-max.json` 3개 생성
- 선택 부분집합 재실행 → **선택 안 한 `cpu.json` 이 byte 단위로 보존**, 선택한 `auto.json` 은 교체
- **production Index 오염 없음**: 실행 전후 `Index/` 항목 집합을 **내용 비교**했다.
  MainWindow 생성으로 추가된 항목 0건, benchmark 실행으로 추가된 항목 **0건**.
  (기존 항목은 과거 CLI/테스트 산재물이며 benchmark 와 무관하다)
- benchmark 인덱스는 `Benchmark/GUI/<label>_<id>/runtime/run-<id>/<mode>/` 아래에만 생성됨
- 스캔 대상 폴더에는 json/jsonl/Index/db 산출물 **0건**
- suite lock busy: 외부 holder 가 쥔 상태에서 실행 시도 → **시작되지 않고** 기존 snapshot **변경 없음**
- Cancel: 실행 중 정지 → UI idle 복귀, 실행 버튼·스캔 버튼·mode checkbox 재활성
- 실행 중 상호 배제: benchmark 중 `scan` 비활성, `pause` 비활성, mode checkbox 3개 잠금, `stop` 활성

## 4. 미해결 이슈 (S4 미완료 사유)

### 4-1. Resource Policy 전달 — **해결됨**

조사 결과 (코드 확인, 추측 아님):

- `msf::BenchmarkRequest` 에 `ResourcePolicy` 를 담는 필드가 **존재하지 않았다**.
  존재하는 것은 `distance` 뿐이었다.
- `ProductionBenchmarkExecutor::runMode()` 은 내부에서 `MediaSearchEngine` 를 지역 생성하고
  `engine.resourcePolicy()` 로 출발했다. 이는 기본 생성된 `policy_{}` 이므로
  GUI 의 `preset_` / `cpu_` 값이 전달될 통로가 없었다.

**해결 방식 (최소 추가, additive)**

- `BenchmarkRequest::resourcePolicy` 를 `std::optional<ResourcePolicy>` 로 추가했다.
  **비어 있으면(기본값) 기존 S2 동작이 그대로 유지**된다 — 엔진 기본 policy 에서 출발하고
  `policy.gpuEnabled` 만 mode 로 조정한다. 기존 S2/S3 호출자는 이 필드를 지정하지 않으므로
  영향이 없다.
- `ProductionBenchmarkExecutor::runMode()` 은 이제
  `request.resourcePolicy` 가 있으면 그것에서, 없으면 기존처럼 `engine.resourcePolicy()` 에서
  출발한다. **gpuEnabled 는 양쪽 모두 mode 로부터 결정**되므로 S2 규칙이 그대로다.
- GUI 는 `req.resourcePolicy = policy_` 로 **MainWindow 가 이미 해석해 둔 policy 를 그대로**
  전달한다. `policy_` 는 `resourceChanged()` / `customResourceChanged()` 안에서
  `msf::make_policy()` 로만 만들어지므로, preset/CPU 해석 로직을 복제하거나
  새 policy 생성 경로를 만들지 않았다.

**검증 (실제 GUI E2E)**: toolbar 의 preset combo 를 "Maximum 90%" 로 선택한 뒤 실제로 실행하면
스냅샷에 `"resourcePolicy":{"mode":1,...,"cpuPercent":90,...}` 이 기록된다.
Balanced 기본값 55 로 남아있지 않음을 함께 확인한다.
저장 계층 테스트도 정책 기록과 "정책 미지정 시 블록이 아예 생략되어 기존 스냅샷과 호환"됨을 검증한다(110 checks).

**남는 제약**: `gpuEnabled` 는 benchmark mode 가 결정한다(S2 규칙 유지 지시).
따라서 GUI 의 `gpuEnabled_` 체크박스는 AUTO / GPU-max 실행에 영향을 주지 않는다.
preset 축과 mode 축은 계속 별개다.

### 4-2. `datasetFingerprint` — **해결됨**

조사 결과:

- 해시 함수 자체는 **존재한다**: `msf::computeDatasetFingerprint(root)` (`src/dataset_fingerprint.{h,cpp}`),
  `MediaSearchEngine` 이 이미 `src/media_search_engine.cpp` 에서 호출해 legacy recorder 에 넣는다.
- `DatasetFingerprint` 는 `state` / `fingerprint` / `fileCount` / `totalBytes` 를 가진
  공개 멤버 구조체라, **계산된 canonical 값에 직접 접근하는 accessor 는 이미 있다**
  (별도 accessor 추가 불필요).

**해결 방식**

- worker 가 fingerprint 미지정 시 `msf::computeDatasetFingerprint(request_.sourceRoot).fingerprint`
  를 그대로 사용한다. **새 해시도, 새 직렬화 형식도 만들지 않았고**,
  다른 필드를 이어 붙이지도 않았다. fingerprint 문자열을 verbatim 으로 기록한다.
- 파일 내용을 읽는 walk 가 UI 를 막지 않도록 **worker 스레드에서** 계산한다
  (일반 스캔과 동일한 기존 호출 경로).
- 빈 문자열 상태는 더 이상 정상 완료 상태가 아니다. root 가 없거나 읽을 수 없을 때만
  비어 있으며, 그 경우 `DatasetFingerprint::state` 가 `not_available` / `failed` 다.

**검증 (실제 GUI E2E)**: 스냅샷에 64자 소문자 hex 값이 기록되고,
그 값이 `computeDatasetFingerprint(root).fingerprint` 와 **완전히 동일**함을 확인한다.
예: `3793e510219a2f85312aad725d7fcf9ff6f12386bf17cfe9b0194d2871598c1a`

### 4-3. `selectedBenchModes()` 는 private

public API 로 승격하지 않았다. 현재 커버는:

- `ui_benchmark_test`: UI 게이트가 7개 조합을 모두 수용하는지 간접 검증
- `benchmark_worker_test`: runner 가 전달받은 벡터를 파일 단위 순서대로 실행하는지 검증

mode 조합 → runner 벡터의 직접 대응은 간접 검증이다. 추가 직접 검증이 필요하면
public API 신설보다 실제 실행 경로/신호로 검증하는 쪽이 맞다고 판단했다.

### 4-4. 진행 표시의 한계 (기능 결함이 아니라 S2 계약)

S2 는 case 가 **완료될 때만** hook 을 준다. case 내부 진행 hook 이 없으므로
진행 중인 파일/mode 는 알 수 없다. 따라서 UI 는 **"완료 k/N · 마지막 &lt;파일&gt;"** 로
표시하며, 코드 주석과 문자열에 이 사실을 명시했다. 추측 표시하지 않는다.

## 5. 남은 사항과 S5 진입 조건

Resource Policy 전달(4-1)과 datasetFingerprint(4-2)가 해결되었으므로 S4 를 CLOSED 로
기록할 수 있다. 다만 다음 두 항목은 **의도적으로 남겨둔다**.

- ④ O(N²) folder walk 유지 (S2 특성, S3 가 저장 계층이라 손대지 않음)
- `gpuEnabled` 는 benchmark mode 가 결정 → GUI 의 `gpuEnabled_` 체크박스는 AUTO / GPU-max 실행에
  영향을 주지 않는다. 지시대로 S2 규칙을 유지했다. 사용자가 이 동작을 수용하는지는 확인 필요.

`selectedBenchModes()` 는 private 유지(간접 검증, 4-3).

S5 진입 조건 3가지:

1. O(N²) walk 를 그대로 감수할지, 실행 전 파일 수 상한/경고를 넣을지 결정
2. CPU 빌드에서 GPU-max 가 `SKIPPED` 로 기록되는 것이 충분한 표현인지 확인
3. benchmark 실행 시 GUI GPU 토글을 반영해야 한다면 S2 규칙 변경 여부를 사전 결정
