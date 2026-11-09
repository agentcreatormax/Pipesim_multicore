# Counter definitions

`count_event(system, component, event, amount)` receives accepted events.
Store them under `component + "." + event`. Add `amount` for ordinary events;
for names ending in `_max`, retain the maximum. `Counters::json` and the output
writer are supplied: you populate the counters, not another JSON exporter.
Missing counts default to zero. Reset clears all counters.

`sample_occupancy_and_stalls` runs once at cycle start. Read `current` for
sampling. Call `event` or update `counters`; never read/copy/write `observer`.
Record an event only when accepted, not whenever it is retried. Latencies and
waits are in cycles, byte counts in bytes, occupancy in queued entries.

## Supplied accepted events

These events are emitted by the framework. Handle them in `count_event`; do
not emit duplicates from your controllers.

| Component | Event | Definition |
| --- | --- | --- |
| noc.T | packets_injected, injected_MESSAGE | A successful send at source tile T |
| noc.T | packets_delivered, delivered_MESSAGE | Admission to destination T's router inbox, before endpoint consumption |
| noc.T | link_hops | One inter-router transfer completes; T is its sending router |
| noc.T | link_bytes | Packet bytes launched on a link from T; count again on each hop |
| noc.T and noc.T.port.P | grants | An input wins output arbitration, including Local |
| noc.T | arbitration_losses | Number of eligible candidates not selected at each grant |
| noc.T | downstream_blocked_cycles | Once per router per cycle if any ready routed input is blocked by destination storage and its output is otherwise available |
| noc.T | delivered_latency_sum | Sum of destination-inbox admission boundary minus send cycle |
| cache.C | accesses_completed | cache_complete produces a result; excludes forwarded loads and fences |
| order.C | stores_enqueued, stores_drained | Store enters buffer / its draining cache result is consumed |
| order.C | loads_forwarded | Load completes directly from its store buffer |
| order.C | loads_bypassed | Accepted load has at least one older buffered store to a different word; includes forwarding if another buffered store is to a different word |
| order.C | fences_started, fences_completed | CPU submission accepts a fence / ordering completes it |
| llc.H | requests_accepted, requests_completed | LLC request enqueued / reply created; H is bank index |
| llc.H | hits, misses | One lookup classification per accepted LLC request |
| llc.H | dirty_evictions | Dirty victim's MemWrite is successfully sent |
| mc.M | requests_accepted | Packet admitted to controller M's request queue |
| mc.M | requests_service_started | Scheduler removes a request and assigns its idle bank |
| mc.M and mc.M.bank.B | row_hits, row_misses, row_conflicts | At service start: target row already open / no row open / another row open |
| mc.M | requests_completed | Response successfully injected after the burst |
| mc.M | completed_latency_sum | Sum of response-injection boundary minus controller_arrival |
| mc.M | read_bytes, write_bytes | 64 bytes per completed MemRead / MemWrite |
| mc.M | command_ACT, command_PRE, command_RD, command_WR | Each DRAM command actually issued; refresh is separate internal timing |
| mc.M.core.C | queue_wait_cycles_sum, queue_wait_cycles_max | Service-start cycle minus controller_arrival; attribute fills and victim writebacks to the core whose LLC request caused them |

`delivered_latency_max` is updated by the supplied network model. Your other
maximum counters must use `Counters::maximum`.

## Events you emit in cache/directory controllers

Use `cache.C`, where C is core ID, and `directory.H`, where H is home-bank
index (`home_tile - configuration.cores`).

| Events | Definition |
| --- | --- |
| loads_accepted, stores_accepted, atomics_accepted | A local request is accepted exactly once |
| load_permission_hits, store_permission_hits, atomic_permission_hits | Matching readable line for load, or M line for store/exchange; classify at acceptance |
| read_misses, write_misses, atomic_misses | Accepted request lacks required permission; an S-to-M store is instead an upgrade |
| upgrades | Accepted store finds the same line in S and must acquire M |
| invalidations_received | Inv is consumed, including when the line is already absent |
| invalidations_applied | Consumed Inv finds a readable or retained victim copy and invalidates it |
| acks_sent | Ack for Inv or recall is successfully sent; excludes GrantAck/PutAck |
| dirty_evictions, writebacks_sent | PutM successfully sent for a victim; one of each per PutM |
| requests_accepted | Home consumes GetS/GetM/PutS/PutM, including when queued |
| transactions_started | A request becomes the line's active transaction, including releases |
| transactions_completed | That active transaction finishes: grant on GrantAck; release on successful PutAck |
| recalls_sent, invalidations_sent | Corresponding packet successfully sent |
| acks_received | Ack matched to an outstanding recall/invalidation and consumed |
| ownership_grants | DataM successfully sent, not received |
| conflicts_queued | Acquisition/release consumed into a line's conflict queue |
| completed_latency_sum | Sum of completion boundary minus transaction starting cycle |

An exchange needing M permission counts as `atomic_misses`, including from S;
do not also count it as a store upgrade. Local hits are still completed accesses.

## Occupancy and stall samples you collect

At every evaluated cycle, including idle cycles, collect `occupancy_sum`,
`occupancy_max`, and `samples` for `noc.T.queue.Q`. For each component-level
prefix below, use `queue_occupancy_sum`, `queue_occupancy_max`, and `samples`:

- Each `noc.T.queue.Q`: current router input queue length, excluding reservations.
- Each `noc.T`: sum of its 15 input queue lengths.
- Each `mc.M`: current controller request queue length, excluding bank-owned work.
- Each `llc.H`: current LLC request queue length, excluding active work/replies.
- Each `order.C`: current store buffer length, including a store being drained
  until its reply is consumed.

For order.C, count each of these at most once per cycle:

- `buffer_full_cycles`: a CPU store is waiting and the store buffer is full.
- `fence_wait_cycles`: a CPU fence is waiting with a nonempty store buffer or
  an in-flight cache request.
- `ordering_stall_cycles`: a CPU request is waiting while an earlier cache
  request is in flight, or a fence/exchange waits for buffered stores. Overlap
  with the other stall categories is allowed; do not sum them as total stalls.

## Accounting

At drain: injected=delivered; directory starts=completions;
stores enqueued=drained; memory accepted=completed. During execution, include
outstanding work in each equation. In-network packets include source queues,
router inputs and link transfers; packets in destination inboxes already count
as delivered. MC outstanding work includes queued requests and bank work.
Row hits+misses+conflicts=requests_service_started. Sum only component totals,
not both component and per-port/per-bank copies. Directory starts/completions
are observed independently from state transitions by the supplied framework.
