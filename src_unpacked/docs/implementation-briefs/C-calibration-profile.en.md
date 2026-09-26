# Implementation Brief — Node C Calibration / INI Performance Profile

## 1. Purpose

Node C prevents the Scheduler from starting every run from hardware-only baseline knowledge. It briefly measures relatively stable capability/cost values, stores them in an INI Performance Profile, and exposes them as the next run's initial estimate.

Node C does not redesign the Node B Scheduler policy.

Core flow:

    Environment / backend identity
            |
            v
       Profile load
            |
       +----+----+
       |         |
     usable    stale/invalid
       |         |
       |         v
       |   Short Calibration
       |         |
       |         v
       |    Profile candidate
       |         |
       +----+----+
            |
            v
      Initial Estimate
            |
            v
       B Scheduler
            |
            v
      Live Measurement
            |
            v
    Decision / deviation check
            |
       +----+----+
       |         |
    stable   repeated deviation
       |         |
       v         v
  keep profile  opportunistic calibration

The governing rule is: Profile = initial estimate; Live Runtime = authoritative current-run evidence.

## 2. Responsibilities and boundaries

### C owns

- Performance Profile data model
- INI persistence
- profile identity / version / confidence
- stale / invalid classification
- short initial calibration
- opportunistic recalibration
- Profile → Scheduler initial-estimate bridge
- calibration/Profile lifecycle through the existing Benchmark CalibrationTelemetry
- CPU-only / GPU-off operation

### C does not own

- Scheduler policy redesign
- worker / queue / barrier topology
- Node D pipeline optimization
- NVDEC implementation
- additional GPU backend
- video planner
- search-verdict algorithm changes

The Node B decision → execution binding remains unchanged.

## 3. Existing code integration

Node A already provides:

- BenchmarkRecorder
- CalibrationTelemetry
- MeasureState
- benchmark schemaVersion 1
- CPU/GPU backend abstraction
- Scheduler telemetry

CalibrationTelemetry already contains confidence, cpuThroughput, gpuThroughput, resizeThroughput, decodeThroughput, transferCostMs, queueLatencyMs, profileId, and profileVersion.

C therefore does not create a second benchmark telemetry model. It connects the existing CalibrationTelemetry to real calibration results and the Profile lifecycle.

C1 must not bump benchmark schemaVersion. Existing calibration fields are populated first. A schema change is considered separately only if genuinely new JSON fields are required.

## 4. Profile storage

Initial portable location:

    <application directory>/
      Index/
        PerformanceProfile.ini

This is an application-environment performance profile, not a media-root index profile.

Use the existing portable Index area. Do not introduce a new vcpkg or machine-global dependency.

A missing Profile is a normal profile-not-present state.

## 5. Profile data contract

Initial profileVersion is 1.0.

Conceptual INI structure:

    [meta]
    profileId=
    profileVersion=1.0
    createdAt=
    updatedAt=
    confidence=
    engineVersion=
    benchmarkSchemaVersion=1

    [identity]
    cpuFingerprint=
    gpuFingerprint=
    backend=
    driver=

    [cpu]
    fingerprintThroughput=
    fingerprintState=

    [gpu]
    fingerprintThroughput=
    batchThroughput=
    fingerprintState=
    batchState=

    [transform]
    resizeThroughput=
    resizeState=

    [transfer]
    bandwidthMBps=
    costState=

    [decode]
    cpuThroughput=
    cpuState=
    hardwareFeasibilityState=

    [queue]
    latencyMs=
    state=

    [lastUpdate]
    reason=
    oldProfileId=
    oldConfidence=
    newConfidence=

Exact key spelling and serialization are fixed in C1 with the implementation.

### Storage rules

- Measurement state and numeric value are separate.
- Unmeasured values are never stored as numeric zero.
- Preserve not_measured, not_available, partial, failed, and fallback.
- A metric without a measurement may omit the numeric key or represent the absence explicitly.
- Write the complete new Profile to a temporary file, then atomically replace the target.
- Profile write failure must not fail the search.

## 6. Profile identity

Minimum identity:

- CPU fingerprint
- GPU fingerprint
- backend identity
- driver identity
- engine compatibility information
- profileVersion

