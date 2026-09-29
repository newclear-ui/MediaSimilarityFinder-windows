# Implementation Brief — Node F Hardware Video Decode Backend (Pre-register)

Status: **PRE-REGISTERED** — 이 문서는 brief 만 포함하며, probe/구현 코드가 이 커밋보다 앞서면 안 된다.
Version 기준: v0.9.4.42 (`395ae57`)

---

## 1. 목적

Node E(Adaptive Video Decode Planner)가 0.9.4.42 종결되었다. E 는 "불필요한 decode 를
줄인다"는 목표로 시작했지만, sparse seek 는 exactness 가 증명되지 않아 **production
채택이 거부**되었고 최종 결과는 "sequential 을 확정"이었다.

Node F 는 **다른 축**을 다룬다. 어떤 프레임을 샘플링할지가 아니라, **선택된 프레임을
어떻게 디코딩할지**다. software FFmpeg decode 를 **GPU(NVDEC) hardware decode 로
대체**하는 것이 목표이며, 이는 E 가 다루지 않았던 축이다.

## 2. 시작 조건

- Node E 종결 확인 (`docs/development-progress.*`, `docs/build-history/0.9.4.42.*`)
- 이전 E brief 의 조건 "F 는 E 가 완전히 끝나기 전에는 시작하지 않는다" → 충족됨
- `ExactnessPolicy::RefuseAll` 및 production 순차 디코딩 경로는 **변경하지 않는다**

## 3. 범위 (Scope)

### 3-1. 이번 F 의 실제 범위 — 단일 backend

```text
Primary backend candidate: NVIDIA NVDEC
이번 F 의 실제 조사·실험·구현 범위: NVDEC
```

- 다른 hardware decode backend 의 **실제 조사·실험·구현·검증은 이번 F 범위에 포함하지 않는다.**
- roadmap 의 NVDEC 우선(Node F)을 그대로 반영한다.

### 3-2. Architecture 는 NVIDIA 전용으로 고정하지 않는다

backend abstraction 은 **향후 Intel/AMD 등 hardware decode backend 를 추가할 수 있는
방향**을 고려한다. 단, 그 구현·검증은 **이번 F 의 범위가 아니다.**

이 구분이 중요:
- **이번 F**: NVDEC 하나를 실제로 조사·검증·구현한다.
- **추후 G 및 이후**: 다른 backend 를 추가할 수 있도록 **추가 비용이 낮게** 설계한다.
- abstraction 설계는 "NVDEC 특수화"가 아니라 "backend 교체 가능한 형태"를 목표로 하되,
  **premature generalization 은 하지 않는다.** 검증되지 않은 backend 인터페이스를
  미리 추상화하는 것이 오히려 위험하다.

## 4. 이번 F 에서 하지 않는 것

- sparse seek 재도입 또는 재구현
- `ExactnessPolicy::RefuseAll` 완화
- `AllowVerified` 활성화
- production predicate(`ft + 0.05 >= target`) 또는 tolerance 변경
- E-3B 결론 뒤집기
- Intel/AMD/기타 hardware decode backend 의 실제 구현·검증
- Node E 의 샘플링 로직 변경

## 5. E-3B에서 승계하는 검증 원칙 (반드시 준수)

이것은 Node F 의 **선행 조건**이며 선택 사항이 아니다.

### 5-1. 기준선은 반드시 production 경로

```text
production sequential decode (software FFmpeg)
        vs
experimental optimization
```

**금지**: `seek implementation A vs seek implementation B` 형태의 비교.
E-2A/E-2B 의 exactness 는 이 형태로 **자기참조**였고, 그 결과가 무효화되었다
(`docs/build-history/0.9.4.42.*`, worklog `E-3B-REF`).
같은 계열의 두 구현은 **공유하는 결함을 서로 검증할 수 없다.**

### 5-2. A vs A control

실험 결과는 가능하면 항상 `A vs A control`(동일 조건 2회 실행)과 함께 판정한다.
control 이 0 이 아니면 실험 간 차이를 실험 자체의 영향으로 해석할 수 없다.

### 5-3. 분리해서 확인할 항목

영상 decode 관련 변경에서는 아래를 **분리**해서 확인한다.

- sample count
- sample position (PTS)
- decoded result (픽셀)
- fingerprint
- failure / recovery 상태
- codec / container 별 차이

단순히 "테스트가 PASS했다"는 이유만으로 exactness 를 주장하지 않는다.

### 5-4. exactness 증거로 취급 금지

- container index 값
- probe 내부 측정값
- seek landing 정합성
- `SparseSeekStats` 계열 계측

