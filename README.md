# CS6600 Assignment 4

A configurable 2D mesh connects private coherent data caches, shared LLC/home
banks, and memory controllers. Default: 3 x 3 mesh, four cores, four home/LLC
banks, one DRAM controller. Route modes: XY and YX.

## Build and run

Requires C++17, GNU Make and Python 3. No third-party C++ libraries.

~~~
make -j4
make framework-test
make test
./pa4 cpu configs/default.cfg configs/pa3.json workloads/arithmetic_programs.txt arithmetic 2000
./pa4 trace configs/default.cfg workloads/sharing.trace sharing 5000
./pa4 cpu configs/default.cfg configs/pa3.json workloads/lock_programs.txt lock 30000
./pa4-noc configs/default.cfg workloads/network.traffic network 2000
~~~

Run from this directory. Output-prefix parent directories must exist.
You can build the code and run `make framework-test` before migrating PA3 or
implementing A4. For CPU runs, first migrate your completed PA3 code.
Run `make test` to check your A4 implementation; tests for unfinished functions
will fail.

## Manual PA3 migration

Files retain the A3 top-level structure. Copy your completed versions into:

- design.cc, design/core.{cc,h}, design/control.{cc,h}
- execution_unit.cc, execution_unit/impl.cc, execution_unit/internal.h
- cache/cache_controller/internal.h, cache/replacement.cc
- speculation.cc, speculation/internal.h
- predictors/predictor_one.{cc,h} and your custom predictor helpers

Retain A4's pipesim.h, framework/*, framework_api.cc,
design/instruction_helpers.cc and execution_unit/helpers.cc. Merge custom
shared-header changes without removing A4 interfaces. Keep shadow predictors
unchanged. Keep your submission3 folder unchanged.

Required core merges:
1. Core::Memory checks Machine::MemorySequenceMayIssue(sequence) before AGU
   handoff. False means stall without consuming the execution output.
2. Core::Decode treats XCHG's displacement as rhs and rs2 as store_value.
   FENCE has no register operands. Supplied helpers select AGU and hazards.
3. AGU preserves instruction/prediction metadata, computes lhs + rhs and carries
   store_value. memory_write is true only for SW; the adapter uses the opcode
   to distinguish FENCE and XCHG.
4. FENCE and XCHG wait behind unresolved branches like LW and SW.

Migration check: arithmetic register 3 must be 12 on cores 0 and 1.

## Functions to implement

Complete the marked functions in these files. Each comment states what you
need to implement. All A4 interfaces are in the `multicore` namespace.
Read [the interface contracts](docs/INTERFACES.md) and
[the counter definitions](docs/COUNTERS.md) before implementing them.

- `select_output_port` and `select_router_input`: routing and arbitration.
- `accept_cache_request` and `handle_cache_message`: cache requests and messages.
- `handle_directory_message` and `advance_directory_transaction`: directory transactions.
- `decide_memory_action`: TSO buffering, forwarding, and issue decisions.
- `select_memory_request`: memory-controller scheduling.
- `count_event` and `sample_occupancy_and_stalls`: counters and cycle sampling.

| File | Required work |
| --- | --- |
| noc/routing.cc | XY/YX routing; round-robin/custom arbitration |
| coherence/cache.cc | Private-cache MSI controller |
| coherence/directory.cc | Home MSI controller and LLC integration |
| memory/ordering.cc | TSO forwarding, bypass, buffering and fences |
| memory/scheduling.cc | FCFS/custom request selection |
| statistics.cc | Event counting and occupancy/stall sampling |

The LLC and DRAM models are supplied. The new coherent data
path models one private cache level. Migrated PA3 instruction caches remain
active; PA3 L1D/L2D objects are disconnected in external-memory-port mode.

## Inputs and instructions

Program lists contain one .pisa path per core. Shared DATA initializers must
agree. Paths are relative to the working directory.

Trace records: earliest_cycle core LOAD address; STORE address value; XCHG
address value; or FENCE. Per-core order is preserved. Values accept decimal/hex.

~~~
INST pc FENCE 0 0 0 0
INST pc XCHG destination base source displacement
~~~

XCHG writes register[source] to memory[register[base] + displacement] and returns
the old word in destination. Aligned 32-bit accesses only.

## Protocol contract

- Each endpoint attaches to a router. Row-major tile IDs: cores first, homes
  next, controllers last. Remaining tiles are routers only.
- Five ports: +X, -X, +Y, -Y, Local. Opposite directions are independent.
  One fixed routing order applies globally per run.
