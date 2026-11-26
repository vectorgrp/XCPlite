#!/usr/bin/env python3
"""Find moments in a trace file written by xcpclient --perfetto where a span of one context overlaps the same span of another
context, optionally while a span of a third context is running, to locate them for a screenshot in Perfetto.

Usage:
  find_overlap.py trace.json [--span calc] [--ctx SigGen1 SigGen2] [--also Mainloop poll_loop] [--max 20]

Without --also, the overlaps are listed by length, longest first.
With --also CONTEXT SPAN, only overlaps during which SPAN of CONTEXT is running are listed. For each of them, the arrows (span links)
which end in that span are shown with their source context and time. The best moments come first: arrows from both contexts of --ctx,
with sources close to the overlap, then the longest overlap.

Times are in us since the measurement start, as shown by Perfetto.
"""
import argparse
import bisect
import json
import sys


def read_trace(filename, slice_keys):
    """Read process names, the slices named in slice_keys (set of (context, span)) and the flow events (span links)
    xcpclient writes one event per line, the file is read line by line to keep memory low for large traces"""
    pid_name = {}
    raw = []  # (pid, name, ts, end)
    flow_src = {}  # id -> (pid, ts)
    flow_dst = []  # (ts, id, pid)
    names = {span for _, span in slice_keys}
    for line in open(filename):
        line = line.strip().rstrip(",")
        if not (line.startswith("{") and line.endswith("}")):
            continue
        if '"ph":"X"' in line:
            if not any(f'"name":"{n}"' in line for n in names):
                continue
        elif '"ph":"s"' not in line and '"ph":"f"' not in line and '"process_name"' not in line:
            continue
        e = json.loads(line)
        ph = e.get("ph")
        if ph == "X":
            ts = float(e["ts"])
            raw.append((e["pid"], e["name"], ts, ts + float(e["dur"])))
        elif ph == "s":
            flow_src[e["id"]] = (e["pid"], float(e["ts"]))
        elif ph == "f":
            flow_dst.append((float(e["ts"]), e["id"], e["pid"]))
        elif ph == "M" and e.get("name") == "process_name":
            pid_name[e["pid"]] = e["args"]["name"]

    pids = {name: pid for pid, name in pid_name.items()}
    missing = sorted({c for c, _ in slice_keys if c not in pids})
    if missing:
        sys.exit(f"Context(s) not in trace: {missing}, found: {sorted(pids)}")
    slices = {key: [] for key in slice_keys}
    by_pid = {(pids[c], s): (c, s) for c, s in slice_keys}
    for pid, name, ts, end in raw:
        key = by_pid.get((pid, name))
        if key:
            slices[key].append((ts, end))
    for v in slices.values():
        v.sort()
    flow_dst.sort()
    return pid_name, slices, flow_src, flow_dst


def intersect(a, b):
    """Overlaps of two sorted interval lists, as (begin, end, interval of a, interval of b)"""
    out = []
    i = j = 0
    while i < len(a) and j < len(b):
        s = max(a[i][0], b[j][0])
        e = min(a[i][1], b[j][1])
        if s < e:
            out.append((s, e, a[i], b[j]))
        if a[i][1] < b[j][1]:
            i += 1
        else:
            j += 1
    return out


def closest_gap(a, b):
    gap = None
    i = j = 0
    while i < len(a) and j < len(b):
        g = max(a[i][0], b[j][0]) - min(a[i][1], b[j][1])
        if gap is None or g < gap[0]:
            gap = (g, a[i], b[j])
        if a[i][1] < b[j][1]:
            i += 1
        else:
            j += 1
    return gap


