# Leading-Blank (Leader) Insertion Discrimination Field Report

- Target build: 0.9.4.171 (default path vs `MSF_VIDEO_STRUCTURE=full` structured path)
- Dataset: `D:\Temp\OpenCodeWork\msf_video_field/` — 5 long-form (L01-L05) + 50 shorts (f01-f50), each with orig / reenc (720p mpeg4) / flip (hflip) / lead (prepended black leader)
- Leader lengths: L01 45 s, L02 30 s, L03 60 s, L04 30 s, L05 45 s / f01-f25 30 s, f26-f50 60 s
- Manifests: `manifest.csv` (55 orig rows) + `lead_manifest.csv` (55 lead rows)
- Verdict thresholds: 50 (candidate gate) / 87.5 (production duplicate verdict)

## 1. Purpose

Measure whether content with an ad/pre-roll-style prepended segment (a) still matches as the same content (recall), and (b) does not merge different content (precision).

## 2. Method

Temporary harness `video_long_cmp_test` (uncommitted, CMakeLists registration reverted after runs): per-file fingerprint builds (VideoBuildStats recorded) + all-pairs `video_similarity` scores. L 20 files x 2 modes, f 100 files x 2 modes, f 150 files (orig/lead/flip) x 2 modes. Fresh DBs, HW defaults.

## 3. Results

### 3.1 Long-form L01-L05 (leader vs own content)

| Pair | Default | Structured | Verdict (50/87.5) |
|---|---|---|---|
| L01-lead x orig/reenc/flip | 7.7 | 7.7 | miss/miss |
| L02-lead x reenc/flip (same 720p SDR pipeline) | 52.8/52.5 | 51.3/51.4 | DUP/miss |
| L02-lead x orig (HDR) | 13.5 | 14.3 | miss/miss |
| L03-lead x orig/reenc/flip | 6.1 | 8.3-8.4 | miss/miss |
| L04-lead x orig/reenc/flip | 12.8 | 10.8 | miss/miss |
| L05-lead x orig/reenc/flip (same geometry) | 48.8 | 48.8 | miss/miss (borderline) |
| 10 L lead cross pairs | all 0.00 | all 0.00 | separated |

### 3.2 Shorts f01-f50

| Metric (default -> structured) | @50 | @87.5 |
|---|---|---|
| lead-vs-orig same DUP | 20/50 -> 18/50 | 5/50 -> 9/50 |
| lead-vs-flip same DUP | 20/50 -> 18/50 | — (mostly miss) |
| orig-vs-flip same DUP (validity) | 50/50 -> 50/50 | 48/50 -> 47/50 |
| lead-lead cross FPs (1225 pairs) | 282 -> 236 | **16 -> 0** (max 95.50 -> 80.06) |
| lead-flip/lead-orig cross FPs (2450 ordered) | 52/50 -> 15/18 | **0 -> 0** |
| flip-flip cross FPs | 0 -> 0 | 0 -> 0 |

### 3.3 Reference: HDR vs SDR, same episode (L02 vs L03)

Originals 0.00; SDR derivatives (L02-reenc vs L03-reenc) 0.00/5.05 — not the same content (different cuts/grades, 4.7 s length delta). The HDR-transfer effect alone cannot be isolated (confounded). L02 orig x SDR derivatives 53.9 -> 63.8 (DUP at 50, miss at 87.5).

## 4. Miss analysis (same -> missed)

1. **Unabsorbed start offset**: a 30-60 s leader shifts the whole timeline. The matcher anchors the first kept correspondence and enforces monotonicity only, so it cannot absorb the offset and alignment collapses. Even with identical pipeline and geometry, 99.9 collapses to 6-53 (L05 45 s leader 48.8; L03 60 s leader 6.1).
2. **Thin evidence**: shorts carry 1-13 s of content behind 30-60 s of blank, so 80-95% of the file is void. Only 1-2 of 8-11 kept frames hold real content.
3. Conclusion: ad-inserted copy detection-rate loss (recall gap). V4 insertion alignment / V6 partial-clip scope.

## 5. False-alarm analysis (different -> merged)

1. **Identical fingerprints of the shared black prefix**: every leader file starts with byte-identical black. Default kept selection (segmented, no variance floor) stores flat frames, so unrelated videos share identical black hashes. Strong alignment on the common stretch buries main-content differences and passes 50 (f02-f06 95.50).
2. **Structured mitigation**: denser plan + structure-windowed selection stores less of the shared flat part — FP@87.5 16 -> 0, f02-f06 95.5 -> 70.7. Full removal needs the P1 floor promoted into default.
3. Zero at the production threshold (87.5) — current shipping bar safe. Caution on leader-sharing pairs when operating at 50.