- Three buffer classes separate requests, forwarded snoops, and replies.
  Source/destination capacity is per class; physical links are shared.
  Response traffic must not be trapped behind blocked acquisitions.
- Store-and-forward reserves downstream storage before launch. Link occupancy
  is ceil(packet_bytes/link_bytes). Arrival is launch + occupancy + link_latency.
  Router latency applies before the next forwarding decision.
- Headers reserve 16 bytes. PutM, Ack, DataS/DataM and MemWrite/MemData reserve
  another 64 bytes. Even invalidation Ack uses that payload in this baseline.
- Cache lines are 64 bytes. Private data caches and shared LLC banks are finite,
  direct-mapped stores. Directory metadata survives LLC data eviction; the LLC
  is non-inclusive. Private M data can make LLC/backing memory stale.
- Home = line_index modulo homes. Controller = line_index modulo controllers.
  Use llc_request/llc_reply from your home-directory controller; do not read backing memory directly.
- GetS/GetM acquire permissions. RecallS/RecallI obtain current owner data and
  downgrade/invalidate. Inv removes shared permission; Ack confirms it.
  DataS/DataM grant data/permission; GrantAck confirms installation and applying
  the pending access. PutS/PutM release victims; PutAck releases eviction data.
  Match line, requester and transaction ID.
- One cache access per core; one home transaction per line. Conflict queue
  capacity is router_depth. Keep the transaction active through GrantAck.
- Recall during a pending PutM returns retained victim data. A stale queued
  eviction must not overwrite newer data after ownership changes.
  Inv during upgrade invalidates the old copy but preserves the acquisition.
- Grant write permission only after required invalidation acknowledgements.
  Apply a pending access before GrantAck. Retain dirty victims until PutAck.
  Exchange updates one word indivisibly after obtaining exclusive permission.
- The framework reads current state to select work and stages changes in next.
  Your endpoint callbacks inspect and update next so a later callback sees an
  earlier invalidation in the same cycle. Commit exposes changes to other
  components. Incoming messages precede local accesses at an endpoint.
- Sequential Consistency (SC): memory accesses appear in a single global
  order preserving each core's program order.
- Total Store Order (TSO): each core buffers non-speculative stores in a bounded
  FIFO and drains them in order. A load forwards from the youngest older
  buffered store to the same word; otherwise it may execute before older stores
  to different words become visible. Preserve load order. A full store buffer
  stalls new stores. SC or TSO is selected by configuration, not an instruction.
- FENCE waits for older memory accesses and an empty store buffer before
  allowing younger memory accesses. XCHG has the same ordering and exchanges
  one word indivisibly. Fences do not flush caches to DRAM.
- Idle store draining has a supplied four-cycle admission delay. Waiting
  fences/atomics continue draining older stores. Halted cores service coherence;
  terminate only after buffered stores and outstanding work drain.
- DRAM models rows, ACT/PRE/RD/WR, shared burst bandwidth, turnaround/recovery
  and refresh. It is an educational single-rank model, not full DDR conformance.

## Memory-ordering references

- SC: [Leslie Lamport, How to Make a Multiprocessor Computer That Correctly Executes Multiprocess Programs (1979)](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/12/How-to-Make-a-Multiprocessor-Computer-That-Correctly-Executes-Multiprocess-Programs.pdf).
- TSO: [Sewell et al., x86-TSO: A Rigorous and Usable Programmer's Model for x86 Multiprocessors (2010)](https://www.cl.cam.ac.uk/~pes20/weakmemory/cacm.pdf), especially its abstract-machine explanation.

Use these for background. Implement the assignment's ordering rules; the full
x86 instruction set and memory types are outside the assignment.

## Counters and submission

count_event receives accepted transport/memory events. Add coherence classifications
in your controllers; sample_occupancy_and_stalls runs before movement each global cycle.
Count acceptance once, not per retry. Injection is source-interface acceptance;
delivery is admission to the destination router inbox; a hop is a completed inter-router transfer.
Latency is completion boundary minus acceptance boundary. Sample occupancy at
cycle start. Keep atomic categories separate.

At drain: injected = delivered; directory starts = completions; buffered stores
enqueued = drained; memory requests accepted = completed. During execution add
in-flight counts. Row hit/miss/conflict is classified at service start.
Do not edit the independent observer or copy its totals into your counters.

Submit source code only in submission4, with attributable Git contributions.
No binaries, reports, plots or generated results.