### Reuse policy

Exact match:
Reuse normally when CPU/GPU/backend identity matches and profileVersion is compatible.

Soft mismatch:
For compatible driver or engine changes where measurement semantics remain valid but performance may shift, keep the Profile readable, lower confidence, and prefer short calibration.

Hard mismatch:
When the CPU hardware fingerprint, GPU hardware fingerprint, or backend identity changes, or profileVersion is incompatible, do not use the old Profile directly as the initial estimate. Run a new calibration.

### Stale

Initial policy uses maxAgeDays = 30.

- Do not delete a Profile only because it is older than 30 days.
- Stale Profiles lower confidence and prefer short calibration.
- Repeated runtime deviation may request opportunistic recalibration regardless of age.

## 7. Confidence

Confidence is a long-term Profile trust value from 0.0 to 1.0.

Initial policy:

- a new Profile with all required initial measurements succeeds with high confidence
- partial calibration starts lower according to measurement coverage
- soft mismatch lowers confidence
- stale lowers confidence
- failed calibration keeps the old Profile and lowers confidence
- repeated runtime deviation lowers confidence
- consistent recalibration may raise candidate confidence again

Exact numeric adjustments are fixed during C2/C3 with tests. C1 fixes the storage and state contract only.

## 8. Calibration scope

### C measures now

CPU:
- fingerprint throughput

GPU:
- fingerprint throughput
- batch throughput

Common:
- transfer cost / bandwidth
- resize/conversion throughput

Decode:
- existing Software FFmpeg CPU decode baseline

### C records state but does not fabricate yet

Hardware decode:
Before Node F/NVDEC capability exists, C does not invent hardware-decode performance. Record not_available or fallback where appropriate.

Queue latency:
Before Node D provides real queue producer/consumer telemetry, do not synthesize queue latency. Record not_available or not_measured.

This boundary prevents C from pulling D or F forward.

## 9. Calibration strategy

### Initial calibration

Run when:

- no Profile exists
- Profile identity is a hard mismatch
- profileVersion is incompatible
- stale or low-confidence reuse is inappropriate

Do not run a long comprehensive benchmark. Use a set of short probes.

Principles:

- bounded workload
- explicit units
- explicit measurement state
- no full media-root rescan
- calibration failure must not become search failure

Exact probe sizes and time budgets are fixed in C2 and pinned by tests.

## 10. Scheduler initial-estimate contract

A loaded Profile must never overwrite live measured rates.

Precedence:

    1. current live measured rate
    2. valid profile baseline
    3. hardware/static baseline
    4. unknown / fallback

Keep Profile values separate from Node B live throughput inputs.

Recommended shape:

    SchedulerHardware
       |
       +-- live cpuRate / gpuRate
       |
       +-- profile initial estimate
       |      +-- cpuRate
       |      +-- gpuRate
       |      +-- transfer bandwidth
       |
       +-- hardware baseline

C adds/connects only Profile initial-estimate fields. It does not change B's share formula, hold, kill-band, or Resource Mode policy.

When live rate is valid, it always wins over Profile.

## 11. Profile update policy

Never replace an existing Profile because of one anomalous run.

The update record keeps:

- old profileId
- new profileId
- reason
- confidence before
- confidence after

The initial implementation stores lastUpdate metadata inside the Profile.

A full multi-revision history is a separate future design if needed.

## 12. Opportunistic recalibration

A usable Profile may still trigger short recalibration when runtime observations repeatedly diverge.

Initial principles:

- compare live rate with Profile baseline using relative error
- one outlier never triggers replacement
- repeated deviation is required for a short probe
- if a candidate strongly conflicts with the old Profile, do not replace immediately; lower confidence
- promote a candidate to update only after repeat probes are consistent

The exact deviation threshold and repetition count are fixed in C3 with tests.

