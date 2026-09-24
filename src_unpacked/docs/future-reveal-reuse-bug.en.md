# Explorer Reuse Failure — Deferred Bug Record (Paused)

Status: paused. NOT included in the v0.9.2.89 commit (`32dec2a`). The Explorer WIP code is
preserved in `MediaSimilarityFinder-0.9.2.89-src-backup-20260925.zip` (2026-09-25 backup).

## Symptom
- Folder window closed: the `explorer.exe /select` fallback opens a new window with the file selected.
- Folder window already open: the window matches (`GetCurFolder` path equality confirmed) but the
  file is never selected. It falls back to focusing the window or opening a new one.

## Confirmed cause 1: CLSCTX (fixed in v0.9.2.88)
- `revealInOpenExplorer()` created `IShellWindows` with `CLSCTX_INPROC_SERVER`, but ShellWindows is
  registered out-of-proc, so creation always failed with `REGDB_E_CLASSNOTREG`.
- Fixed with `CLSCTX_ALL`; enumeration and matching work. `reveal_window_test` cases A/B PASS.

## Open cause 2: ILFindChild mismatch (residual)
- `ILFindChild(basePidl, filePidl)` returns NULL for the matched window's `GetCurFolder` PIDL
  (`basePidl`) and the file PIDL from `SHCreateItemFromParsingName`+`SHGetIDListFromObject`,
  even though both are absolute PIDLs.
- Side finding: shell parsing names need native `\`, not Qt-style `/`
  (`SHCreateItemFromParsingName` observed failing with `E_INVALIDARG`).
- `IWebBrowser2::Navigate2(file path)` also failed to produce a selection here (HRESULT `800706F4`).
- The WIP 3-stage fallback (SelectItem → Refresh+retry → same-window Navigate2) all failing ends in
  `NoWindow`, so on the surface it looks like "new window opens, reuse doesn't".

## Repro
- Extend `reveal_window_test` with cases D/E/F (unselected window, reselect, minimized window) plus
  case G (Navigate2 mechanism) to reproduce. That extension lives only in the backup zip (uncommitted).
- Temporary stdout logging of the `SHCreateItemFromParsingName` HRESULT and `ILFindChild` result
  proved effective (stderr is not captured by the test harness).

## Next-step candidates
1. Obtain `basePidl` via the `SHGetIDListFromObject` family too (folder `IShellItem`) to unify PIDL
   forms, then retry `ILFindChild`.
2. Bypass via `IFolderView::SelectAndPositionItems` or display-name-based selection.
3. Consider matching inactive tabs (Windows 11 tabbed Explorer) during window enumeration.
4. Log the failing stage to `scanLog` for diagnosis (draft exists in the WIP).
