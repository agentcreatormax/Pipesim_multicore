#!/usr/bin/env python3
"""Public functional tests; creates all generated files in a temporary directory."""
import csv
import json
import pathlib
import random
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run_case(directory, name, text, model="SC", overrides="", max_cycles=20000):
    config = directory / (name + ".cfg")
    config.write_text((ROOT / "configs/default.cfg").read_text()
                      .replace("memory_model SC", "memory_model " + model) + overrides)
    workload = directory / (name + ".trace")
    workload.write_text(text)
    output = directory / name
    result = subprocess.run(
        [str(ROOT / "pa4"), "trace", str(config), str(workload),
         str(output), str(max_cycles)],
        cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise AssertionError(result.stderr.strip())
    rows = list(csv.DictReader(open(str(output) + ".results.csv")))
    values = {int(row["id"]): int(row["value"]) for row in rows}
    state = json.load(open(str(output) + ".state.json"))
    counters = json.load(open(str(output) + ".counters.json"))
    observer = json.load(open(str(output) + ".observer.json"))

    def total(component, event):
        return sum(value for key, value in counters.items()
                   if key.startswith(component + ".") and key.count(".") == 2
                   and key.endswith("." + event))

    for event, observation in [
        ("packets_injected", "noc.injected"),
        ("packets_delivered", "noc.delivered"),
        ("link_hops", "noc.hops"),
        ("link_bytes", "noc.link_bytes"),
    ]:
        assert total("noc", event) == observer.get(observation, 0), event
    assert total("noc", "packets_injected") == total("noc", "packets_delivered")
    assert total("directory", "transactions_started") == observer.get("directory.started", 0)
    assert total("directory", "transactions_completed") == observer.get("directory.completed", 0)
    assert total("directory", "transactions_started") == total(
        "directory", "transactions_completed")
    assert total("order", "stores_enqueued") == total("order", "stores_drained")
    assert total("order", "loads_forwarded") == observer.get("order.forwarded", 0)
    assert total("order", "loads_bypassed") == observer.get("order.bypassed", 0)
    if name == "forwarding":
        assert observer.get("order.forwarded", 0) > 0
    if name == "tso_litmus":
        assert observer.get("order.bypassed", 0) > 0
    assert total("mc", "requests_accepted") == total("mc", "requests_completed")
    assert total("mc", "requests_service_started") == sum(
        total("mc", field) for field in ("row_hits", "row_misses", "row_conflicts"))
    if model == "SC":
        reference = {}
        for line in pathlib.Path(str(output) + ".trace").read_text().splitlines():
            fields = line.split()
            if len(fields) < 8 or fields[1] != "memory_event":
                continue
            operation = fields[4]
            address, written, returned = map(int, fields[5:8])
            if operation in ("load", "exchange"):
                assert returned == reference.get(address, 0), (operation, address, returned)
            if operation in ("store", "exchange"):
                reference[address] = written
        for address, value in reference.items():
            assert state["memory"].get(str(address), 0) == value
    return values, state, observer


def main():
    failed = 0
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        cases = [
            ("sharing", (ROOT / "workloads/sharing.trace").read_text(), "SC", "",
             {2: 42, 4: 99}),
            ("dirty_eviction", (ROOT / "workloads/eviction.trace").read_text(), "SC", "",
             {3: 123}),
            ("llc_dirty_eviction", (ROOT / "workloads/eviction.trace").read_text(), "SC",
             "\nllc_lines 1\n", {3: 123}),
            ("false_sharing", (ROOT / "workloads/false_sharing.trace").read_text(), "SC", "", {}),
            ("sc_litmus", (ROOT / "workloads/store_buffer.trace").read_text(), "SC", "", {}),
            ("tso_litmus", (ROOT / "workloads/store_buffer.trace").read_text(), "TSO", "", {}),
            ("tso_fence", (ROOT / "workloads/fence.trace").read_text(), "TSO", "", {}),
            ("forwarding", "0 0 STORE 0 9\n0 0 LOAD 0\n", "TSO", "", {2: 9}),
            ("buffer_full", "".join(f"0 0 STORE {64*i} {i+1}\n" for i in range(10)),
             "TSO", "\nstore_entries 1\n", {}),
            ("atomic_chain",
             "0 0 XCHG 0 10\n0 1 XCHG 0 20\n0 2 XCHG 0 30\n0 3 XCHG 0 40\n1000 0 LOAD 0\n",
             "SC", "", {}),
            ("small_buffers", (ROOT / "workloads/sharing.trace").read_text(), "SC",
             "\nrouter_depth 1\ninjection_depth 1\nllc_queue_entries 1\nmc_entries 1\n", {2: 42, 4: 99}),
            ("yx_custom", (ROOT / "workloads/sharing.trace").read_text(), "SC",
             "\nrouting YX\narbitration custom\nscheduler custom\n", {2: 42, 4: 99}),
        ]
        # Independent sequential expected values, stressing both cache levels.
        expected = {}
        text = []
        memory = {}
        for index in range(80):
            address = ((index * 7) % 24) * 64
            core = index % 4
            cycle = index * 300
            if index % 3:
                value = 100 + index
                text.append(f"{cycle} {core} STORE {address} {value}\n")
                memory[address] = value
                expected[index + 1] = 0
            else:
                text.append(f"{cycle} {core} LOAD {address}\n")
                expected[index + 1] = memory.get(address, 0)
        cases.append(("sequential_oracle", "".join(text), "SC", "", expected))

        for seed in range(8):
            rng = random.Random(seed)
            text = []
            for index in range(120):
                core = index % 4
                address = rng.randrange(20) * 64 + rng.randrange(2) * 4
                operation = rng.choice(("LOAD", "STORE", "XCHG"))
                extra = "" if operation == "LOAD" else " " + str(rng.randrange(1, 1000))
                text.append(f"{index // 4 * 2} {core} {operation} {address}{extra}\n")
            cases.append((f"concurrent_oracle_{seed}", "".join(text), "SC",
                          "\nrouter_depth 1\ninjection_depth 1\nllc_lines 1\n", {}))

        for name, text, model, overrides, expected in cases:
            try:
                values, state, observer = run_case(
                    directory, name, text, model, overrides, 40000)
                assert all(values.get(key) == value for key, value in expected.items())
                if name == "sc_litmus":
                    assert not (values[2] == 0 and values[4] == 0)
                if name == "tso_fence":
                    assert not (values[3] == 0 and values[6] == 0)
                if name == "atomic_chain":
                    final = values[5]
                    assert sorted(values[i] for i in range(1, 5)) == sorted(
                        [0] + [value for value in (10, 20, 30, 40) if value != final])
                if name == "llc_dirty_eviction":
                    assert observer.get("dram.WR", 0) > 0
                print("PASS", name)
            except (AssertionError, KeyError) as error:
                print("FAIL", name, str(error))
                failed += 1

        try:
            traffic = directory / "network.traffic"
            traffic.write_text("0 0 1 GetS 0\n100 0 2 GetS 0\n200 0 8 GetS 0\n300 0 8 DataM 0\n")
            output = directory / "network"
            completed = subprocess.run(
                [str(ROOT / "pa4-noc"), "configs/default.cfg", str(traffic), str(output), "1000"],
                cwd=ROOT, capture_output=True, text=True)
            assert completed.returncode == 0, completed.stderr
            injected, delivered = {}, {}
            for line in pathlib.Path(str(output) + ".trace").read_text().splitlines():
                fields = line.split()
                if fields[1] == "inject": injected[int(fields[2])] = int(fields[0])
                if fields[1] == "deliver": delivered[int(fields[2])] = int(fields[0]) + 1
            latency = [delivered[index] - injected[index] for index in (1, 2, 3, 4)]
            assert latency[0] < latency[1] < latency[2] < latency[3], latency
            print("PASS noc_distance_and_payload")
        except AssertionError as error:
            print("FAIL noc_distance_and_payload", error)
            failed += 1

        try:
            output = directory / "cpu"
            result = subprocess.run(
                [str(ROOT / "pa4"), "cpu", "configs/default.cfg", "configs/pa3.json",
                 "workloads/programs.txt", str(output), "5000"],
                cwd=ROOT, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            registers = {(int(row["core"]), int(row["register"])): int(row["value"])
                         for row in csv.DictReader(open(str(output) + ".registers.csv"))}
            assert registers[0, 2] == 42
            assert registers[1, 2] == 0 and registers[1, 3] == 7
            print("PASS cpu_fence_exchange")
        except AssertionError as error:
            print("FAIL cpu_fence_exchange", error)
            failed += 1

        for model in ("SC", "TSO"):
            try:
                config = directory / ("lock_" + model + ".cfg")
                config.write_text((ROOT / "configs/default.cfg").read_text()
                                  .replace("memory_model SC", "memory_model " + model))
                output = directory / ("lock_" + model)
                completed = subprocess.run(
                    [str(ROOT / "pa4"), "cpu", str(config), "configs/pa3.json",
                     "workloads/lock_programs.txt", str(output), "30000"],
                    cwd=ROOT, capture_output=True, text=True)
                assert completed.returncode == 0, completed.stderr
                state = json.load(open(str(output) + ".state.json"))
                assert state["memory"]["64"] == 12
                print("PASS lock_counter_" + model)
            except AssertionError as error:
                print("FAIL lock_counter_" + model, error)
                failed += 1

    print(f"{failed} failed")
    raise SystemExit(bool(failed))


if __name__ == "__main__":
    main()
