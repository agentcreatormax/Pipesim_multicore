# Coherent Multicore Pipeline Simulator

**C++17 · Computer Architecture · Cache Coherence · Network-on-Chip · Memory Consistency**

A configurable, cycle-stepped simulator of a shared-memory multicore system, combining pipelined CPU execution with private coherent caches, a directory-managed last-level cache (LLC), a 2D mesh interconnect, and a timed DRAM subsystem.

The project evolved through successive **CS6600 computer architecture coursework milestones**: processor pipeline and execution, cache hierarchy, branch prediction and speculation, followed by multicore coherence and memory ordering. The final codebase supports both instruction-driven CPU workloads and trace-driven memory experiments.

> **Repository status:** The current public snapshot is missing `framework/pa4/`, which contains required PA4 simulation infrastructure and executable entry points. The implementation files compile, but `make`, `make framework-test`, and `make test` cannot complete until those framework sources are restored from an appropriately distributable version. The commands below describe the intended workflow **after** that dependency is restored; a passing test run is not claimed here.

## Highlights

- **Pipelined CPU cores:** Fetch, decode, execution, memory access, and in-order retirement, with configurable execution units, dependency checks, speculative control flow, and branch misprediction recovery. Includes a TAGE-SC-L-style branch predictor from an earlier milestone.
- **Directory-based MSI coherence:** Private 64-byte-line caches use Modified/Shared/Invalid permissions. Home directories track ownership and sharers, manage `GetS`/`GetM` requests, invalidations and recalls, handle dirty evictions, and coordinate permission grants with LLC access.
- **2D mesh Network-on-Chip:** Configurable mesh dimensions and endpoint counts, dimension-order **XY/YX routing**, and round-robin or class-prioritized arbitration. The framework's network model represents packet buffering, backpressure, and link/router timing.
- **SC and TSO memory models:** Configurable sequential consistency and total store order, with bounded FIFO store buffers, same-address load forwarding, store-load bypass, `FENCE`, and atomic `XCHG` operations.
- **DRAM request scheduling:** Implemented FCFS and a row-hit-aware scheduling policy with an aging threshold to reduce starvation. The timing model for banks, rows, bus turnarounds, and refresh is provided by the course framework.
- **Instrumentation:** Per-component events and cycle samples for coherence traffic, NoC hops and congestion, cache behavior, store buffers, LLC accesses, and memory-controller activity.

## Default architecture

The default configuration is a **3 × 3 mesh** with **4 CPU cores**, **4 shared LLC/home banks**, and **1 memory controller**. Every tile has a router; endpoints are assigned row-major in core → home → controller order.

```text
+--------------+--------------+--------------+
| Core 0       | Core 1       | Core 2       |
| Private MSI  | Private MSI  | Private MSI  |
+--------------+--------------+--------------+
| Core 3       | Home / LLC 0 | Home / LLC 1 |
| Private MSI  |              |              |
+--------------+--------------+--------------+
| Home / LLC 2 | Home / LLC 3 | DRAM Ctrl 0  |
+--------------+--------------+--------------+
      Each tile connects to the 2D mesh NoC.
```

The coherent data path is **private data cache → mesh NoC → home directory / shared LLC → memory controller**. The earlier pipeline's instruction caches remain separate from the new coherent data-cache path. LLC banks are **non-inclusive**, and directory metadata is retained independently of LLC data eviction.

## Key implementation modules

| Path | Responsibility |
| --- | --- |
| `design/`, `execution_unit/`, `speculation/` | CPU pipeline, execution units, hazards, and speculative execution |
| `predictors/predictor_one.cc` | TAGE-SC-L-style branch prediction |
| `coherence/cache.cc` | Private-cache MSI requests, snoops, misses, upgrades, evictions, and atomics |
| `coherence/directory.cc` | Home-directory transaction state machine, ownership, sharers, and LLC interaction |
| `noc/routing.cc` | XY/YX output-port selection and router-input arbitration |
| `memory/ordering.cc` | Memory-ordering decisions, TSO forwarding and buffering, fence/atomic gating |
| `memory/scheduling.cc` | FCFS and aging-aware, row-hit-prioritized memory request selection |
| `statistics.cc` | Event counting, queue-occupancy sampling, and stall-cycle statistics |
| `configs/` | Mesh, memory-order, routing, and processor configuration examples |
| `workloads/`, `tests/` | CPU programs, memory traces, network traffic, and test drivers |
| `docs/` | Protocol interfaces and counter definitions |

