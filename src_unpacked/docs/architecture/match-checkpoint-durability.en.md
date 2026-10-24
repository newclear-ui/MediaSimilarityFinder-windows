# Match-checkpoint crash durability (design + implemented in 0.9.4.154)

Status: **IMPLEMENTED in 0.9.4.154** — implemented as designed below; the marker path is verified by crash_diagnostics_test section 7. Forced-kill mid-write durability itself remains best-effort (WAL + single-txn semantics unchanged).
Related: `docs/build-history/0.9.4.151.en.md` (C1/C2), `0.9.4.152.en.md` (kill durability declared unproven), `0.9.4.153.en.md` (P1-A/P1-B exit paths).

## 1. Problem

The in-scan match set is persisted in three places: the 5-second checkpoint (inside `ScanWorker::onMatch`),
the analyze-slice boundary (`control_.analyzeCheckpoint`), and the final persist (end of `run()`).
Only the last runs outside a transaction (autocommit); the first two are written with SAVEPOINT inside
the scan transaction (`Database::saveMatches`, `txActive_` branch, 0.9.4.151 C2).

A forced process kill (`kill -9`, power loss, AV termination) rolls back every uncommitted transaction.
So **matches checkpointed after the last engine commit are lost**. File rows share the same commit
boundary, so the loss window is identical: "since the last engine commit". Match durability therefore
trails file-row durability by at most one checkpoint interval.

What the current code guarantees (verified):
- Final persist + baseline invalidation on normal/cancel/failed exits (`invalidateMatchBaseline`, 0.9.4.152–153).
- Engine file-row checkpoint commits (0.9.4.78 and later).
- Nested-save consistency (rolls back together, 0.9.4.151 C2).

What it does not guarantee (kept as unproven):
- Any way for the next scan to know how far the killed scan got. With a valid baseline,
  the next A/AB skips full verification and the lost matches stay lost forever
  (`unchanged` verdicts, since the file rows survived and change detection never fires).

## 2. Design D1 (recommended): crash marker

At scan start, write `meta.scan_dirty='1'` in its own autocommit transaction.
Delete it on every settled exit: normal completion, user cancel, handled failure.
If the key is still present at the next scan start, the previous run died mid-scan:
run one full verification regardless of the baseline, then restamp.

- Cost: two small writes per scan (start/clear). No extra match verification.
- No conflict with existing machinery: the meta table is schemaless key-value, so no schema bump.
- Complementary to the 0.9.4.152/153 invalidation (known-incomplete exits vs unknown forced kills).
- Rollback-safe: the marker write is its own transaction, never entangled with the scan transaction.

## 3. Rejected alternatives (kept on record, never deleted)

- D2 co-commit: force an engine commit on every match checkpoint. Equal or worse effect than D1
  with higher wall time from commit frequency. NOT ACCEPTED (no benefit for the cost).
- D3 separate DB + autocommit checkpoints + merge on completion: best durability, but doubles
  consistency/recovery/backup concerns across two databases. DEFERRED (revisit only if D1 proves insufficient).
- Per-match real-time fsync: cannot be adopted without measurement on million-pair scans. REJECTED.

## 4. Verification plan (on implementation)

1. Start a large scan on an isolated fixture index, then force-kill (`TerminateProcess`).
2. Confirm the restarted scan full-verifies (look for `forceFullPass` in the backend log).
3. Confirm the match set is fully restored, in both the DB (`loadMatches`) and the GUI (re-applied view).
4. Confirm no marker remains after a clean exit (no gratuitous full verification next time).
5. Overhead: measure the two marker writes' contribution to scan wall time (expected: below measurement noise).

## 5. Out of scope

- This document includes no implementation. On adoption, proceed in a separate version with the
  `scan_dirty` key, writes/clears on the three exit paths, and a regression test with injected kills.
- Test Mode / re-verify / Load paths are out of scope (they follow separate transaction contracts).
