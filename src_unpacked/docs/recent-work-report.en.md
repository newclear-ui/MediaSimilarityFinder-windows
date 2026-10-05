# Report on the Three Most Recent Important Changes (2026-10-04)

Baseline commit: `619f74a` (synchronized with `origin/main`)
Version: `0.9.4.45` / HEAD `619f74a` / CPU CTest 102/102 / GPU CTest 103/103

This document is a single-place summary of three recently completed important
changes. The detailed evidence for each already lives in Build History / Work
Log; this document only gathers **what was done and what the state is now**,
without duplicating that evidence.

---

## Change 1 — `--version` console output fix

Commit `619f74a` / file `src_unpacked/gui/main.cpp`

### What was done

`MediaSimilarityFinder.exe --version` **printed nothing from PowerShell.** It
worked correctly from CMD.

The cause was the predicate `streamIsRedirected()` inside
`gui/main.cpp::attachParentConsole()`. Because the executable is GUI subsystem
(`WIN32_EXECUTABLE TRUE`), a null `GetConsoleWindow()` leads to
`AttachConsole(ATTACH_PARENT_PROCESS)`, and that path skips the `CONOUT$` reopen
when it believes the streams are redirected.

The bug was that this predicate **mistook "the stream cannot be written to" for
"the stream is redirected".**

| Condition | Previous verdict | What it actually meant |
|---|---|---|
| `fd < 0` | `true` (wrong) | no descriptor = **unusable** |
| `_get_osfhandle()` is `-1` or `0` | `true` (wrong) | no usable OS handle = **unusable** |
| `GetFileType()` is `FILE_TYPE_UNKNOWN` | fell through to the `FILE_TYPE_DISK`/`FILE_TYPE_PIPE` test, so `false` | invalid handle = **unusable** |

So two cases were wrongly reported as `true`. `FILE_TYPE_UNKNOWN` already resolved
to `false`, but only by falling through the final comparison rather than by a
deliberate check, so this fix also made that path explicit.

So a PowerShell launch whose `stdout`/`stderr` were not real output targets was
classified as redirected, `AttachConsole` was skipped, and `std::cout` had
nowhere to write.

### What the fix was

All three cases are now explicit `false` (unusable). Only a genuinely usable
`FILE_TYPE_DISK`/`FILE_TYPE_PIPE` counts as redirected.

Why that matters: it keeps the original design intent — **real redirection must
always be preserved** — and removes only the misdetection. This is not the same
as calling `AttachConsole` unconditionally. File/pipe redirection is preserved
as-is, and only genuinely unwritable streams fall through to
`AttachConsole` + `CONOUT$`.

### Result

- `--version`, `--help`, `cmd /c`, and a bad option (stderr + EXIT=2) all correct.
- OS-level redirection (`cmd /c "exe --version > file"`) records 45 bytes.
- `Start-Process -Wait -RedirectStandardOutput`: 45 bytes, EXIT=0.
- No impact on search/index/comparison logic or GUI behavior.

### One caveat found during verification

`--version > file` yielding 0 bytes from PowerShell was observed alongside this.
**That is not a regression introduced by this fix.** It reproduces identically on
the pre-fix binary and leaves `$LASTEXITCODE` empty. The cause is that PowerShell
does not wait for GUI-subsystem executables.

Console output verification should therefore use **OS-level redirection** as the
criterion (`cmd /c` or `Start-Process -Wait`).

---

## Change 2 — CUDA `C4819` encoding warning removal

Commit `619f74a` / file `src_unpacked/CMakeLists.txt`

### What was done

GPU build logs repeatedly emitted MSVC `warning C4819` from the CUDA headers
(`driver_types.h`, `cuda_runtime_api.h`).

**Important: this was neither a CUDA syntax error nor a link error.** It is an
encoding warning: code page 949 cannot represent non-ASCII characters inside the
CUDA headers. The build itself succeeded and GPU CTest passed 103/103.

The cause was the scope of `/utf-8`. Previously there was only this:

```cmake
add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)
```

So `/utf-8` reached MSVC C++ compilation only. CUDA uses a separate host
compiler and never received the flag.

### What the fix was

`nvcc` does not accept `/utf-8` directly, so it is forwarded to the MSVC host
compiler via `-Xcompiler`.

```cmake
add_compile_options($<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CXX_COMPILER_ID:MSVC>>:-Xcompiler=/utf-8>)
```

**The first attempt failed, and that failure is worth recording.** It initially
used `COMPILE_LANG_AND_ID:CUDA,MSVC`, which did not match: CUDA reports compiler
id `NVIDIA`, while `MSVC` is the frontend variant. The language is CUDA but the
id is not MSVC, so the two cannot be asserted at once. Combining
`COMPILE_LANGUAGE:CUDA` with `CXX_COMPILER_ID:MSVC` puts
`-Xcompiler="/EHsc -Ob2 /utf-8"` on the actual nvcc command line, which was
verified.

### Result

- Forced recompile of `cuda_backend.cu`: `C4819=0` / `warning=0` / `error=0`.
- `msf_cuda.lib` produced normally.
- GPU full build + CTest **103/103 PASS**, zero warnings.
- CUDA architectures (`compute_75/86/89`) and runtime behavior unchanged.
- No source/ABI change. Build option only.

---

## Change 3 — XMP Orientation Fallback implementation + independent review correction

Commits `97db24f` (implementation) + `392a4c2` (review correction and
`color_thumb` pre-register)

Related documents: `docs/implementation-briefs/I-xmp-orientation-fallback.{ko,en}.md`

