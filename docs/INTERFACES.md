# A4 interface contracts

Implement the functions listed below. The mesh timing, queues, LLC storage,
DRAM commands and core adapter are supplied. You implement routing,
arbitration, coherence, TSO decisions, scheduling and counters. You may add
helper functions and transient-state fields. Keep the public signatures,
capacities, address mappings and framework timing unchanged. Never update
`observer`: the framework maintains it independently.

## State and cycle order

`begin()` copies `current` to `next`. `evaluate()` calls counter sampling,
network movement, memory ordering, endpoints, LLC and DRAM, in that order.
`commit()` exposes `next` and advances the cycle.

The framework selects incoming work from `current`. Your cache/directory
callbacks inspect and update `next`. This matters when an invalidation and a
local request reach the same cache in one cycle: the request must see the
invalidation. Do not write `current`. New packets, LLC replies and cache
results become available to their consumers after commit.

Each core handles at most one incoming packet and one local cache request per
cycle, in that order. Each home handles at most one incoming packet per cycle.
`advance_directory_transaction` is then called for each directory entry that
existed in `current`, including entries changed by the incoming packet. A
newly inserted entry first receives this callback in the next cycle. A new
transaction must not advance its first stage in its starting cycle. At most
one new invalidation per home per cycle may be issued. You may need per-home
state to coordinate several lines. The supplied can_send/send also limit new
home snoops to one per cycle.

A callback returning `false` has not consumed its input. Leave `next`, counters,
queues and outgoing messages unchanged. Check resources before accepting an
input or making a state change. A callback returning `true` consumes its input
exactly once. Directory progress has no return value: keep its transaction in
its current phase when a required resource is unavailable.

## Routing and arbitration

`RoutingContext.current_router` is the router making this decision, not the
packet's original source. `destination_router` is the final destination.
Return one `Port`. Return `Local` only at the destination. Tile ID is
`y * configuration.x + x`. Coordinates use increasing X/Y for PositiveX/Y.

An `ArbitrationCandidate.input` is a router input ID, **not its position in the
candidate vector**. Input ID is `port * 3 + channel(message_type)`, in [0,14].
`select_router_input` returns one of these IDs, or `std::nullopt` when the list
is empty. Candidates already satisfy link availability, routing, storage and
ready-time checks. With a nonempty list, select a candidate; do not idle.
Round-robin compares these IDs cyclically from `round_robin_cursor` modulo 15.
The framework updates the cursor after a grant. Write your other policy under
`arbitration custom`; the algorithm and tie-breaking are yours.

## Addresses, IDs and messages

CPU addresses are aligned 32-bit word **byte addresses**. Packet/LLC
`line_address` is a 64-byte-aligned byte address. Word index within a line is
`(address % 64) / 4`. `slot(address)` indexes the private direct-mapped cache.
Private and LLC tags store aligned byte addresses. `home`, `controller`,
`bank` and `row` take aligned byte addresses. `home`/`controller` return tile
IDs; `bank`/`row` return indices. The controller vector index is its tile ID
minus `configuration.cores + configuration.homes`; the LLC vector index is
the home tile ID minus `configuration.cores`.

`MemoryRequest.id` is unique for an entire run across cores. Use it as the
cache acquisition/eviction transaction ID. The supplied adapter and trace
runner allocate these IDs. `Packet.id` is a separate transport ID assigned
by `send`; it identifies one packet, not a transaction. Preserve
`transaction_id`, `line_address` and `requester` in protocol replies.
`requester` is the original requesting core, even when a different core
answers a recall. Packet source/destination are tile IDs.

| Message | Sender -> receiver | Meaning / data |
| --- | --- | --- |
| GetS / GetM | Core -> home | Request shared / exclusive permission |
| PutS / PutM | Core -> home | Release a clean / modified victim; PutM carries its data |
| RecallS / RecallI | Home -> owner | Return modified data and downgrade / invalidate |
| Inv | Home -> sharer | Invalidate its copy |
| Ack | Core -> home | Acknowledge Inv or recall; a recall Ack carries current data |
| DataS / DataM | Home -> requester | Full line and shared / exclusive permission |
| GrantAck | Requester -> home | Grant installed and pending access applied |
| PutAck | Home -> evicting core | Victim can now be released |
| MemRead / MemWrite | LLC -> memory controller | Supplied LLC requests a fill / writeback |
| MemData / MemAck | Memory controller -> LLC | Supplied DRAM response |