## 13. State flow

    No Profile
        |
        v
    Initial Calibration
        |
        +---- failed/partial ----> explicit-state Profile
        |
        v
    Profile Ready
        |
        v
    Identity Check
        |
        +---- hard mismatch ----> Recalibration
        |
        +---- stale/low confidence ----> Recalibration preferred
        |
        +---- usable ----> Initial Estimate
                               |
                               v
                         Live Runtime
                               |
                    +----------+----------+
                    |                     |
                 stable              repeated deviation
                    |                     |
                    v                     v
               keep profile      Opportunistic Calibration
                                             |
                                  +----------+----------+
                                  |                     |
                              consistent           inconsistent
                                  |                     |
                                  v                     v
                            update candidate      lower confidence

## 14. C1–C4 stages

### C1 — Profile Foundation

Scope:

- PerformanceProfile data model
- ProfileStore
- INI serializer/loader
- Profile identity representation
- profileVersion
- confidence
- measurement state
- stale/invalid classification
- atomic save
- Scheduler initial-estimate interface
- C1 unit tests

Do not implement actual calibration execution in C1.

Exit criteria:

- profile round-trip
- exact identity reuse
- soft mismatch
- hard mismatch
- stale
- not_measured != numeric zero
- atomic write/read
- CPU-only Profile subsystem

### C2 — Initial Calibration

Scope:

- short calibration runner
- CPU fingerprint measurement
- GPU fingerprint/batch measurement
- transfer/resize measurement
- CPU decode baseline
- calibration result → Profile candidate
- Profile → Scheduler initial-estimate integration
- CalibrationTelemetry measurement recording

Exit criteria:

- no Profile → calibration → Profile creation
- valid Profile avoids unnecessary full calibration
- CPU-only
- GPU-off
- GPU-on
- live measurement overrides Profile
- search-result parity

### C3 — Opportunistic Recalibration

Scope:

- runtime/Profile deviation detector
- repeated-deviation policy
- short recalibration trigger
- confidence update
- candidate Profile update
- stale/invalid reclassification

Exit criteria:

- one outlier cannot replace the Profile
- repeated deviation triggers recalibration
- only consistent candidates update
- failure preserves the old Profile
- live runtime stays authoritative
- search correctness unchanged

### C4 — Calibration Gate

Scope:

- CPU-only
- GPU OFF
- GPU ON/AUTO
- no Profile
- exact Profile reuse
- stale Profile
- hard mismatch
- soft mismatch
- partial/failed calibration
- runtime deviation
- Profile update
- persistence/reload
- benchmark calibration state

Exit criteria:

- complete Profile lifecycle
- Scheduler initial-estimate integration
- live runtime precedence
- measurement-state consistency
- accuracy parity
- regression suite

After C4, Node C is complete and the next gate is D.

## 15. Test design principles

C tests verify relationships and states rather than absolute performance values.

Verify:

- unit and measurement-state consistency
- identity classification
- Profile lifecycle
- precedence
- failure/partial/fallback behavior
- persistence
- search parity

Do not hard-code tests such as "GPU must equal exactly X images/sec".

Measured values belong in benchmark evidence; tests verify contracts and state transitions.

## 16. C / B / D integration

### B

C provides initial estimates only.

B throughput feedback, live load, hysteresis, minimum hold, Resource Mode, and execution binding remain unchanged.

### D

C does not alter queue topology.

If queue latency is not yet measurable, record not_available / not_measured. D can activate that metric after real queue telemetry exists.

### E/F

Do not activate hardware-decode metrics before E/F provides the capability.

## 17. Implementation order

    C0 Documentation / design review
          |
          v
    C1 Profile Foundation
          |
          v
    C2 Initial Calibration
          |
          v
    C3 Opportunistic Recalibration
          |
          v
    C4 Calibration Gate
          |
          v
    Node C Complete
          |
          v
    Node D

Do not preassign version numbers to C1/C2/C3/C4. Increment 0.9.4.x according to actually validated code states.

## 18. Forbidden implementation shortcuts

- treat Profile values as live rates
- encode unmeasured values as numeric zero
- force a full media-root rescan for calibration
- redesign B Scheduler policy
- implement D queue/worker topology early
- implement NVDEC early
- add new GPU backends
- change search correctness
- propagate Profile write failure into search failure

Node C is not about perfect prediction. It provides a reasonable next-run initial estimate and the foundation for runtime correction.
