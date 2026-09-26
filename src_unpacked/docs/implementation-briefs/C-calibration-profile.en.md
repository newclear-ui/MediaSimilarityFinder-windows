# Implementation Brief — Node C Calibration / INI Performance Profile

## 1. Purpose

Node C prevents the Scheduler from starting from zero knowledge on every run by measuring CPU/GPU capability and major costs briefly, then preserving relatively stable values in an INI Performance Profile.

Node C is not a completely new architecture. It reuses and extends existing profile and benchmark concepts so the Scheduler can consume them.

Core idea:

```
reuse existing profile concepts
+
extend measurements
+
profile identity
+
confidence
+
versioning
+
Scheduler initial estimate
```

## 2. Long-term Profile vs Live Runtime State

### Long-term Profile

Stored in INI:

- CPU fingerprint baseline
- GPU fingerprint baseline
- resize/conversion baseline
- transfer baseline
- decode baseline
- queue baseline

### Live Runtime State

Observed during the current run:

- recent throughput
- current load
- current queue
- current transfer
- current backend state

Priority:

```
INI Profile
   ↓
Initial Estimate
   ↓
Live Measurement
   ↓
Scheduler Decision
```

The INI profile never overrides live runtime state.

## 3. Profile Identity

Minimum information:

- profileId
- profileVersion
- createdAt / updatedAt
- confidence
- CPU fingerprint
- GPU fingerprint
- backend identity
- relevant driver/backend information

If the environment no longer matches the profile, reuse should be limited or begin with reduced confidence.

## 4. Calibration scope

Do not run a long comprehensive benchmark from the start.

Candidate measurements:

### CPU
- fingerprint throughput

### GPU
- fingerprint throughput
- batch throughput

### Common
- CPU↔GPU transfer
- resize/conversion

### Decode
- CPU decode baseline
- hardware-decode feasibility

### Queue
- queue latency

Unavailable measurements remain explicit measurement states.

## 5. Measurement state

Reuse the Node A state system:

- measured
- not_measured
- not_available
- partial
- failed
- fallback

Never write an unmeasured value as numeric zero.

## 6. Calibration strategy

### Initial calibration

Run when no profile exists or the profile identity does not match the current environment.

### Opportunistic recalibration

Run a short recalibration when a profile exists but runtime observations repeatedly diverge.

Do not force a long calibration on every run.

## 7. Confidence

Store confidence with the profile.

Driver/backend changes, hardware changes, engine changes, repeated runtime deviation, and calibration failure may lower confidence.

## 8. Profile versioning

Increment profileVersion when the profile structure or measurement meaning changes.

Keep these concepts separate:

- Application Version
- Engine Version
- Database Version
- Benchmark Schema Version
- Profile Version

## 9. Scheduler integration

Node C outputs the Scheduler's initial estimate.

```
Profile
  ↓
CPU/GPU initial capacity
transfer/conversion estimate
  ↓
Scheduler
  ↓
Live measurement
  ↓
updated decision
```

A stale profile must never override live runtime measurements.

## 10. Profile updates

Do not immediately overwrite a profile because of one anomalous run.

The update structure should be able to track:

- old profile
- new profile
- reason
- confidence before
- confidence after

## 11. Out of scope

- Scheduler policy redesign
- large-scale Pipeline changes
- worker topology changes
- NVDEC optimization
- video planner
- additional GPU backends

## 12. Completion criteria

- profile creation
- profile reuse
- profile identity validation
- confidence recording
- stale/invalid profile identification
- Scheduler initial-estimate integration
- live runtime precedence
- calibration does not change search correctness
- explicit measurement states
- CPU-only operation remains valid

The purpose is not perfect prediction; it is **a reasonable next-run initial estimate plus a foundation for runtime correction**.