Classes are requests=0 (Get/Put/MemRead/MemWrite), snoops=1 (Recall/Inv),
replies=2 (all other messages). Ack reserves a data-sized payload even for
Inv. Data in an Inv acknowledgement is ignored by the home. `CacheState::I`
means invalid in a cache and unowned at a directory; S means shared, M means
modified with one owner. Directory metadata remains after LLC eviction.

## Sending and completing

`message(type, source_tile, destination_tile, line_address, transaction_id,
data, requester)` builds a packet without sending it. Always supply requester
for coherence messages. `can_send(source_tile, type, count)` checks **next**
injection capacity for that protocol class. It does not reserve capacity.
`send(packet)` returns false without sending when the class is full. Check
its result. Preflight every message needed before consuming an input; for
multiple messages in one class, check their combined count.

For example, replying to a recall needs reply-class space. Handling PutAck
may instead need request-class space for the new GetS/GetM. Checking one
class says nothing about another. Retain the pending request and victim data
until the acknowledgement can be consumed and the next action accepted.

Before calling `cache_complete(core_id, result_value)`, set that core's
`next.caches[core_id].pending`. It produces one cache result and clears pending
and the cache phase. Loads return the read word, stores return 0, exchanges
return the old word. Fences are completed by the supplied ordering machinery.
Do not call `cpu_complete` from a cache/directory controller.

Choose and document your own transient states. Handle recalls during dirty
eviction, invalidations during an upgrade, and queued stale PutM requests.
Never overwrite newer data with an old victim. Keep a directory transaction
active until GrantAck. Apply the pending access before sending GrantAck.

## LLC integration

Use `llc_request(home_tile, LLCRequest)`; true means enqueued once, false means
retry without changing the directory phase. `write=false` requests a line.
`write=true` supplies a complete updated line, marks it dirty and installs it
even on a miss. It is not a write-through-to-DRAM operation. Both forms produce
an LLCReply. The framework stamps `accepted`; it ignores your supplied value.

`llc_reply(home_tile, line_address, transaction_id, requester)` returns and
consumes a matching reply visible in `current`, or returns no value. Do not
call it until your transaction is ready to consume the reply. Update phases
so neither requests nor replies are consumed twice. Recall dirty owner data
before using LLC data; the LLC may be stale while a core owns M permission.

## Memory ordering

`decide_memory_action` is called only in TSO mode, when no earlier cache request
is in flight. Its arguments are read-only. The framework moves queues and
completes requests according to your returned decision.

| MemoryAction | Framework action / permitted use |
| --- | --- |
| Wait | Keep the CPU request; drain the oldest buffered store if any |
| BufferStore | Enqueue a store and acknowledge it to the core; only if capacity remains |
| Forward | Complete a load with decision.value from its youngest older same-word store |
| Issue | Send a load/store/exchange to the cache; never a fence |
| CompleteFence | Complete a fence; only with an empty store buffer |

Implement TSO's store buffering and forwarding rules from Task 3. The FIFO
contains only older stores. The supplied in-order request interface preserves
load order. The framework services drain replies before processing another
request, so an exchange cannot overlap an earlier drain. When no CPU request
is waiting, a store waits at least four cycles from its admission before
draining. Wait can trigger draining immediately. No fence flush to DRAM is
required. Halt does not discard stores or stop coherence.

## Memory scheduling

`eligible_requests` contains **indices into controller.queue**, not positions
in that vector. Candidates target idle banks. They may still need PRE/ACT and
timing waits; the supplied DRAM engine enforces those. Return an eligible index,
or no value if none exist. Select a request when the list is nonempty.

FCFS selects earliest `Packet.controller_arrival`; break ties by queue index.
`Packet.injected` remains the NoC injection cycle. Implement your other policy
under `scheduler custom`, including a finite aging rule to prevent starvation.
Do not change DRAM timing. Bank/row mapping for line_index=line_address/64:
bank=(line_index/controllers)%banks; row=(line_index/controllers)/banks/16.

## Checks and implementation order

Run `make framework-test` before completing A4. It uses isolated scripted
fixtures and does not test your algorithms. Migrate PA3 and check arithmetic.
Then complete routing/arbitration, cache+directory coherence, counters, TSO and
scheduling. Implement cache and directory together: each needs the other's
messages. Run `make test` once the required callbacks are present. Public tests check results, invariants and independent observations. Passing public
tests is necessary, but does not prove all race cases correct.