production-parity 증명이 기록되기 전까지 어떤 값도 exactness 근거가 아니다.

## 6. 조사 대상 (실험 전 pre-register)

코드 작성 전에 아래를 조사하고 결과를 기록한다. 조사부터 커밋한다.

- FFmpeg 9.0.1 의 NVDEC 경로: `avcodec_get_hw_config`, `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX`
- 현재 빌드의 FFmpeg 구성: `vcpkg_installed` 의 FFmpeg 빌드 옵션에 `cuda`/`nvdec`/`cuvid` 가 포함되어 있는가
- GPU backend 추상화(`src/gpu_backend.*`)가 어느 지점에서 decode 를 관장하는지
- `VideoDecoder`(`src/video_decoder.*`)가 software 경로에 얼마나 깊이 묶여 있는가
- GPU hash(96x96/32x32) 파이프라인과 decode 분리 가능 지점
- 기존 `build-windows-gpu` 와 `build-windows-cpu` 양쪽에서의 video 경로 동작 차이
- 이 환경의 GPU/드라이버/CUDA 버전

**미해결 질문(실험 전에 명시적으로 답해야 함)**:
- hardware decode 결과가 software decode 와 **픽셀 단위로 동일한가?** E-3B 와 같은 함정
  (같은 계열끼리 비교)에 빠지지 않았는지 확인할 방법이 무엇인가.

## 7. 측정 계획 (pre-register)

- dataset: video dataset 확보 상태를 먼저 확인. 없다면 준비 절차부터 문서화
- run count: 5회 이상 권장, median/min/max/range 기록
- 측정 항목: decode wall time, frames/s, GPU/CPU split, fallback 발생 횟수, VRAM
- exactness: production sequential decode 와 **픽셀 및 fingerprint** 비교
- 측정하지 않은 값은 `Not measured` 로 기록한다. 추정하지 않는다.
- 실행 오차 범위 내의 차이는 개선으로 선언하지 않는다.

## 8. Fallback 원칙

- software FFmpeg 은 **기준/폴백 경로로 유지**한다
- 초기화 / seek / frame mapping / decode 실패는 **파일 단위 fallback**
- fallback 은 조용히 일어나서는 안 된다. backend 선택과 fallback 사유를 benchmark 에 기록
- NVDEC 이 해당 파일에 이득이 없으면 CPU decode 또는 다른 경로를 선택 가능하게 유지
- low-level vendor API 를 상위 엔진에 확산시키지 않는다

## 9. 산출물 (예상)

- `docs/build-history/<version>.*`
- `docs/development-progress.*` 갱신
- 구현 시: backend 분리 구조, benchmark/telemetry 필드, 회귀 테스트

## 10. 완료 조건 (초안 — 착수 시 확정)

- CPU path PASS (software decode 결과 불변)
- GPU build PASS
- **production baseline 대비 video exactness PASS** (§5.1 준수)
- fallback PASS
- no unexplained mismatch
- NVDEC 미지원 codec/profile/pixel-format/bit-depth 에서 **파일 단위 fallback 이 실제로 동작**함을 측정으로 확인

## 11. 위험 요소 (착수 전 인지 필요)

- **전제 붕괴 위험**: 이 환경의 FFmpeg 빌드에 NVDEC 가 포함되어 있지 않을 수 있다.
  `build-windows-gpu` 가 존재한다는 사실은 GPU **hash** 를 GPU 로 계산한다는 뜻이지,
  **decode** 가 GPU 로 된다는 뜻이 **아니다.** 조사(§6)에서 먼저 확인해야 한다.
- **exactness 위험**: hardware decode 와 software decode 의 출력이 다를 수 있다.
  어떤 차이를 허용할지는 **판정 근거와 함께 명시**해야 하며, 임의 임계값으로 정하지 않는다.
- **범위 표류**: 다른 backend 까지 미리 추상화하면 §3-2 의 경계를 넘어 검증 없는
  인터페이스가 남는다.
- **성능 전제**: E-3B 에서 sparse 는 정확성 위반 + 17% 저하가 함께 나타났다.
  NVDEC 도 **정확성 검증 전에 성능 이득을 근거로 채택하지 않는다.**

## 12. 관련 문서

- `docs/build-history/0.9.4.42.*` — E-3B, sparse 기각과 exactness 교훈
- `docs/worklog/0.9.4.*` — `E-3B-REF`, `E-CLOSE`
- `docs/implementation-briefs/E-planner-calibration.*` — E COMPLETE 조건 종결 처리
- `docs/development-roadmap.*` — Node F 범위
- `docs/architecture/benchmark-telemetry-roadmap.*` — telemetry 설계
