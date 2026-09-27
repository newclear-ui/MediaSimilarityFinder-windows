# Standard D8 Dataset — `test_sample_img_vid`

> This document is intentionally **not** inside the dataset root. Any file
> placed under that root becomes part of the dataset manifest and therefore
> changes the fingerprint, so the root contains media and nothing else.
> `scripts/prepare_dataset.ps1` deletes and recreates the root on every run,
> which would remove a README kept there.

## Purpose

Gives D4b (overlap decision), Full D3 (queue decision), and D8 (end-to-end
validation) one **reproducible input** so two runs can be compared honestly.
Before this existed, "compare under the same conditions" was not expressible.

## This directory is a placeholder

The dataset is **generated, not committed**, because this repository keeps
**no binary assets** — every tracked file is text, and every test in the
suite already generates its own media at runtime.

## How to generate

```powershell
cd src_unpacked
.\scripts\prepare_dataset.ps1 -Root ..\test_sample_img_vid
```

The generator reads no clock, no environment, and no random source. Every
byte is a pure function of `(seed, x, y)`, so repeated runs produce
byte-identical files.

## Composition (v1 — image only)

| Group | Files | Content |
| --- | --- | --- |
| `images/exact/dupGG_MM.bmp` | 12 groups × 4 = 48 | Byte-identical members inside a group; distinct seeds per group |
| `images/varied/vNN.bmp` | 12 | One distinct seed each, so no two are identical |
| **Total** | **60** | |

- Format: 8×8 24-bit uncompressed BMP (246 bytes each), the format the rest
  of the test suite hand-rolls. WIC reads it through the real production
  path, and the scanner recognises `.bmp`.
- `exact` exercises the duplicate/aggregation path; `varied` exercises
  classification and threshold behaviour.

## Expected fingerprint

```
fingerprint          f01d5c77ccd777057494cefc5ad817caea567b40341fa1925f53bb04ec5b2d7c
fingerprintVersion   1
fileCount            60
totalBytes           14760
state                measured
```

## How to validate

```powershell
cd src_unpacked
.\build-windows-cpu\Release\msf_dataset_report.exe ..\test_sample_img_vid
```

Prints the identity and writes `test_sample_img_vid.fingerprint.json`
**beside** the dataset.

Then run a real scan over it and compare against the benchmark JSON:

```powershell
.\build-windows-cpu\Release\msf_dataset_e2e_test.exe ..\test_sample_img_vid <app-dir>
```

Every benchmark JSON from a scan of this root carries
`meta.dataset.fingerprint`. If that value differs from the fingerprint above,
the two runs did **not** use the same input and must not be compared.

### A trap worth naming

While building this, a `README.md` was briefly placed inside the dataset
root. Because the manifest covers *every* file under the root, the file
count went 60 → 61 and the fingerprint changed to
`8654f69b…bafb`. That is correct behaviour, not a bug: a dataset's identity
includes whatever is inside it. Hence this document lives outside the root,
and `prepare_dataset.ps1` clears any stale identity file.

## Fingerprint algorithm (v1)

```
for each regular file under the root:
    entry = canonical relative path + ";" + size + ";" + SHA-256(content)
sort entries by canonical relative path (byte order)
hash("|".join(entries)) with SHA-256 -> 64 lowercase hex chars
```

- Relative paths use forward slashes, no case folding (case-sensitive
  filesystems must not merge distinct files).
- The **whole** file is read. The scanner's existing per-file `quick()` hash
  covers only the first 64 KiB and serves file identity, not dataset identity.
- The fingerprint contains **no** absolute path, timestamp, process id,
  hardware name, or run id. Two copies of the same content under different
  roots therefore share a fingerprint — this is exactly what test C asserts.
- `fingerprintVersion` identifies the *algorithm*, not the data. It is raised
  when the algorithm changes.

## Not included: video

Video is deliberately excluded from v1. Every existing video fixture in this
project shells out to `ffmpeg ... lavfi testsrc`, and ffmpeg output is **not
byte-reproducible** (encoder and creation-time metadata are muxed into the
container). No current test passes `-fflags +bitexact` or `-map_metadata -1`.

Adding video without that guarantee would make the fingerprint drift between
runs and defeat the entire purpose of this step.

```
D8a dataset foundation        = this directory (image only)
future video fixture expansion = separate step, once bitexact is guaranteed
```

## Regenerating after a change

Any change to the composition table above or to the pixel formula changes the
fingerprint **on purpose**. Regenerate, re-run `msf_dataset_report`, and
update the expected value in this file and in
`src_unpacked/docs/build-history/0.9.4.21.ko.md` / `.en.md` in the same
commit.
