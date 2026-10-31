#!/usr/bin/env python3
"""Per-release bench: sample several analyzers (one per path) and print one table.

Usage:
  tools/bench_paths.py [--minutes 30] [--interval 55] [--generator URL] [--json out.json] NAME=URL [NAME=URL ...]

  NAME=URL   an analyzer's web base, for example direct=http://avana:8180
  --generator  the generator's web base: missed/late grains and commit time over the run

Every --interval seconds it reads /api/v1/events?limit=200 from each analyzer and keeps every
event once (by event_id), so the capped event list does not hide events. At the end it prints, per
path: events seen, paired, each loss reason, the AV sync median and 5/95 %, warn/fail share,
absolute latency (or "relative" when no source emit offsets are entered), correlation and audio
uncertainty, plus the analyzer's events_total increase over the run. Only the Python standard
library is used.
"""
import argparse
import json
import statistics
import sys
import time
import urllib.request


def get_json(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return json.load(response)


def get_metrics(base):
    values = {}
    try:
        with urllib.request.urlopen(base + "/metrics", timeout=5) as response:
            for line in response.read().decode().splitlines():
                if line and not line.startswith("#"):
                    name, _, value = line.rpartition(" ")
                    try:
                        values[name] = float(value)
                    except ValueError:
                        pass
    except OSError:
        pass
    return values


def pct(values, p):
    if not values:
        return None
    values = sorted(values)
    return values[min(len(values) - 1, int(round(p * (len(values) - 1))))]


def fmt(value, digits=2):
    return "–" if value is None else f"{value:.{digits}f}"


def summarise(events):
    paired = [e for e in events.values() if e.get("av_offset_ms") is not None and e.get("grade") != "invalid"]
    reasons = {}
    for e in events.values():
        if e not in paired:
            key = e.get("reason") or "invalid"
            reasons[key] = reasons.get(key, 0) + 1
    av = [e["av_offset_ms"] for e in paired]
    grades = [e.get("grade") for e in paired]
    def median(key):
        values = [e[key] for e in paired if e.get(key) is not None]
        return statistics.median(values) if values else None
    return {
        "events": len(events),
        "paired": len(paired),
        "reasons": reasons,
        "av_median": statistics.median(av) if av else None,
        "av_p5": pct(av, 0.05),
        "av_p95": pct(av, 0.95),
        "warn_pct": 100.0 * grades.count("warn") / len(grades) if grades else None,
        "fail_pct": 100.0 * grades.count("fail") / len(grades) if grades else None,
        "video_abs": median("video_latency_abs_ms"),
        "audio_abs": median("audio_latency_abs_ms"),
        "video_arrival": median("video_latency_ms"),
        "audio_arrival": median("audio_latency_ms"),
        "correlation": median("correlation"),
        "uncertainty": median("uncertainty_ms"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="NAME=URL of an analyzer")
    parser.add_argument("--minutes", type=float, default=30)
    parser.add_argument("--interval", type=float, default=55)
    parser.add_argument("--generator", help="generator web base")
    parser.add_argument("--json", help="also write the result as JSON")
    args = parser.parse_args()
    paths = {}
    for item in args.paths:
        name, sep, url = item.partition("=")
        if not sep or not url.startswith(("http://", "https://")):
            parser.error(f"expected NAME=URL, got {item}")
        paths[name] = url.rstrip("/")
    seen = {name: {} for name in paths}
    start_metrics = {name: get_metrics(url) for name, url in paths.items()}
    gen_start = get_metrics(args.generator) if args.generator else {}
    end = time.time() + args.minutes * 60
    while True:
        for name, url in paths.items():
            try:
                for event in get_json(url + "/api/v1/events?limit=200").get("events", []):
                    if event.get("event_id"):
                        seen[name][event["event_id"]] = event
            except OSError as error:
                print(f"{name}: {error}", file=sys.stderr)
        if time.time() >= end:
            break
        time.sleep(min(args.interval, max(0.0, end - time.time())))
    result = {"minutes": args.minutes, "paths": {}}
    for name, url in paths.items():
        summary = summarise(seen[name])
        now = get_metrics(url)
        summary["events_total_increase"] = {
            key.split('"')[1]: now.get(key, 0) - start_metrics[name].get(key, 0)
            for key in now if key.startswith("mxl_avsync_events_total{")}
        try:
            summary["latency_basis"] = get_json(url + "/api/v1/measurement").get("latency_basis")
        except OSError:
            summary["latency_basis"] = None
        result["paths"][name] = summary
    if args.generator:
        now = get_metrics(args.generator)
        def delta(key):
            return now.get(key, 0) - gen_start.get(key, 0)
        result["generator"] = {
            "grains": delta('mxl_avsync_output_frames_total{essence="video"}'),
            "missed": delta('mxl_avsync_output_frames_missed_total{essence="video"}'),
            "late": delta('mxl_avsync_output_frames_late_total{essence="video"}'),
        }
        try:
            result["generator"]["commit_vs_timestamp_ms"] = get_json(args.generator + "/api/v1/status").get("emit_offsets", {}).get("video_commit_vs_timestamp_ms")
        except OSError:
            pass
    print(f"| path | events | paired | lost by reason | AV sync median (p5…p95) ms | warn % | fail % | latency video / audio ms | corr | ± audio ms |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for name, s in result["paths"].items():
        lost = ", ".join(f"{k} {v}" for k, v in sorted(s["reasons"].items())) or "0"
        if s["video_abs"] is not None or s["audio_abs"] is not None:
            latency = f"{fmt(s['video_abs'], 1)} / {fmt(s['audio_abs'], 1)}"
        else:
            latency = f"relative: {fmt(s['video_arrival'], 1)} / {fmt(s['audio_arrival'], 1)}"
        print(f"| {name} | {s['events']} | {s['paired']} | {lost} | {fmt(s['av_median'], 3)} ({fmt(s['av_p5'], 1)}…{fmt(s['av_p95'], 1)}) | "
              f"{fmt(s['warn_pct'], 1)} | {fmt(s['fail_pct'], 1)} | {latency} | {fmt(s['correlation'], 3)} | {fmt(s['uncertainty'], 2)} |")
    if "generator" in result:
        g = result["generator"]
        print(f"\ngenerator: {g['grains']:.0f} grains, missed {g['missed']:.0f}, late {g['late']:.0f}, commit vs T_k {g.get('commit_vs_timestamp_ms')}")
    if args.json:
        with open(args.json, "w") as out:
            json.dump(result, out, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
