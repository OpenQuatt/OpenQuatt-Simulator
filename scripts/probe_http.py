#!/usr/bin/env python3
"""Bounded read-only HTTP + one SSE client probe; never changes lab settings."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import threading
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--duration", type=int, default=120)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--path", action="append", dest="paths",
                        help="GET path to cycle through; repeat for multiple paths (default: /)")
    args = parser.parse_args()
    if not 10 <= args.duration <= 3600:
        parser.error("duration must be 10..3600 seconds")
    if not args.url.startswith(("http://", "https://")):
        parser.error("url must be an explicit HTTP(S) lab URL")
    paths = args.paths or ["/"]
    if any(not path.startswith("/") or path.startswith("//") for path in paths):
        parser.error("paths must be relative to the explicit lab URL")
    # Bypass environment proxies for explicit local lab targets.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    start = time.monotonic()
    stop = threading.Event()
    events = []
    stream = {"events": 0, "logs": 0, "bytes": 0, "error": None}

    def capture():
        try:
            # ESPHome can send only a 10-second keepalive when state is stable.
            with opener.open(args.url.rstrip("/") + "/events", timeout=20) as response:
                for raw in response:
                    if stop.is_set() or time.monotonic() - start >= args.duration:
                        break
                    stream["bytes"] += len(raw)
                    line = raw.decode("utf-8", errors="replace").strip()
                    if line.startswith("event: "):
                        stream["events"] += 1
                        if line == "event: log":
                            stream["logs"] += 1
                    if line.startswith("data: {"):
                        data = json.loads(line[6:])
                        name = data.get("id", "")
                        if name.startswith("sensor/Simulator ") or name in (
                            "text_sensor/Simulator reset reason",
                            "text_sensor/OpenQuatt Simulator Version",
                            "text_sensor/ODU 1 diagnostics",
                            "text_sensor/ODU 2 diagnostics",
                        ):
                            events.append({"seconds": round(time.monotonic() - start, 3),
                                           "id": name, "value": data.get("value")})
                if not stop.is_set() and time.monotonic() - start < args.duration:
                    stream["error"] = "SSE closed before the capture deadline"
        except Exception as error:
            if not stop.is_set():
                stream["error"] = str(error)
        finally:
            stream["ended_at_s"] = round(time.monotonic() - start, 3)

    worker = threading.Thread(target=capture, daemon=True)
    worker.start()
    probes = []
    try:
        while time.monotonic() - start < args.duration:
            before = time.monotonic()
            path = paths[len(probes) % len(paths)]
            sample = {"seconds": round(before - start, 3), "path": path}
            try:
                with opener.open(args.url.rstrip("/") + path, timeout=5) as response:
                    sample["status"] = response.status
                    response.read()
            except Exception as error:
                sample["error"] = str(error)
            sample["latency_ms"] = round((time.monotonic() - before) * 1000, 2)
            probes.append(sample)
            stop.wait(min(2, max(0, args.duration - (time.monotonic() - start))))
    finally:
        stop.set()
        worker.join(timeout=21)
        if worker.is_alive():
            stream["error"] = "SSE worker did not stop within its read timeout"
        elif not events and not stream["error"]:
            stream["error"] = "No simulator diagnostics received"
        latencies = sorted(p["latency_ms"] for p in probes
                           if p.get("status") == 200 and "error" not in p)
        result = {"captured_at": datetime.now(timezone.utc).isoformat(),
                  "url": args.url, "duration_s": args.duration,
                  "http_requests": len(probes),
                  "http_errors": sum(p.get("status") != 200 or "error" in p for p in probes),
                  "http_p95_ms": latencies[int((len(latencies) - 1) * .95)] if latencies else None,
                  # Snapshot even if a stalled daemon reader missed its join
                  # deadline; its finally block may still record ended_at_s.
                  "sse": dict(stream), "probes": probes, "diagnostics": list(events)}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({key: value for key, value in result.items()
                          if key not in ("probes", "diagnostics")}))
    return 1 if result["http_errors"] or result["sse"]["error"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
