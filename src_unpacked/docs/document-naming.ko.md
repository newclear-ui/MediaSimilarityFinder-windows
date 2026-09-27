# 문서 네이밍 및 구조 규칙

> 이 문서는 MediaSimilarityFinder 문서의 **명명 규칙과 역할 분리 규칙에 대한 기준 문서**다.
> 새 문서를 추가하거나 기존 문서를 이동할 때는 이 규칙을 먼저 확인한다.

## 1. 기본 원칙

- 문서는 **역할에 따라 하나의 정식 위치와 이름**을 가진다.
- 한국어/영문 문서는 원칙적으로 항상 **동일한 basename + 언어 suffix** 쌍으로 유지한다.
- 파일명에는 날짜, 작성자명, 임시 상태를 넣지 않는다. 예외적으로 역사적 버전을 고정해서 보존해야 하는 문서는 버전 식별자를 사용한다.
- 같은 정보를 여러 상위 문서에 중복해서 관리하지 않는다.
- 이름을 바꾸는 경우 파일 내용뿐 아니라 **모든 내부 링크, 인덱스, 구조 문서, LLM 인덱스**를 같은 변경에서 갱신한다.
- 문서 이동은 가능하면 `git mv`를 사용하여 Git에서 rename history가 보존되도록 한다.
- 소스/런타임 코드와 관계없는 문서 구조 정리는 제품 버전을 올리지 않고 별도의 `docs:` 커밋으로 기록한다.

## 2. 정식 문서 분류

| 분류 | 정식 경로 규칙 | 역할 |
|---|---|---|
| Roadmap | `docs/development-roadmap.ko.md` / `.en.md` | 전체 개발 방향, Node 순서, 경계와 의존관계 |
| Progress | `docs/development-progress.ko.md` / `.en.md` | 현재 실제 Node/version/substep/blocker/검증 상태 |
| Document Naming | `docs/document-naming.ko.md` / `.en.md` | 문서 명명·위치·갱신 규칙의 기준 |
| Implementation Brief | `docs/implementation-briefs/<Node>-<topic>.ko.md` / `.en.md` | 특정 Node의 구현 계약, 범위, 단계, telemetry, 완료 조건 |
| Build History | `docs/build-history/<version>.ko.md` / `.en.md` | 특정 버전의 실제 변경·검증 증거 |
| Work Log | `docs/worklog/<development-line>.ko.md` / `.en.md` | 한 개발선 전체의 누적 작업 흐름, 판단, 실험, 학습 |
| Architecture | `docs/architecture/<topic>.ko.md` / `.en.md` | 장기 설계·구조 설명 |
| Architecture Snapshot | `docs/architecture/<topic>-<version>.ko.md` / `.en.md` | 특정 버전 시점의 역사적/감사 snapshot이 필요한 경우에만 허용 |
| Legacy Snapshot | `docs/architecture/legacy/` | 교체된 구현/설계의 비빌드 보존본 |
| Structure | `docs/STRUCTURE.md` | 실제 저장소/소스 구조 요약 |
| LLM Index | `docs/llms.txt` | LLM/자동화 도구가 먼저 읽을 핵심 문서·구조·URL 규칙 |

## 3. 파일명 규칙

### 3.1 언어 suffix
- 한국어: `.ko.md`
- 영어: `.en.md`
- 두 파일은 같은 basename을 사용한다.
- 한쪽만 새로 만들거나 한쪽만 갱신하는 것은 금지한다.

### 3.2 Implementation Brief
형식:
```
<Node>-<topic>.ko.md
<Node>-<topic>.en.md
```
예: `B-adaptive-scheduler.ko.md`, `C-calibration-profile.ko.md`, `D-pipeline-queue.ko.md`
Node가 활성화되면 해당 Node의 정식 brief를 하나로 유지한다. 단계별 세부 구현은 brief 내부 heading으로 나누고 임시 파일을 따로 만들지 않는다.

### 3.3 Build History
형식:
```
<version>.ko.md
<version>.en.md
```
예: `0.9.4.24.ko.md`, `0.9.4.24.en.md`
**Build History의 버전 기반 이름은 변경하지 않는다.** 파일명 자체가 특정 구현 상태의 증거 식별자이기 때문이다.

### 3.4 Work Log
형식:
```
docs/worklog/<development-line>.ko.md
docs/worklog/<development-line>.en.md
```
예:
```
docs/worklog/0.9.4.ko.md
docs/worklog/0.9.4.en.md
```
Work Log 파일명에 `0.9.4.19-0.9.4.24`와 같은 **버전 범위를 넣지 않는다.**
개발이 계속되는 동안 같은 `0.9.4` 문서를 누적하고 내부에서 버전별 heading/table로 구분한다.
다음 개발선이 시작되면 `docs/worklog/0.9.5.ko.md` / `.en.md`처럼 새 개발선 파일을 만든다.

### 3.5 Architecture
일반 구조 문서는 주제만 사용한다.
```
resource-scheduling.ko.md
benchmark-telemetry-roadmap.ko.md
gpu-backend-roadmap.ko.md
```
특정 버전의 감사/snapshot 성격이 명확한 경우에만 `<topic>-<version>`을 허용한다.

## 4. 역할 중복 금지
- Roadmap에는 상세 구현 절차를 중복해서 쓰지 않는다.
- Progress에는 설계 문서를 복사하지 않고 **현재 상태와 다음 gate**만 기록한다.
- Implementation Brief에는 해당 Node가 실제로 구현해야 할 계약을 기록한다.
- Build History에는 실제 변경과 실제 검증 결과만 기록한다.
- Work Log에는 여러 버전에 걸친 흐름과 의사결정의 맥락을 기록한다.
- Architecture에는 장기적으로 유지할 구조를 기록한다.

## 5. 변경 절차
1. 기존 이름/링크를 전수 확인한다.
2. 규칙에 맞는 새 경로를 결정한다.
3. `git mv`로 이동한다.
4. 문서 본문의 상호 참조를 수정한다.
5. `docs/STRUCTURE.md`, `docs/llms.txt`, 필요한 README/인덱스를 갱신한다.
6. KO/EN 쌍과 내부 링크를 점검한다.
7. 문서-only 변경이면 `docs:` 커밋으로 분리한다.

## 6. 현재 정리 대상
기존:
```
docs/worklog-0.9.4.19-0.9.4.24.ko.md
docs/worklog-0.9.4.19-0.9.4.24.en.md
```
정식 위치:
```
docs/worklog/0.9.4.ko.md
docs/worklog/0.9.4.en.md
```
내용은 0.9.4.19~0.9.4.24의 기록을 유지하되 파일명만 개발선 단위로 일반화한다.
이후 0.9.4.25부터는 같은 `docs/worklog/0.9.4.ko.md` / `.en.md`에 누적한다.