def fmt(iv):
    return f"{iv[0]:.3f}..{iv[1]:.3f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace")
    ap.add_argument("--span", default="calc", help="span name to overlap (default calc)")
    ap.add_argument("--ctx", nargs=2, default=["SigGen1", "SigGen2"], metavar=("A", "B"), help="the two contexts (default SigGen1 SigGen2)")
    ap.add_argument("--also", nargs=2, metavar=("CONTEXT", "SPAN"), help="require SPAN of CONTEXT to run during the overlap, e.g. Mainloop poll_loop")
    ap.add_argument("--max", type=int, default=20, help="number of moments to print (default 20)")
    args = ap.parse_args()

    ka, kb = (args.ctx[0], args.span), (args.ctx[1], args.span)
    keys = {ka, kb} | ({tuple(args.also)} if args.also else set())
    pid_name, slices, flow_src, flow_dst = read_trace(args.trace, keys)
    a, b = slices[ka], slices[kb]
    print(f"{len(a)} '{args.span}' spans in {args.ctx[0]}, {len(b)} in {args.ctx[1]}")

    overlaps = intersect(a, b)
    if not overlaps:
        print("No overlap found")
        gap = closest_gap(a, b)
        if gap:
            print(f"Closest approach: gap {gap[0]:.3f} us, {args.ctx[0]} {fmt(gap[1])}, {args.ctx[1]} {fmt(gap[2])}")
        return 1
    print(f"{len(overlaps)} overlaps, total {sum(e - s for s, e, *_ in overlaps):.3f} us")

    if not args.also:
        overlaps.sort(key=lambda o: o[0] - o[1])  # Longest first
        for s, e, x, y in overlaps[: args.max]:
            print(f"  {s:14.3f} .. {e:14.3f} us  ({e - s:7.3f} us)   {args.ctx[0]} {fmt(x)}   {args.ctx[1]} {fmt(y)}")
        return 0

    # Overlaps during which the span of the third context is running
    actx, aspan = args.also
    c = slices[tuple(args.also)]
    # The spans of one name in one track do not overlap each other, so their ends are sorted as well
    windows = []
    starts = [iv[0] for iv in c]
    for s, e, x, y in overlaps:
        k = bisect.bisect_left(starts, e)  # Spans of c which begin before the overlap ends
        while k > 0 and c[k - 1][1] > s:
            k -= 1
            iv = c[k]
            windows.append((max(s, iv[0]), min(e, iv[1]), x, y, iv))
    print(f"{len(windows)} of them while '{aspan}' of {actx} is running")
    if not windows:
        return 1

    # Arrows which end in the span of the third context, with their source context and time
    dst_ts = [f[0] for f in flow_dst]
    results = []
    for ws, we, x, y, iv in windows:
        lo, hi = bisect.bisect_left(dst_ts, iv[0]), bisect.bisect_right(dst_ts, iv[1])
        arrows = []
        for ts, fid, _ in flow_dst[lo:hi]:
            if fid in flow_src:
                spid, sts = flow_src[fid]
                arrows.append((pid_name.get(spid, str(spid)), sts, ts))
        # Score: arrows from both contexts, the source of each as close to the overlap as possible
        dist = {}
        for ctx, sts, _ in arrows:
            if ctx in args.ctx:
                d = max(ws - sts, sts - we, 0.0)
                dist[ctx] = min(dist.get(ctx, d), d)
        score = (-len(dist), max(dist.values()) if dist else float("inf"), ws - we)
        results.append((score, ws, we, x, y, iv, arrows))
    results.sort(key=lambda r: r[0])

    for score, ws, we, x, y, iv, arrows in results[: args.max]:
        print(f"\n  {ws:.3f} .. {we:.3f} us  ({we - ws:.3f} us, all three running)")
        print(f"    {args.ctx[0]} {args.span} {fmt(x)}   {args.ctx[1]} {args.span} {fmt(y)}   {actx} {aspan} {fmt(iv)}")
        for ctx, sts, dts in arrows:
            print(f"    arrow {ctx} {sts:.3f} -> {actx} {dts:.3f}  (source {sts - ws:+.3f} us from the overlap)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
