# Explorer Reuse Failure — Fix Record

Status: fixed in v0.9.3.12 and verified on Windows.

## Symptom
- Folder window closed: the `explorer.exe /select` fallback opens a new window with the file selected.
- Folder window already open: the window matches (`GetCurFolder` path equality confirmed) but the
  file is never selected. It falls back to focusing the window or opening a new one.

## Confirmed cause 1: CLSCTX (fixed in v0.9.2.88)
- `revealInOpenExplorer()` created `IShellWindows` with `CLSCTX_INPROC_SERVER`, but ShellWindows is
  registered out-of-proc, so creation always failed with `REGDB_E_CLASSNOTREG`.
- Fixed with `CLSCTX_ALL`; enumeration and matching work. `reveal_window_test` cases A/B PASS.

## Cause 2: ILFindChild mismatch (fixed in v0.9.3.12)
- `ILFindChild(basePidl, filePidl)` returns NULL for the matched window's `GetCurFolder` PIDL
  (`basePidl`) and the file PIDL from `SHCreateItemFromParsingName`+`SHGetIDListFromObject`,
  even though both are absolute PIDLs.
- Side finding: shell parsing names need native `\`, not Qt-style `/`
  (`SHCreateItemFromParsingName` observed failing with `E_INVALIDARG`).
- `IWebBrowser2::Navigate2(file path)` also failed to produce a selection here (HRESULT `800706F4`).
- The implementation now enumerates child PIDLs returned by the active view, compares their absolute
  paths, and passes the matching child PIDL to `SelectItem()`. The real `reveal_window_test` confirmed
  selection while retaining the same HWND and window count (`8->8`).

## Repro
- The A/B scenarios in `reveal_window_test` (new-window selection and existing-window reuse) pass.
- Temporary stdout logging of the `SHCreateItemFromParsingName` HRESULT and `ILFindChild` result
  proved effective (stderr is not captured by the test harness).

## Remaining validation scope
1. Inactive tabs in Windows 11 tabbed Explorer need separate validation.
2. Child enumeration cost should be measured for very large folders.
