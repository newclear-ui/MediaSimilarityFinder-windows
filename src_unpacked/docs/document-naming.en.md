# Document Naming and Structure Rules

> This document is the **authoritative naming and role-separation rule** for MediaSimilarityFinder documentation.
> Check it before creating or moving any document.

## 1. Core principles
- Each document has **one canonical name and location** according to its role.
- Korean/English documents normally remain as a **same-basename + language-suffix pair**.
- Do not put dates, author names, or temporary-state labels into filenames. Historical documents that must identify a fixed version are the exception.
- Do not duplicate the same source of truth across multiple higher-level documents.
- When renaming or moving a document, update **all internal links, indexes, structure docs, and the LLM index** in the same change.
- Prefer `git mv` for document moves so Git can preserve rename history.
- Documentation-only restructuring does not bump the product version; record it as a separate `docs:` commit.

## 2. Canonical document classes
| Class | Canonical path rule | Role |
|---|---|---|
| Roadmap | `docs/development-roadmap.ko.md` / `.en.md` | Overall direction, node order, boundaries, dependencies |
| Progress | `docs/development-progress.ko.md` / `.en.md` | Actual current node/version/substep/blocker/validation state |
| Document Naming | `docs/document-naming.ko.md` / `.en.md` | Naming, location, and update rules |
| Implementation Brief | `docs/implementation-briefs/<Node>-<topic>.ko.md` / `.en.md` | Focused engineering contract for a Node |
| Build History | `docs/build-history/<version>.ko.md` / `.en.md` | Version-specific implementation and validation evidence |
| Work Log | `docs/worklog/<development-line>.ko.md` / `.en.md` | Cumulative work flow, decisions, experiments, and lessons for one development line |
| Architecture | `docs/architecture/<topic>.ko.md` / `.en.md` | Long-lived design and structural documentation |
| Architecture Snapshot | `docs/architecture/<topic>-<version>.ko.md` / `.en.md` | Only when a historical/audit snapshot must be pinned to a version |
| Legacy Snapshot | `docs/architecture/legacy/` | Non-build preservation of replaced implementations/designs |
| Structure | `docs/STRUCTURE.md` | Repository/source structure summary |
| LLM Index | `docs/llms.txt` | Key documents, structure, and raw-URL rules for LLM/automation |

## 2-1. Document roles for performance tuning / profiling experiments

Performance experiments are recorded through the **existing four document
roles**, not a new document kind.

| Document | Role |
|---|---|
| Implementation Brief | Pre-defines what will be tested (hypothesis, scope, risk) |
| Build History | What was actually changed and measured, the resulting numbers, the refutation evidence and the revisit condition |
| Work Log | Why a candidate was chosen or refuted and what comes next. Links experiments via the **Performance / Tuning Experiment Index** |
| Development Progress | Current state and the surviving candidate, without repeating detailed numbers |

Detailed figures and run conditions go in **Build History**; the continuous
reasoning and the links between experiments go in the **Work Log Index**.

- No dedicated `docs/experiments/` or `docs/optimization-history/` directory is
  created.
- Connection order: `Implementation Brief → Build History → Work Log Index →
  Development Progress`
- Successful optimizations are preserved alongside **refuted hypotheses,
  measurements, run conditions, refutation reasons and future revisit
  conditions**. `NOT ACCEPTED` / `REJECTED` / `DEFERRED` / `LOW PRIORITY` are
  verdicts under the current conditions, not permanent retirements.
- Values that were not measured are written as `N/A` or `Not measured` and are
  never filled in by estimation.
- The full required field list and the status vocabulary follow `AGENTS.md`
  item 9.

## 3. Filename rules
### 3.1 Language suffix
- Korean: `.ko.md`
- English: `.en.md`
- Both files use the same basename.
- Never create or update only one side of a new KO/EN document pair.

### 3.2 Implementation Brief
Use:
```
<Node>-<topic>.ko.md
<Node>-<topic>.en.md
```
Examples: `B-adaptive-scheduler.ko.md`, `C-calibration-profile.ko.md`, `D-pipeline-queue.ko.md`
Maintain one canonical brief per active Node. Split substeps with headings inside the brief rather than creating temporary sub-files.

### 3.3 Build History
Use:
```
<version>.ko.md
<version>.en.md
```
Examples: `0.9.4.24.ko.md`, `0.9.4.24.en.md`
**Do not rename Build History files.** The versioned filename is part of the evidence identity for that implementation state.

### 3.4 Work Log
Use:
```
docs/worklog/<development-line>.ko.md
docs/worklog/<development-line>.en.md
```
Example: `docs/worklog/0.9.4.ko.md`, `docs/worklog/0.9.4.en.md`
Do **not** put a version range such as `0.9.4.19-0.9.4.24` into the Work Log filename.
Keep one cumulative Work Log for the development line and separate versions inside it with headings/tables.
When a new development line starts, create a new line-level file such as `docs/worklog/0.9.5.ko.md` / `.en.md`.

### 3.5 Architecture
Normal architecture documents use the topic only. A `<topic>-<version>` name is allowed only when the document is explicitly a historical audit/snapshot.

## 4. Avoid role duplication
- Roadmap must not duplicate detailed implementation procedure.
- Progress records **current state and next gates**, not copied design text.
- Implementation Brief records the actual engineering contract for the Node.
- Build History records actual changes and actual validation.
- Work Log records cross-version context and decisions.
- Architecture records durable structure/design.

## 5. Change procedure
1. Find all existing names and references.
2. Decide the canonical path under this rule.
3. Move with `git mv`.
4. Update cross-references inside the documents.
5. Update `docs/STRUCTURE.md`, `docs/llms.txt`, and any required README/index.
6. Verify the KO/EN pair and internal links.
7. Keep documentation-only changes in a separate `docs:` commit.

## 6. Current migration target
Current 0.9.4 Work Log:
```
docs/worklog-0.9.4.19-0.9.4.24.ko.md
docs/worklog-0.9.4.19-0.9.4.24.en.md
```
Canonical location:
```
docs/worklog/0.9.4.ko.md
docs/worklog/0.9.4.en.md
```
The content remains the 0.9.4.19–0.9.4.24 record; only the filename is generalized to the development-line level.
From 0.9.4.25 onward, append to the same `docs/worklog/0.9.4.ko.md` / `.en.md`.
