# 탐색기 재사용 실패 — 추후 버그 수정 기록 (일시 중지)

상태: 일시 중지. v0.9.2.89 커밋(`32dec2a`)에는 포함되지 않음. 탐색기 WIP 코드는
`MediaSimilarityFinder-0.9.2.89-src-backup-20260925.zip`(2026-09-25 백업)에 보존됨.

## 증상
- 폴더 창이 닫혀 있으면 `explorer.exe /select` 폴백으로 새 창이 열리며 파일이 정확히 선택됨.
- 폴더 창이 이미 열려 있으면 매칭은 되지만(`GetCurFolder` 경로 일치 확인) 파일 선택이 안 됨.
  창이 앞으로 오거나, 폴백으로 새 창이 열리는 데 그침.

## 확정된 원인 1: CLSCTX (해결됨, v0.9.2.88)
- `revealInOpenExplorer()`가 `CLSCTX_INPROC_SERVER`로 `IShellWindows`를 생성했으나
  ShellWindows는 out-of-proc 등록이라 항상 `REGDB_E_CLASSNOTREG` 실패.
- `CLSCTX_ALL`로 수정 후 열거·매칭은 동작. `reveal_window_test` case A/B PASS.

## 미해결 원인 2: ILFindChild 불일치 (잔류)
- 매칭된 창의 `GetCurFolder` PIDL(`basePidl`)과
  `SHCreateItemFromParsingName`+`SHGetIDListFromObject`의 파일 PIDL(`filePidl`)에 대해
  `ILFindChild(basePidl, filePidl)`이 NULL 반환. 둘 다 절대 PIDL인데도 자식으로 인정 안 됨.
- 부수 발견: 셸 파싱명에는 Qt식 `/` 대신 네이티브 `\`를 써야 함
  (`SHCreateItemFromParsingName`이 `E_INVALIDARG`로 실패하는 경우 확인).
- `IWebBrowser2::Navigate2(파일 경로)`도 같은 환경에서 선택을 만들지 못함 (HRESULT `800706F4`).
- 위 3단계(SelectItem→Refresh 후 재시도→동일 창 Navigate2)가 모두 실패하면
  WIP 코드는 `NoWindow`를 반환해 새 창으로 폴백하므로, 겉으로는 “새 창은 뜨는데 재사용이 안 됨”으로 보임.

## 재현
- `reveal_window_test`에 case D/E/F(선택 없는 창·재선택·최소화 창) + case G(Navigate2 메커니즘)를
  추가하면 재현됨. 해당 확장 코드는 위 백업 zip에만 있음(커밋 안 됨).
- `SHCreateItemFromParsingName` HRESULT·`ILFindChild` 결과를 stdout 임시 로그로 확인하는 방식이 유효했음
  (stderr는 테스트 하네스에 잡히지 않음).

## 다음 단계 후보
1. `basePidl`도 `SHGetIDListFromObject` 계열(폴더 `IShellItem`)로 구해 PIDL 형태를 통일 후 `ILFindChild` 재시도.
2. `IFolderView::SelectAndPositionItems` 또는 표시명 기반 선택으로 우회.
3. 창 열거 시 비활성 탭(Windows 11 탭형 탐색기)까지 매칭 범위 확대 검토.
4. 실패 시 `scanLog`에 원인 단계 기록 남기기(진단용, WIP에 초안 있음).
