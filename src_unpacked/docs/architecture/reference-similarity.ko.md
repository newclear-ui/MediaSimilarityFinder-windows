# 외부 참고: GAR Similarity v2.5.1 분석

> 원본: `C:\project\Similarity.zip` (바이너리만, 소스 없음). GAR Software, 2007~2021, 업데이트 중단.
> 오디오 중복 검색이 본체. 이미지는 v1.7.1부터 붙은 부기능(`content` + `experimental`).
> 클로즈드 소스이므로 코드 재사용 금지. 아이디어·동작 패턴만 클린룸 참고.

## 분석 근거

- `Similarity.exe` 15MB, `Decoder.exe`, `Scripter.exe`, `Speech.exe`
- `Scripts/*.js` 24개 (`example-async.js`, `test.js`, `scan-samename.js`, `automark.js`, `export-results.js`)
- `Locale/English.lng` (v2.0.1, 전체 UI 문자열), `Locale/Korean.lng` (v1.4.0 구판)
- `readme.txt`, `changes.txt` (v0.2~v2.5.1 전체 변경 이력)
- `Similarity.exe` 유니코드 문자열 정적 추출 (파이프명, OpenCL, 캐시/디코더 관련)

## 검색엔진

```
queue(file, algs) → compare(file, callback) → calculate(i1, i2, algs, cb) → results.add()
process(folder, onfile, onfolder)  // 재귀 스트림, Scripts API
```

- 다단계 점수: 오디오 `tags → content → precise → speech`, 이미지 `content + experimental`. 알고리즘별 임계값 OR 판정, 혼합식도 가능 (`example.js`의 tags×content×precise 혼합).
- `duration` 사전 필터로 N² 회피 (`scan-samename.js`, `scan-with-restrictions.js`).
- `v2.2.0 work queue` 멀티프로세서 큐, 작업 스레드 수 제한 옵션, `precise`는 OpenCL(CUDA/AMD)로 오프로드.
- `v2.3.1 Global optimization indexing` — 대용량 컬렉션용 색인.
- 이미지 반전/거울 검출 (v1.7.1, v1.9.0) — 우리 mirror 대응과 동일 발상.

## 파일스트림

- `Decoder.exe` 별도 프로세스 + 네임드 파이프(`\\.\pipe\similarity...`) 통신. 크래시 격리, 30초 무활동 자동 종료, 실패 파일 skip-list 등록 (v1.1, v1.8.4, v1.9.0).
- 캐시 통합: 보기 캐시 == 비교 캐시 (v1.8.2), 분석 결과 캐시로 재스캔 단축.
- `journal` 실시간 자동 저장 (v1.9.0) — 크래시해도 결과 유지. `ignores.list` 별도 저장.
- 손상 파일 허용 (v1.9.1: 8bit/손상 WAV 처리), 디코더별 on/off·우선순위·확장자 설정.

## 검색중 UI

- Start / Pause / Stop, `Estimated Time Remaining`, 작업 표시줄 진행률 (v1.5.3), 상태바 `duplicate(s) / Cache / New`.
- 탭: `Folders / Results:Audio / Results:Images / Analysis`. 그룹/플랫 모드, `Pairs` 카운트 컬럼 (v1.8.1), 기준 파일 bold.
- `Automark` (임계값+폴더 우선순위+포맷+태그), `Rearrange`, `Invert marked`, Analysis↔Duplicates 동기 삭제/swap/rename.
- 전원 상태 유지 (스캔 후 절전 복원, v1.8.2), 트레이 최소화, 폴더 단위 Mark/Unmark.

## 우리 프로젝트 대응표

| Similarity | MediaSimilarityFinder 현재 상태 | 참고 방향 |
|---|---|---|
| journal 자동 저장 | 5초 체크포인트 + `loadMatches` 빠른 불러오기 (동등) | 유지, 검증 근거로 활용 |
| duration 사전 필터 | 동영상 duration 게이트 있음 (`scan_pipeline.cpp`) | 이미지 쪽 불필요, 동영상 게이트 유지 |
| 디코더 격리+skip-list | 인프로세스 디코더, 실패 시 재시도 반복 | 0.9.2.62에서 실패 skip-list 도입 |
| Pairs 컬럼 | 그룹당 파일 수만 표시 | 0.9.2.62에서 Pairs 컬럼 추가 |
| 남은 시간 표시 | 경과 시간만 표시 | 0.9.2.62에서 ETR 추가 |
| Automark 우선순위 | mark/invert만 있음 | 후순위 (폴더 우선순위 다이얼로그) |
| V8 스크립팅 | 없음 | 도입 보류 (범위 과대) |
| OpenCL precise | CUDA 이미지 해싱 (`gpu_backend`) | 구조 대응됨, 변경 없음 |

## 라이선스 주의

- Similarity 본체는 GAR 독점 라이선스. zip 내 `Decoder.exe`/`Similarity.exe`/`Scripts`를 그대로 번들·복제 금지.
- `readme.txt`의 libFLAC/libvorbis/TagLib/V8 등은 해당 OSS 라이선스 준수 시에만 사용. 우리는 이미 `vcpkg.json` 기준이므로 신규 도입 시에만 검토.
- 이 문서는 동작 관찰 기반의 아이디어 정리이며, 바이너리 역컴파일·코드 복제를 하지 않는다.