## 6. Measured structured trade-off

- Precision gain: leader cross FP@87.5 16 -> 0, @50 282 -> 236 (f) / 52 -> 15 (f lead-flip cross).
- Recall cost: lead-vs-orig DUP@50 20 -> 18 (f). L-set easy pairs unchanged (delta <= 0.01).
- V2 confidence < 0.30 on all files, so kept selection fell back to uniform. Zero live structure-window selections.

## 7. Measurement issues (no product change)

1. narrow-argv Korean-path fail-fast (0xC0000409): the harness assumed UTF-8 argv (CP949 bytes). Worked around with a UTF-8 file list; 15/15, 20/20, 100/100, 150/150 all succeeded. Product GUI/Backend (UTF-8 IPC) unaffected.
2. Exotic timebase (f02: 13200000/440897) broke the mpeg4 encoder — short leaders unified to 30 fps (documented).
3. Portrait/small clips broke width-fixed scaling + pad — fit-inside 1280x720 + pad (distortion-free).
4. First failure left a 0-byte file that fooled the skip logic — added a sub-1 MB regeneration rule.

## 8. Verdict and follow-up

- Accuracy gates stay unpassed: leading-ad recall gap + no same-content HDR pair secured. No default-path change (measurement-only).
- Follow-up: V4 insertion alignment, V6 partial-clip, P1 variance-floor default promotion, HDR transfer-aware normalization.
- Reproduction: dataset paths above + 0.9.4.171 temporary harness (§2) + derivative recipes in this report.

## 9. Source evidence

- `docs/worklog/0.9.4.en.md` — long-form field evaluation 1, leading-blank discrimination, 50 short leaders, leader-vs-original/flip false-alarm check.
- Measurement TSVs/DBs/logs: `D:\Temp\OpenCodeWork\msf_video_long/` (lead_default/full, lead50_default/full, leadflip_default/full).

## 10. Appendix: why a miss differs from a false alarm

Superficially both "drop out of the duplicate verdict", but in a duplicate-cleanup product the harm points in opposite directions.

- **Miss (False Negative, not found)**: same content stays out of the group. Files remain intact and listed. Harm is manual effort; **data is safe**.
- **False alarm (False Positive, wrongly merged)**: different content gets grouped as duplicates. Since cleanup means deletion, the user may delete a unique file believing it a duplicate. **Unique-data deletion = irrecoverable**.

That is why the production threshold 87.5 is precision-first (conservative). This evaluation matches the design: zero false alarms @87.5, many misses. Lowering to 50 converts misses into false alarms (52 ordered leader-sharing FPs). One line: **a miss leaves trash behind; a false alarm throws away the original**.

## 11. Separated cause-effect record (miss vs false alarm)

### Miss — cause -> effect

- **Cause C1 (unabsorbed time shift)**: the 30-60 s leader shifts the whole timeline by +D. The matcher anchors the first kept correspondence and enforces monotonicity only, so it cannot absorb the start offset and alignment collapses. Evidence: same-pipeline L05 48.8 (45 s leader), L03 6.1 (60 s leader) — longer leader, lower score.
- **Cause C2 (evidence dilution)**: kept frames are split-selected across the whole file timeline. With the leader occupying 80-95%, only 1-2 of 8-11 kept frames hold real content, so comparison evidence itself is scarce.
- **Cause C3 (L02 only, format)**: HDR PQ 10-bit vs SDR tonemap conversion loss pre-exists the leader (L02-orig vs L02-reenc 53.9).
- **Effect**: same-content miss (recall loss). Ad-inserted copy detection rate drops. Harm is effort and reversible.

### False alarm — cause -> effect

- **Cause C1 (identical values)**: every leader file starts with byte-identical black. Default kept selection (segmented, no variance floor) stores flat frames, so unrelated videos share many identical black hashes in their kept sets. Strong common-stretch alignment buries main-content differences and passes 50 (f02-f06 95.50).
- **Cause C2 (threshold sensitivity)**: the common part inflates scores to roughly 70-95. It fires at 50 and is filtered at 87.5 (f lead-flip cross).
- **Effect**: different-content merge (precision loss). Harm is unique-data deletion risk and irreversible.

### Causal contrast

| | Miss | False alarm |
|---|---|---|
| Input | same content + changed front | different content + identical front |
| Trigger | time shift (offset) | identical values (black) |
| Matcher failure | anchored start, no offset absorption | flat-frame storage + common-stretch alignment |
| Score pattern | 99.9 -> 6-53 collapse | 0 -> 50-95 inflation |
| Structured relief | none (fallback on all files) | FP@87.5 16 -> 0 (floor + dense plan) |
| Harm | lost cleanup opportunity (reversible) | unique-data deletion risk (irreversible) |
