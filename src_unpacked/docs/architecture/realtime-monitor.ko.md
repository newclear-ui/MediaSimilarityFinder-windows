# 실시간 미디어 중복 감시 아키텍처

## 현재 구현 상태 — 0.9.2.9

0.9.2.8에서 기반 구현을 시작했고, 0.9.2.9에서 Windows 감시 방식을 `ReadDirectoryChangesW` 이벤트 기반으로 전환했습니다.

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

### 핵심 원칙

**실시간 분석 속도보다 사용자의 현재 작업을 보호하는 것이 우선입니다.**

- Windows에서는 각 감시 루트에 `ReadDirectoryChangesW` watcher를 둡니다.
- 동일 파일에 대한 연속 이벤트는 하나의 pending 항목으로 합칩니다.
- 파일 크기/수정 시간이 안정될 때까지 분석하지 않습니다.
- CPU/메모리/GPU 부하가 높으면 분석을 지연합니다.
- 비교 인덱스는 모니터 시작 시 열고 세션 동안 유지합니다.
- 비-Windows 환경은 polling을 fallback으로 사용합니다.

### 다음 설계 과제

- 이벤트 버퍼 overflow 시 전체 루트 재동기화
- 감시 루트의 추가/삭제를 실행 중에도 안전하게 반영
- 시스템 부하에 따른 대기열 우선순위 및 backoff 세분화
- 인덱스 변경을 monitor session에서 즉시 반영
- 게임/foreground workload 보호 정책 고도화


## 0.9.2.11 지연 스케줄러

모니터 작업은 안정 파일 판정 또는 시스템 부하 때문에 반복 지연될 수 있으므로 즉시 재큐잉하지 않는다. 조건변수로 작업 도착을 기다리고, 지연 작업에는 지수형 backoff를 적용한다. 최대 지연은 30초이다. 이를 통해 다운로드 중인 파일이나 게임/고부하 작업 중 발생하는 반복 wake-up과 분석 재시도를 최소화한다.

## 0.9.2.13 상주 비교 인덱스 및 이벤트 복구

0.9.2.13부터 실시간 비교는 SQLite 전체 선형 검색 대신 이미지/비디오별 resident CandidateIndex를 사용한다. 임계값에서 허용되는 Hamming distance만 후보로 조회하고 기존 similarity 계산으로 최종 판정한다. 비교 루트별 엔진에는 root ownership이 명시되어 해당 root의 파일만 해당 index에 동기화한다. Windows `ReadDirectoryChangesW`가 이벤트 버퍼 overflow/0-byte 결과를 반환하면 해당 root를 재귀적으로 재동기화하지만, 실제 분석은 stable-file 및 system-load 보호 정책을 통과해야 한다.


## 0.9.2.14 상태 가시화
`MediaMonitor::status()`가 부하 상태와 queue/deferred/analyzed/matches/errors 카운터를 제공한다. GUI는 1초 주기로 이를 표시하여 foreground workload 보호 때문에 분석이 지연되는 상황을 사용자가 확인할 수 있다.

## 0.9.2.15 수동 일시정지 제어

사용자가 실시간 분석을 즉시 멈출 수 있도록 `MediaMonitor::setPaused()`를 추가했다. 일시정지 중에도 이미 감지된 파일은 대기열에 유지되며, 분석 작업만 수행하지 않는다. 자동 부하 보호와 수동 일시정지는 별도로 동작한다.


## 0.9.2.17 모니터 설정 GUI

전용 설정 대화상자에서 감시 Root와 비교 Root를 서로 독립된 목록으로 관리한다. 모니터 전용 임계값, 안정화 대기 시간, 폴링 간격, GPU 사용 여부도 일반 검색 설정과 분리하여 저장한다.

## 설계 결정 회고 — 0.9.2.8~0.9.2.24

실시간 Monitor는 단순 폴더 감시 기능이 아니라 기존 검색 엔진을 상시 백그라운드 서비스로 확장하는 구조다.

### 변경 필요성

- polling은 Windows에서 불필요한 wake-up과 지연이 발생할 수 있었다.
- 파일 저장 중에는 불완전한 미디어를 분석하면 안 됐다.
- 비교 대상이 많아지면 매번 SQLite를 선형 조회하는 방식은 확장성이 떨어졌다.
- 파일 이벤트 폭주/overflow와 삭제-재생성 race를 복구해야 했다.
- 가장 중요한 요구사항은 실시간 분석이 사용자의 게임/foreground workload를 방해하지 않는 것이었다.

### 해결 구조

`ReadDirectoryChangesW → event coalescing → stable-file detector → load protection → resident CandidateIndex → SimilarityEngine` 구조로 발전했다. 지연 작업에는 condition-variable scheduler와 exponential backoff를 사용하고, 오류/overflow는 root resync와 파일 단위 retry limit으로 복구한다. 수동 Pause/Resume은 자동 load protection과 독립적으로 제공한다.

이 설계의 핵심은 **실시간성보다 비간섭성을 우선**하는 것이다. 분석 지연은 오류가 아니라 사용자 작업을 보호하기 위한 정상 상태다.

### Windows 공통 호환성 반영 (0.9.2.34)
CPU 테스트에서 발견된 Windows API/헤더/파일 경로/파일 watcher 호환성 문제는 CUDA backend와 독립적인 공통 계층에 반영하였다. CUDA backend 자체의 처리 알고리즘은 변경하지 않는다.
