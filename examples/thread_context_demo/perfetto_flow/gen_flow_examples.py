# Generates two Perfetto JSON traces with the same producer/consumer timeline,
# flow_v1.json with flow format v1 (ph s/f events), flow_v2.json with flow format v2 (bind_id/flow_out/flow_in on X slices)
# Usage: python3 gen_flow_examples.py <output dir>
import json, sys, os

out_dir = sys.argv[1]
END = 60000  # us

# Producers: (pid, name, offset)
producers = [(1, "SigGen1", 0), (2, "SigGen2", 1000)]
PERIOD, CALC, CLIP_AT, CLIP = 8000, 3000, 1500, 1000
PUSH_AT = 2800  # push inside calc, after clip
# Consumer drains: 13 ms, 30 ms, then a stall until 60 ms (backlog, long arrows)
drains = [13000, 30000, 60000]
PROC, GAP = 800, 100


def build(fmt):
    ev = []
    meta = [(1, "SigGen1"), (2, "SigGen2"), (3, "Mainloop")]
    for pid, name in meta:
        ev.append({"ph": "M", "pid": pid, "name": "process_name", "args": {"name": name}})
        ev.append({"ph": "M", "pid": pid, "tid": 1, "name": "thread_name", "args": {"name": "spans"}})

    items = []  # (push_ts, pid, flow_id)
    fid = 1
    for pid, name, off in producers:
        t = off
        while t + CALC <= END:
            calc = {"ph": "X", "pid": pid, "tid": 1, "name": "calc", "ts": t, "dur": CALC}
            if fmt == 2:
                calc.update({"bind_id": hex(fid), "flow_out": True})
            ev.append(calc)
            ev.append({"ph": "X", "pid": pid, "tid": 1, "name": "clip", "ts": t + CLIP_AT, "dur": CLIP})
            push = t + PUSH_AT
            if fmt == 1:
                ev.append({"ph": "s", "pid": pid, "tid": 1, "name": "queue", "cat": "flow", "id": fid, "ts": push})
            items.append((push, pid, fid))
            fid += 1
            t += PERIOD
    items.sort()

    queue_len = []  # (ts, delta)
    for push, _, _ in items:
        queue_len.append((push, +1))
    pending = list(items)
    for d in drains:
        batch = [i for i in pending if i[0] < d]
        pending = [i for i in pending if i[0] >= d]
        if not batch:
            continue
        ev.append({"ph": "X", "pid": 3, "tid": 1, "name": "drain", "ts": d, "dur": len(batch) * (PROC + GAP) + GAP})
        t = d + GAP
        for push, pid, f in batch:
            proc = {"ph": "X", "pid": 3, "tid": 1, "name": "process", "ts": t, "dur": PROC, "args": {"from": f"SigGen{pid}", "item": f}}
            if fmt == 1:
                ev.append(proc)
                ev.append({"ph": "f", "pid": 3, "tid": 1, "name": "queue", "cat": "flow", "id": f, "ts": t + PROC // 2, "bp": "e"})
            else:
                proc.update({"bind_id": hex(f), "flow_in": True})
                ev.append(proc)
            queue_len.append((t, -1))
            t += PROC + GAP

    n = 0
    for ts, delta in sorted(queue_len):
        n += delta
        ev.append({"ph": "C", "pid": 3, "name": "queue_len", "ts": ts, "args": {"queue_len": n}})
    return ev


os.makedirs(out_dir, exist_ok=True)
for fmt in (1, 2):
    ev = build(fmt)
    with open(os.path.join(out_dir, f"flow_v{fmt}.json"), "w") as f:
        f.write("[\n" + ",\n".join(json.dumps(e, separators=(",", ":")) for e in ev) + "\n]\n")
    print(f"flow_v{fmt}.json: {len(ev)} events")
