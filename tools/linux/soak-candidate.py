"""Local candidate-only, single-process load/lifecycle qualification.

Uses the existing bounded 120-second frame probe, without changing production
or publishing network ports. Raw console logs stay private/local. Resource
sampling and file dumps have overhead; do not compare this monitored lifecycle
campaign directly with an unmonitored steady-state benchmark.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import statistics
import subprocess
import threading
import time
import uuid


spec = importlib.util.spec_from_file_location("player_load", Path(__file__).with_name("compare-player-load.py"))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

RESOURCE_COMMAND = r"""
process_dir=''
for comm_path in /proc/[0-9]*/comm; do
    if read -r comm < "$comm_path" && test "$comm" = hlds_linux; then
        process_dir=${comm_path%/comm}
        break
    fi
done
test -n "$process_dir" || exit 3
printf 'section uptime\n'
cat /proc/uptime
printf 'section status\n'
awk '/^(VmRSS|VmSize|VmData|Threads):/ {print $1, $2}' "$process_dir/status"
printf 'section open_fds\n'
ls -1 "$process_dir/fd" | wc -l
for metric in cpu.stat cpu.max memory.current memory.events cpu.pressure memory.pressure io.pressure; do
    if test -r "/sys/fs/cgroup/$metric"; then
        printf 'section %s\n' "$metric"
        cat "/sys/fs/cgroup/$metric"
    fi