### What was done — stage 1: implementation

Fixed files that appeared rotated because EXIF Orientation was absent.

- EXIF `VT_UI2` value `1..8` applies EXIF (XMP is not queried at all).
- If EXIF is absent, fails, has the wrong type, or is out of range, XMP is tried.
- XMP uses the WIC path `/xmp/tiff:Orientation`. It was **measured as a
  `VT_LPWSTR` string**, and integer VARIANTs are normalized as well.
- On an EXIF/XMP conflict, **EXIF wins**.
- Both output paths (`decodeBoth` fingerprint, color display lane) **share one
  resolver**, so transform semantics cannot diverge.
- An invalid XMP is treated as identity. No telemetry field was added.

### What was done — stage 2: independent review and correction

An independent review pointed out that "implemented" does not mean "passed", so
the fixture was actually strengthened. It grew from **14 checks to 38 checks**,
filling items the review had marked **NOT VERIFIED**.

| Review item | At review time | This result |
|---|---|---|
| mapping `1/3/6/8` | PASS | PASS kept |
| mapping `2/4/5/7` | **NOT VERIFIED** | **PASS** (H2/H4/H5/H7) |
| 90/270 direction | geometry `32x16<->16x32` only, so unverified | **PASS** (quadrant means discriminate direction) |
| 180 transform | geometry unchanged, so unverified | **PASS** (left/right swapped quadrant means) |
| EXIF out-of-range fallback | unverified | **PASS** (I9: EXIF=9 → XMP=6 applies) |
| EXIF wrong-type fallback | unverified | **PASS** (IT: type=ASCII → XMP=8 applies) |
| full scan regression | undemonstrated | **DEFERRED** |
| standard dataset XMP coverage | reported only | **reported only, kept** |

The fixture BMP became a **four-quadrant image** (levels `0/85/170/255`). The old
fixture had only two left/right halves, so top/bottom discrimination was
impossible. BMP rows are bottom-up, so the assertions use decoded-image
coordinates. `5` and `7` share geometry with `6` and `8`, so instead of claiming
a direction they are pinned by **byte identity with the EXIF path through the
same transform** (`XMP=5` == `EXIF=5`, `XMP=7` == `EXIF=7`).

### Current verdict — this distinction matters

```text
XMP code implementation        PASS
XMP fixture (1..8 + pixel)    PASS
XMP real-dataset coverage     NOT_AVAILABLE  (standard dataset has 0 XMP files, unverified)
full Search/Scan regression   DEFERRED       (depends on S4 functional acceptance / S5 benchmark)
XMP production acceptance     CONDITIONAL
```

The fixture proves implementation correctness (1..8 plus pixel direction plus
EXIF invalid fallback), but without full scan regression and real-dataset
coverage the production acceptance is CONDITIONAL. `docs/build-history/0.9.4.45.*`
is not rewritten retroactively; the correction is recorded in this document and
in the Work Log.

### Side output — `color_thumb` audit pre-register

The same commit added
`docs/implementation-briefs/I-color-thumb-no-ffmpeg-classification.*`.
**Audit only; no production correction was made.**

Core conclusion: classification is a single extension rule and is **identical
across all three FFmpeg states.** The absence of decoder capability must not
change the media type; that is the contract candidate.

Confirmed risks:
- **R1 (high)**: `color_thumb_test` always fails in a no-FFmpeg build
  (unconditional CMake registration, no skip handling, `frameAtColor`
  unconditionally `false`). This classifies the previously unclassified
  `exit 5` from worklog run 082.
- **R2 (medium)**: `kindOf()` (extension) and DB `x.kind` are dual sources of
  truth with no cross-check.
- **R3 (medium)**: the extension list is duplicated four times (`scanner`, three
  places in `monitor`, GUI).
- **R4 (medium)**: the shell thumbnail overwrites the engine color thumbnail
  without an `isNull()` guard, polluting `thumbStatEngine_`.
- **R5/R6 (low)**: overstated configure message / magic-number `MediaKind`
  mapping.

---

## Overall Current State

| Item | Status |
|---|---|
| Version | `0.9.4.45` (no bump) |
| Commit | `619f74a`, synchronized with `origin/main` |
| CPU CTest | 102/102 PASS |
| GPU CTest | 103/103 PASS |
| CUDA warnings | 0 |
| XMP production acceptance | CONDITIONAL |
| `color_thumb` production correction | NOT performed (pre-register only) |
| S4 final GUI visual/save acceptance | DEFERRED |
| S5 product benchmark | DEFERRED |
| S6 | DEFERRED |
| NVDEC production adoption | DEFERRED |

## Remaining Candidates / Next Steps

- Add a dedicated console-output regression test (currently manual verification only).
- Decide whether nvcc warnings join the release gate.
- Implement the `color_thumb` R1 fixture plus skip/pass handling; R2 through R6
  stay separate decisions.
- XMP full scan regression — after S4 functional acceptance completes.
- The backup zip rule (`backup_src.ps1` / `package_portable.ps1`) was not run for
  this change. Both scripts require zero uncommitted tracked changes, but the two
  `.gitattributes` files show as `M` purely from CRLF normalization, so a decision
  is needed first.

## Related Documents

- `docs/build-history/0.9.4.45.{ko,en}.md`
- `docs/worklog/0.9.4.{ko,en}.md`
- `docs/implementation-briefs/I-xmp-orientation-fallback.{ko,en}.md`
- `docs/implementation-briefs/I-color-thumb-no-ffmpeg-classification.{ko,en}.md`
- `docs/development-progress.{ko,en}.md`