**Implementation scope:** This repository includes implementation work in the controllers, routing, scheduling, pipeline, predictor, and instrumentation modules above. Some components—including the LLC/DRAM simulation infrastructure and test harness—are part of the course-provided framework rather than original implementations. The missing `framework/pa4/` directory is essential to building the combined simulator.

## Requirements

- C++17 compiler (for example, `g++`)
- GNU Make
- Python 3 (for tests)
- No external C++ libraries

## Build and run

> These commands require the missing `framework/pa4/` sources to be restored first. Run them from the repository root. Use output prefixes whose parent directories already exist.

```bash
make -j4
make framework-test
make test
```

### 1. CPU-driven execution

Run instruction programs on the configured CPU cores:

```bash
./pa4 cpu configs/default.cfg configs/pa3.json \
  workloads/arithmetic_programs.txt arithmetic 2000

./pa4 cpu configs/default.cfg configs/pa3.json \
  workloads/lock_programs.txt lock 30000
```

`configs/pa3.json` specifies the CPU and earlier-stage cache configuration. Program-list files contain one `.pisa` path per core.

### 2. Trace-driven memory simulation

Use traces to study cache-line sharing, coherence, and ordering without running the CPU pipeline:

```bash
./pa4 trace configs/default.cfg workloads/sharing.trace sharing 5000
./pa4 trace configs/tso.cfg workloads/store_buffer.trace tso_run 5000
```

Trace records use the following format:

```text
<earliest_cycle> <core> LOAD  <address>
<earliest_cycle> <core> STORE <address> <value>
<earliest_cycle> <core> XCHG  <address> <value>
<earliest_cycle> <core> FENCE
```

For example:

```text
0 0 STORE 0 42
10 1 LOAD 0
```

### 3. Network-only simulation

```bash
./pa4-noc configs/default.cfg workloads/network.traffic network 2000
```

## Configuration

Edit or duplicate a file in `configs/` to explore the design space:

```text
x 3
y 3
cores 4
homes 4
controllers 1
routing XY
arbitration rr
memory_model SC
scheduler fcfs
```

| Parameter | Available modes / meaning |
| --- | --- |
| `x`, `y` | Mesh dimensions |
| `cores`, `homes`, `controllers` | Number of CPU, home/LLC, and DRAM endpoints |
| `routing` | `XY` or `YX` |
| `arbitration` | `rr` (round robin) or `custom` (protocol-class priority) |
| `memory_model` | `SC` or `TSO` |
| `scheduler` | `fcfs` or `custom` (row-hit-aware with aging) |

Additional defaults in `include/pa4.h` define queue capacities, link and router latencies, cache sizes, and simplified DRAM timing parameters.

## Tests and observability

The included tests cover cache sharing and invalidation, dirty eviction, false sharing, SC/TSO litmus patterns, buffered-store forwarding, fences, atomic exchanges, routing, and multicore CPU workloads. The repository provides:

- `make framework-test` — framework-level checks.
- `make test` — trace, memory, network, counter-accounting, and CPU functional tests.

The test targets **have not been validated on this public snapshot**, because the required PA4 framework sources are absent.

Simulator runs are designed to emit trace files and structured results, including `.results.csv`, `.state.json`, `.counters.json`, and `.observer.json`; CPU runs also produce `.registers.csv`. Generated data and executables are ignored by `.gitignore`.

## Scope and limitations

This is an educational architecture simulator, **not a full ISA emulator or production-accurate DDR system**. In particular, TSO implements the assignment's memory-ordering rules rather than the complete x86 memory model. The design models a single private coherent data-cache level, shared LLC/home banks, and simplified single-rank DRAM timing.

Developed in the context of **CS6600** coursework, with successive milestones integrated into the final multicore project. Course-provided scaffolding and models should remain properly attributed; any missing framework code should be restored only if its redistribution is permitted.