done
"""


def parse_resources(text):
    sections, current = {}, None
    for line in text.splitlines():
        if line.startswith("section "):
            current = line[8:]
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    result = {"sections": sections}
    for line in sections.get("status", []):
        key, value = line.split()
        result[key.rstrip(":")] = int(value)
    if sections.get("open_fds"):
        result["open_fds"] = int(sections["open_fds"][0])
    if "VmRSS" not in result or "Threads" not in result:
        raise ValueError("Missing process resource fields")
    if result["VmRSS"] <= 0 or result["Threads"] <= 0 or result.get("open_fds", 0) <= 0:
        raise ValueError("Invalid process resource fields")
    return result


class ResourceSampler:
    def __init__(self, name, path):
        self.name, self.path = name, path
        self.finished = threading.Event()
        self.errors = self.samples = 0
        self.fatal_error = None
        self.thread = threading.Thread(target=self.run, name="local-resource-sampler", daemon=True)

    def run(self):
        try:
            self.collect()
        except Exception as error:
            self.fatal_error = type(error).__name__ + ": " + str(error)

    def collect(self):
        with self.path.open("w", encoding="utf-8") as output:
            while not self.finished.is_set():
                record = {"host_monotonic_start": time.monotonic(), "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
                try:
                    response = subprocess.run(["docker", "exec", self.name, "sh", "-c", RESOURCE_COMMAND],
                                              capture_output=True, text=True, timeout=8)
                    if response.returncode:
                        raise RuntimeError(f"Resource reader exit {response.returncode}")
                    record.update(parse_resources(response.stdout))
                    self.samples += 1
                except Exception as error:
                    self.errors += 1
                    record["missing_sample"] = type(error).__name__ + ": " + str(error)
                record["host_monotonic_end"] = time.monotonic()
                output.write(json.dumps(record) + "\n")
                output.flush()
                self.finished.wait(5)

    def start(self):
        self.thread.start()

    def stop(self):
        self.finished.set()
        if self.thread.ident is None:
            return
        self.thread.join(timeout=10)
        if self.thread.is_alive():
            raise RuntimeError("Resource sampler did not stop")


def activity_report(text):
    records = re.findall(r"\[SIM\] REPORT ([^\r\n]+)", text)
    if not records:
        raise RuntimeError("No gameplay activity report")
    return {key: float(value) for key, value in re.findall(r"(\w+)=(-?[\d.]+)", records[-1])}


def has_marker(text, needle, exact_line=False):
    # Launcher input is echoed by its separate input thread. Seeing the typed
    # 'echo TOKEN' is NOT evidence that the engine processed that command.
    return bool(re.search(r"(?m)^" + re.escape(needle) + r"[ \t]*\r?$", text)) if exact_line else needle in text


def validate_stall_counters(reports):
    keys = [f"lifetime_launcher_gt_{threshold}ms" for threshold in (1, 5, 10, 50)]
    previous = [0] * len(keys)
    for report in reports:
        if any(key not in report for key in keys) or "launcher_iterations" not in report:
            raise RuntimeError("Missing cumulative launcher stall counters")
        counts = [report[key] for key in keys]
        if any(value < old for value, old in zip(counts, previous)):
            raise RuntimeError("Cumulative launcher stall counters went backwards")
        if counts != sorted(counts, reverse=True) or counts[0] > report["launcher_iterations"]:
            raise RuntimeError("Inconsistent cumulative launcher stall counters")
        previous = counts
    return dict(zip(keys, previous))


def summarize_resources(path):
    records = [json.loads(line) for line in path.read_text().splitlines()]
    if not records or any("missing_sample" in record for record in records):
        raise RuntimeError("Missing resource samples")
    summary = {"samples": len(records), "scope": "Lifecycle samples; growth is descriptive, not a leak diagnosis"}
    for key in ("VmRSS", "VmSize", "VmData", "Threads", "open_fds"):
        values = [record[key] for record in records if key in record]
        if len(values) != len(records):
            raise RuntimeError("Incomplete resource field: " + key)
        summary[key] = {"first": values[0], "last": values[-1], "min": min(values), "max": max(values),
                        "first_five_median": statistics.median(values[:5]), "last_five_median": statistics.median(values[-5:])}
    for section, counter in (("cpu.stat", "nr_throttled"), ("memory.events", "oom_kill")):
        values = []
        for record in records:
            pairs = dict(line.split() for line in record["sections"].get(section, []) if len(line.split()) == 2)
            if counter not in pairs:
                raise RuntimeError("Missing cgroup counter: " + counter)
            values.append(int(pairs[counter]))
        summary[counter + "_delta"] = values[-1] - values[0]
        if summary[counter + "_delta"]:
            raise RuntimeError("Resource-limit events during normal-profile soak: " + counter)
    return summary


def validate_window(probe, activity, population, seconds):
    checks = {
        "probe": not probe["capacity_reached"] and probe["added_work_us"] == 0,
        "population": probe["connected_min"] == probe["connected_max"] == population,
        "alive": probe["alive_min"] == probe["alive_max"] == population,
        "roles": activity["connected"] == activity["alive"] == population and
                 activity["humans"] == activity["zombies"] == population / 2,
        "weapons": activity["custom"] == population / 2 and all(activity[f"shots_{key}"] > 0 for key in bench.WEAPONS),
        "activity": activity["hits"] > 0 and activity["distance"] > 0 and activity["commands"] > population * seconds * 90,
        "simulation": 0.98 < probe["simulation_seconds"] / probe["wall_seconds"] < 1.02,
        "pacing": probe["sys_ticrate"] == 0,
        "dead_steps": activity["dead_steps"] == 0,
    }
    if not all(checks.values()):
        raise RuntimeError("Window validation failed: " + ", ".join(key for key, passed in checks.items() if not passed))
    return checks


def qualify(args):
    if args.output.exists():
        raise RuntimeError("Choose a fresh evidence directory")
    args.output.mkdir(parents=True)
    config = args.config.read_text(encoding="utf-8")
    config, count = re.subn(r"(?m)^target_fps\s*=.*$", "target_fps = 2000", config)
    if count != 1:
        raise RuntimeError("Expected exactly one target_fps setting")
    manifest = {
        "mode": "single_process_soak", "candidate_sha256": bench.sha(args.candidate),
        "probe_sha256": bench.sha(args.instrumentation / "frame_probe_mm_i386.so"),
        "workload_sha256": bench.sha(args.instrumentation / "player_load.amxx"),
        "runner_sha256": bench.sha(Path(__file__)), "analysis_runner_sha256": bench.sha(Path(bench.__file__)),
        "image_id": bench.docker("image", "inspect", args.image, "--format", "{{.Id}}").strip(),
        "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
        "scope": "Isolated fake clients; monitored windows in ONE server process; no real clients or production changes",
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    (args.output / "candidate.rehldsm.cfg").write_text(config, encoding="utf-8")
    manifest["candidate_config_sha256"] = bench.sha(args.output / "candidate.rehldsm.cfg")
    bench.save(args.output / "manifest.json", manifest)
    name = "rehldsm-soak-" + uuid.uuid4().hex[:10]
    manifest["owned_container"] = name
    bench.save(args.output / "manifest.json", manifest)
    process = sampler = None
    created = False
    results = {"passed": False, "windows": [], "map_reloads": 0, "heartbeats": [], "phase_events": []}
    log_path = args.output / "private-runtime.log"
    try:
        bench.docker("run", "-d", "--name", name, "--network", "none", "--security-opt", "no-new-privileges",
                     "--cpuset-cpus", args.cpu, "--memory", "1g", "--mount",
                     f"type=bind,source={args.instrumentation},target=/bench,readonly", "--mount",
                     f"type=bind,source={args.output},target=/results", "--entrypoint", "sleep", args.image, "infinity")
        created = True
        bench.docker("cp", str(args.candidate), f"{name}:/opt/hlds/hlds_linux")
        bench.docker("cp", str(args.output / "candidate.rehldsm.cfg"), f"{name}:/opt/hlds/rehldsm.cfg")
        bench.docker("exec", name, "sh", "-c", "set -eu\nchmod u+x /opt/hlds/hlds_linux\n"
                     "cp /bench/player_load.amxx /opt/hlds/cstrike/addons/amxmodx/plugins/\n"
                     "cp /bench/frame_probe_mm_i386.so /opt/hlds/cstrike/addons/metamod/\n")
        manifest["environment"] = bench.docker("exec", name, "sh", "-c",
            "uname -a; getconf GNU_LIBC_VERSION; "
            "sha256sum /opt/hlds/engine_i486.so /opt/hlds/cstrike/dlls/cs.so "
            "/opt/hlds/libstdc++.so.6 /opt/hlds/libgcc_s.so.1")
        bench.save(args.output / "manifest.json", manifest)
        checked = bench.docker("exec", "-e", "LD_LIBRARY_PATH=/opt/hlds", name, "/opt/hlds/hlds_linux", "--check-config")
        if "target_fps=2000 [rehldsm.cfg]" not in checked:
            raise RuntimeError("Candidate target config was not accepted")
        (args.output / "config-check.txt").write_text(checked)
        with log_path.open("wb") as log:
            process = subprocess.Popen(["docker", "exec", "-i", "-e", "LD_LIBRARY_PATH=/opt/hlds", "-w", "/opt/hlds", name,
                "./hlds_linux", "-game", "cstrike", "-console", "-insecure", "-nomaster", "+sys_ticrate", "0", "+fps_max", "0",
                "+ip", "127.0.0.1", "+sv_lan", "1", "+maxplayers", "32", "+map", "zm_deko2"],
                stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)

            def read_since(offset=0):
                with log_path.open("rb") as source:
                    source.seek(offset)
                    return source.read().decode(errors="replace")

            def send(command):
                if process.poll() is not None:
                    raise RuntimeError(f"Server exited unexpectedly ({process.returncode})")
                offset = log_path.stat().st_size
                process.stdin.write((command + "\n").encode())
                process.stdin.flush()
                return offset

            def wait_for(needle, offset, timeout=20, exact_line=False):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    if has_marker(read_since(offset), needle, exact_line):
                        return
                    if process.poll() is not None:
                        raise RuntimeError("Server exited while waiting for a marker")
                    time.sleep(0.2)
                raise RuntimeError("Missing expected console marker (inspect the private log)")

            def heartbeat(label):
                marker = "stability_health_" + label
                start = time.monotonic()
                offset = send("echo " + marker)
                wait_for(marker, offset, timeout=10, exact_line=True)
                results["heartbeats"].append({"label": label, "console_response_seconds": time.monotonic() - start})

            def populate():
                results["phase_events"].append({"phase": "population_and_warmup", "host_monotonic": time.monotonic()})
                send("sys_ticrate 0\nfps_max 0\nhost_framerate 0\nsys_timescale 1\n"
                     "mp_timelimit 0\nmp_roundtime 9\nmp_round_infinite 1\nzp_gamemode_delay 9999\nfps_max")
                offset = send(f"sim_population {args.population}")
                wait_for(f"[SIM] READY population={args.population}", offset, timeout=30)
                time.sleep(args.warmup)

            wait_for("[FRAMEPROBE] attached", 0, timeout=40)
            time.sleep(5)
            offset = send("version\nmeta list\namxx modules\namxx plugins\nrehldsm_version")
            wait_for("17 modules, 17 correct", offset)
            sampler = ResourceSampler(name, args.output / "resources.jsonl")
            sampler.start()
            populate()
            for window in range(1, args.windows + 1):
                run_id = f"candidate_r{window}_p{args.population}"
                print(json.dumps({"starting_window": window, "players": args.population}), flush=True)
                offset = send(f"sim_reset_stats\nbench_begin {args.seconds} {run_id}")
                begin = time.monotonic()
                results["phase_events"].append({"phase": "measurement", "window": window, "host_monotonic": begin})
                destination = args.output / f"{run_id}.probe.json"
                deadline, next_health = begin + args.seconds + 20, begin + 15
                probe = None
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError("Server exited during measurement")
                    if destination.exists():
                        try:
                            probe = json.loads(destination.read_text())
                            raw = destination.with_name(f"{run_id}.frames.bin")
                            if raw.stat().st_size == probe["frames"] * 8:
                                break
                        except (ValueError, OSError):
                            pass
                        probe = None
                    if time.monotonic() >= next_health:
                        heartbeat(f"w{window}_{len(results['heartbeats'])}")
                        next_health = time.monotonic() + 15
                    time.sleep(0.2)
                if probe is None:
                    raise RuntimeError("Measurement did not finish with complete artifacts")
                results["phase_events"].append({"phase": "probe_artifacts_complete", "window": window,
                                                "host_monotonic": time.monotonic()})
                report_offset = send("sim_report\nrehldsm_stats")
                wait_for("[SIM] REPORT ", report_offset)
                time.sleep(0.3)
                text = read_since(offset)
                activity = activity_report(text)
                checks = validate_window(probe, activity, args.population, args.seconds)
                bench.save(args.output / f"{run_id}.activity.json", activity)
                bench.save(args.output / f"{run_id}.validation.json", {"passed": True, "checks": checks, "scope": "Measurement window; final shutdown checked separately"})
                (args.output / f"{run_id}.log").write_text(text, encoding="utf-8")
                results["windows"].append({"window": window, "passed": True, "frames": probe["frames"]})
                print(json.dumps(results["windows"][-1]), flush=True)
                if window < args.windows and args.map_every and window % args.map_every == 0:
                    results["phase_events"].append({"phase": "map_reload", "host_monotonic": time.monotonic()})
                    send("sim_stop\nchangelevel zm_deko2")
                    time.sleep(5)
                    heartbeat(f"map{results['map_reloads'] + 1}")
                    populate()
                    results["map_reloads"] += 1
                if window < args.windows and args.command_burst:
                    results["phase_events"].append({"phase": "command_burst", "host_monotonic": time.monotonic()})
                    send("\n".join(f"echo stability_burst_{window}_{i}" for i in range(args.command_burst)))
                    heartbeat(f"burst{window}")
            sampler.stop()  # Do not classify intentional process shutdown as a missing sample.
            results["phase_events"].append({"phase": "shutdown", "host_monotonic": time.monotonic()})
            send("sim_stop\nrehldsm_stats\nquit")
            results["exit_code"] = process.wait(timeout=20)
            text = read_since()
            if re.search(r"(?im)^.*(?:run time error|Host_Error|Segmentation fault).*$", text):
                raise RuntimeError("Runtime errors found; inspect the private log")
            if results["exit_code"] != 0:
                raise RuntimeError("Unclean server shutdown")
            reports = bench.parse_launcher_reports(text)
            if not reports:
                raise RuntimeError("Missing launcher telemetry")
            for report in reports:
                for counter in ("output_dropped", "output_write_failures", "reports_failed", "input_rejected", "completion_rejected"):
                    if counter not in report:
                        raise RuntimeError(f"Missing launcher counter: {counter}")
                    if report[counter]:
                        raise RuntimeError(f"Unexpected {counter} in monitored campaign")
            results["resource_samples"], results["resource_errors"] = sampler.samples, sampler.errors
            results["resource_fatal_error"] = sampler.fatal_error
            if sampler.errors or not sampler.samples or sampler.fatal_error:
                raise RuntimeError("Resource collection incomplete; do not call this a passing soak")
            results["launcher_reports"] = reports
            results["final_lifetime_stalls"] = validate_stall_counters(reports)
            results["resources"] = summarize_resources(args.output / "resources.jsonl")
            bench.save(args.output / "resource-summary.json", results["resources"])
        args.players, args.variants, args.repeats = [args.population], ["candidate"], args.windows
        bench.analyze(args, {"mode": "single_process_soak", "run_order": [
            {"variant": "candidate", "players": args.population, "repeat": i} for i in range(1, args.windows + 1)]})
        results["passed"] = True
    except Exception as error:
        results["error"] = type(error).__name__ + ": " + str(error)
        raise
    finally:
        cleanup_errors = []
        if sampler is not None:
            results["resource_samples"], results["resource_errors"] = sampler.samples, sampler.errors
            results["resource_fatal_error"] = sampler.fatal_error
        try:
            if sampler is not None:
                sampler.stop()
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=10)
        except Exception as error:
            cleanup_errors.append(type(error).__name__ + ": " + str(error))
        if created:
            try:
                removed = subprocess.run(["docker", "rm", "-f", "-v", name], capture_output=True, timeout=30)
                if removed.returncode:
                    cleanup_errors.append("Owned container removal failed")
            except Exception as error:
                cleanup_errors.append(type(error).__name__ + ": " + str(error))
        was_passed = results["passed"]
        if cleanup_errors:
            results["passed"] = False
            results["cleanup_errors"] = cleanup_errors
        bench.save(args.output / "runtime-results.json", results)
        if cleanup_errors and was_passed:
            raise RuntimeError("Cleanup incomplete; inspect runtime-results.json")
    print(json.dumps({"passed": True, "windows": len(results["windows"]), "map_reloads": results["map_reloads"],
                      "resource_samples": results["resource_samples"], "report": str(args.output / "REPORT.md")}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidate", "instrumentation", "config", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--image", default="rehldsm-player-load:20260927")
    parser.add_argument("--cpu", default="2")
    parser.add_argument("--players", dest="population", type=int, choices=(10, 32), default=32)
    parser.add_argument("--windows", type=int, choices=range(1, 61), default=6)
    parser.add_argument("--seconds", type=int, choices=range(2, 121), default=120)
    parser.add_argument("--warmup", type=int, default=60)
    parser.add_argument("--map-every", type=int, choices=range(0, 61), default=2)
    parser.add_argument("--command-burst", type=int, choices=range(0, 65), default=32)
    args = parser.parse_args()
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    for name in ("candidate", "instrumentation", "config", "output"):
        setattr(args, name, getattr(args, name).resolve())
    qualify(args)


if __name__ == "__main__":
    main()
