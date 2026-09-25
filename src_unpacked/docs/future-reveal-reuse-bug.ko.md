# 탐색기 재사용 실패 — 수정 기록

상태: v0.9.3.12에서 수정 및 실제 Windows 테스트 통과.

## 증상
- 폴더 창이 닫혀 있으면 `explorer.exe /select` 폴백으로 새 창이 열리며 파일이 정확히 선택됨.
- 폴더 창이 이미 열려 있으면 매칭은 되지만(`GetCurFolder` 경로 일치 확인) 파일 선택이 안 됨.
  창이 앞으로 오거나, 폴백으로 새 창이 열리는 데 그침.

## 확정된 원인 1: CLSCTX (해결됨, v0.9.2.88)
- `revealInOpenExplorer()`가 `CLSCTX_INPROC_SERVER`로 `IShellWindows`를 생성했으나
  ShellWindows는 out-of-proc 등록이라 항상 `REGDB_E_CLASSNOTREG` 실패.
- `CLSCTX_ALL`로 수정 후 열거·매칭은 동작. `reveal_window_test` case A/B PASS.

## 원인 2: ILFindChild 불일치 (수정됨, v0.9.3.12)
- 매칭된 창의 `GetCurFolder` PIDL(`basePidl`)과
  `SHCreateItemFromParsingName`+`SHGetIDListFromObject`의 파일 PIDL(`filePidl`)에 대해
  `ILFindChild(basePidl, filePidl)`이 NULL 반환. 둘 다 절대 PIDL인데도 자식으로 인정 안 됨.
- 부수 발견: 셸 파싱명에는 Qt식 `/` 대신 네이티브 `\`를 써야 함
  (`SHCreateItemFromParsingName`이 `E_INVALIDARG`로 실패하는 경우 확인).
- `IWebBrowser2::Navigate2(파일 경로)`도 같은 환경에서 선택을 만들지 못함 (HRESULT `800706F4`).
- 현재 view가 직접 반환하는 child PIDL을 열거하고 절대 경로를 비교한 뒤 해당 child PIDL을
  `SelectItem()`에 전달하는 fallback을 추가했다. 실제 `reveal_window_test`에서 동일 HWND와
  동일 창 수(`8->8`)를 유지하면서 선택까지 확인했다.

## 재현
- `reveal_window_test`의 A/B 시나리오(새 창 선택·기존 창 재사용)로 검증했다.
- `SHCreateItemFromParsingName` HRESULT·`ILFindChild` 결과를 stdout 임시 로그로 확인하는 방식이 유효했음
  (stderr는 테스트 하네스에 잡히지 않음).

## 남은 검증 범위
1. Windows 11 탭형 Explorer의 비활성 탭은 별도 검증이 필요하다.
2. 매우 큰 폴더에서는 view child 열거 비용을 측정할 필요가 있다.
