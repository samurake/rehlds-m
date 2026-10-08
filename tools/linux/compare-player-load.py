"""Compare launcher binaries using the existing isolated fake-client instrumentation.

Requires a prepared player-load image, frame_probe_mm_i386.so and player_load.amxx.
Never contacts FTP, publishes ports, or changes a production fixture. Raw server
logs remain local and may contain configuration details; do not publish them.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import random
import re
import statistics
import subprocess
import time
import uuid


PROFILES = {
    "classic": {"flags": [], "ticrate": 2000, "label": "Classic / ticrate 2000"},
    "rc2": {"flags": ["-targetfps", "2000", "-framestats", "60"],
            "ticrate": 0, "label": "RC2 / targetfps 2000"},
    "candidate": {"flags": [], "ticrate": 0, "label": "Candidate / cfg target_fps 2000"},
}
WEAPONS = ("scar", "vector", "augex", "mgl")


def docker(*args):
    result = subprocess.run(["docker", *map(str, args)], capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"docker {args[0]} failed: {result.stderr.strip()}")
    return result.stdout


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def run_case(args, variant, population, repeat, run_prefix):
    profile = PROFILES[variant]
    run_id = f"{variant}_r{repeat}_p{population}"
    name = f"rehldsm-compare-{run_prefix}-{run_id}"
    process = None
    created = False
    print(json.dumps({"starting": run_id}), flush=True)
    try:
        docker("run", "-d", "--name", name, "--network", "none",
               "--security-opt", "no-new-privileges", "--cpuset-cpus", args.cpu,
               "--memory", "1g", "--mount",
               f"type=bind,source={args.instrumentation},target=/bench,readonly",
               "--mount", f"type=bind,source={args.output},target=/results",
               "--entrypoint", "sleep", args.image, "infinity")
        created = True
        docker("cp", str(getattr(args, variant)), f"{name}:/opt/hlds/hlds_linux")
        docker("exec", name, "sh", "-c",
               "set -eu\nchmod u+x /opt/hlds/hlds_linux\n"
               "cp /bench/player_load.amxx /opt/hlds/cstrike/addons/amxmodx/plugins/\n"
               "cp /bench/frame_probe_mm_i386.so /opt/hlds/cstrike/addons/metamod/\n")
        if variant == "candidate":
            docker("cp", str(args.output / "candidate.rehldsm.cfg"),
                   f"{name}:/opt/hlds/rehldsm.cfg")
            checked = docker("exec", "-e", "LD_LIBRARY_PATH=/opt/hlds", name,
                             "/opt/hlds/hlds_linux", "--check-config")
            if "target_fps=2000 [rehldsm.cfg]" not in checked:
                raise RuntimeError(f"{run_id}: target configuration was not accepted")
            (args.output / f"{run_id}.config-check.txt").write_text(checked, encoding="utf-8")
        elif docker("exec", name, "sh", "-c",
                    "if test -f /opt/hlds/rehldsm.cfg; then echo present; fi").strip():
            raise RuntimeError("Baseline image unexpectedly contains a launcher config")
        log_path = args.output / f"{run_id}.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen([
                "docker", "exec", "-i", "-e", "LD_LIBRARY_PATH=/opt/hlds",
                "-w", "/opt/hlds", name, "./hlds_linux", "-game", "cstrike",
                "-console", "-insecure", "-nomaster", *profile["flags"],
                "+sys_ticrate", str(profile["ticrate"]), "+fps_max", "0",
                "+ip", "127.0.0.1", "+sv_lan", "1", "+maxplayers", "32",
                "+map", "zm_deko2"], stdin=subprocess.PIPE,
                stdout=log, stderr=subprocess.STDOUT)

            def send(command):
                if process.poll() is not None:
                    raise RuntimeError(f"{run_id}: server exited {process.returncode}")
                process.stdin.write((command + "\n").encode())
                process.stdin.flush()

            def wait_for(needle, timeout):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    if needle in log_path.read_text(errors="replace"):
                        return
                    if process.poll() is not None:
                        break
                    time.sleep(0.2)
                raise RuntimeError(f"{run_id}: missing {needle}; inspect the local log")

            wait_for("[FRAMEPROBE] attached", 35)
            time.sleep(5)
            send("version\nmeta list\namxx modules\namxx plugins")
            wait_for("17 modules, 17 correct", 15)
            send(f"sys_ticrate {profile['ticrate']}\nfps_max 0\nhost_framerate 0\n"
                 "sys_timescale 1\nmp_timelimit 0\nmp_roundtime 9\n"
                 "mp_round_infinite 1\nzp_gamemode_delay 9999\nfps_max")
            send(f"sim_population {population}")
            wait_for(f"[SIM] READY population={population}", 25)
            time.sleep(args.warmup)
            send("sim_report\nsim_debug")
            time.sleep(0.3)
            send(f"sim_reset_stats\nbench_begin {args.seconds} {run_id}")
            destination = args.output / f"{run_id}.probe.json"
            deadline = time.monotonic() + args.seconds + 20
            while not destination.exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"{run_id}: measurement did not finish")
                time.sleep(0.2)
            # The probe writes only after ending the timed window. Allow fclose.
            time.sleep(0.1)
            send("sim_report\nstats\namxx plugins\n" +
                 ("rehldsm_stats\n" if variant != "classic" else "") + "sim_stop")
            time.sleep(0.5)
            probe = json.loads(destination.read_text())
            content = log_path.read_text(errors="replace")
            records = re.findall(r"\[SIM\] REPORT ([^\r\n]+)", content)
            if not records:
                raise RuntimeError(f"{run_id}: no activity report")
            activity = {k: float(v) for k, v in
                        re.findall(r"(\w+)=(-?[\d.]+)", records[-1])}
            save(args.output / f"{run_id}.activity.json", activity)
            assert probe["sys_ticrate"] == profile["ticrate"], probe
            assert not probe["capacity_reached"] and probe["added_work_us"] == 0, probe
            assert probe["connected_min"] == probe["connected_max"] == population, probe
            assert probe["alive_min"] == probe["alive_max"] == population, probe
            assert activity["connected"] == activity["alive"] == population, activity
            assert activity["humans"] == activity["zombies"] == population / 2, activity
            assert activity["custom"] == population / 2 and activity["dead_steps"] == 0, activity
            assert activity["hits"] > 0 and activity["distance"] > 0, activity
            assert activity["commands"] > population * args.seconds * 90, activity
            assert all(activity[f"shots_{key}"] > 0 for key in WEAPONS), activity
            assert 0.98 < probe["simulation_seconds"] / probe["wall_seconds"] < 1.02, probe
            assert re.search(r'"fps_max" is "0(?:\.0*)?"', content), "fps_max was not zero"
            errors = re.findall(r"(?im)^.*(?:run time error|Host_Error|Segmentation fault).*$", content)
            if errors:
                raise RuntimeError(f"{run_id}: runtime errors found; inspect the local log")
            send("quit")
            code = process.wait(timeout=20)
            if code:
                raise RuntimeError(f"{run_id}: shutdown exit={code}")
        cgroup = docker("exec", name, "sh", "-c",
                        "cat /sys/fs/cgroup/cpu.stat; cat /sys/fs/cgroup/cpu.max; "
                        "cat /sys/fs/cgroup/cpuset.cpus.effective")
        (args.output / f"{run_id}.cgroup.txt").write_text(cgroup, encoding="utf-8")
        if "nr_throttled 0" not in cgroup or "max 100000" not in cgroup:
            raise RuntimeError(f"{run_id}: CPU quota/throttling invalidates this run")
        save(args.output / f"{run_id}.validation.json", {
            "passed": True, "shutdown_exit_code": code, "probe_overflow": False,
            "gameplay_checks": "passed", "observed_cpu_throttling": False})
        print(json.dumps({"completed": run_id, "frames": probe["frames"],
                          "cpu_percent": probe["cpu_percent_one_core"],
                          "alive_min": probe["alive_min"], "hits": activity["hits"],
                          "shots": sum(activity[f"shots_{key}"] for key in WEAPONS)}), flush=True)
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        if created:
            subprocess.run(["docker", "rm", "-f", "-v", name], capture_output=True)


def analyze(args, manifest):
    import numpy as np
    rows = []
    for item in manifest["run_order"]:
        variant, population, repeat = item["variant"], item["players"], item["repeat"]
        run_id = f"{variant}_r{repeat}_p{population}"
        probe = json.loads((args.output / f"{run_id}.probe.json").read_text())
        activity = json.loads((args.output / f"{run_id}.activity.json").read_text())
        times = np.frombuffer((args.output / f"{run_id}.frames.bin").read_bytes(), dtype="<u8")
        assert len(times) == probe["frames"] and np.all(times[1:] > times[:-1])
        gaps = np.diff(times).astype(np.float64) / 1e6
        fps = (len(times) - 1) * 1e9 / int(times[-1] - times[0])
        buckets = np.bincount(((times - times[0]) // 1_000_000_000).astype(int))[:-1]
        rows.append({"run_id": run_id, "variant": variant, "players": population,
                     "repeat": repeat, "fps": fps,
                     "cpu_percent": probe["cpu_percent_one_core"],
                     "cpu_us_per_frame": probe["cpu_percent_one_core"] * 10000 / fps,
                     "p50_ms": float(np.percentile(gaps, 50)),
                     "p95_ms": float(np.percentile(gaps, 95)),
                     "p99_ms": float(np.percentile(gaps, 99)),
                     "p999_ms": float(np.percentile(gaps, 99.9)),
                     "max_gap_ms": float(gaps.max()), "min_one_second_fps": int(buckets.min()),
                     "gaps_over_1ms_percent": float((gaps > 1).mean() * 100),
                     "gaps_over_5ms_percent": float((gaps > 5).mean() * 100),
                     "peak_rss_mib": probe["maxrss_kib"] / 1024,
                     "simulation_wall_ratio": probe["simulation_seconds"] / probe["wall_seconds"],
                     "movement_commands_per_player_second": activity["commands"] / population / activity["seconds"],
                     "shots_per_second": sum(activity[f"shots_{key}"] for key in WEAPONS) / activity["seconds"],
                     "hits_per_second": activity["hits"] / activity["seconds"],
                     "dead_steps": activity["dead_steps"], "skipped_steps": activity["skipped_steps"]})
        for threshold in (5, 10, 50):
            count = int((gaps > threshold).sum())
            rows[-1][f"stalls_over_{threshold}ms"] = count
            rows[-1][f"stalls_over_{threshold}ms_per_minute"] = count * 60 / probe["wall_seconds"]
        # Console snapshots are not aligned with the continuous engine probe window.
        reports = parse_launcher_reports((args.output / f"{run_id}.log").read_text(errors="replace"))
        save(args.output / f"{run_id}.launcher-metrics.json", {
            "scope": "Process-lifetime snapshots, not aligned with the engine probe window",
            "reports": reports})
    save(args.output / "measurements.json", rows)
    with (args.output / "measurements.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    summary = []
    for population in args.players:
        for variant in args.variants:
            runs = [row for row in rows if row["variant"] == variant and row["players"] == population]
            entry = {"variant": variant, "players": population, "runs": len(runs)}
            for key in rows[0]:
                if key in ("run_id", "variant", "players", "repeat"):
                    continue
                values = [row[key] for row in runs]
                entry[key] = statistics.median(values)
                entry[key + "_range"] = [min(values), max(values)]
            summary.append(entry)
    save(args.output / "summary.json", summary)
    soak = manifest.get("mode") == "single_process_soak"
    title = "Local candidate single-process soak" if soak else (
        "Local candidate player-load qualification" if args.variants == ["candidate"] else "Launcher player-load comparison")
    measurement_scope = f"{len(rows)} windows in one process" if soak else f"{len(rows)} fresh processes"
    lines = [f"# {title}", "",
             f"Measured locally: {measurement_scope}; {args.repeats} measurements per cell; "
             f"{args.warmup}s combat warmup + {args.seconds}s measurement.", "",
             "Same prepared ReHLDS engine/game/plugins/map and runtime libraries for every launcher. "
             "Serial runs pinned to one logical CPU, 1 GiB container limit, no CPU quota, no external network.", "",
             "## Profiles", ""]
    descriptions = {
        "classic": "Classic: sys_ticrate 2000, fps_max 0, default classic sleep; no native targetfps support.",
        "rc2": "RC2: -targetfps 2000 -framestats 60; sys_ticrate 0, fps_max 0.",
        "candidate": "Candidate: executable-adjacent rehldsm.cfg, target_fps=2000; sys_ticrate 0, fps_max 0. "
                     "Exact reporting/output settings are preserved in candidate.rehldsm.cfg; no -targetfps flag.",
    }
    lines += ["- " + descriptions[variant] for variant in args.variants]
    population_scope = "Medians of windows in ONE process; windows are not independent runs. " if soak else "Medians of independent runs; "
    lines += ["", "## Results", "", population_scope + "Brackets show the full observed FPS range. "
             "CPU is total server process CPU as a percentage of one logical core, including workers. "
             "FPS counts accepted engine StartFrame callbacks, not launcher loop attempts.", "",
             "| Players | Launcher | Engine FPS [range] | CPU/core | p99 gap (ms) | Worst 1s FPS | Peak RSS (MiB) |",
             "|---:|---|---:|---:|---:|---:|---:|"]
    for entry in summary:
        lo, hi = entry["fps_range"]
        lines.append(f"| {entry['players']} | {PROFILES[entry['variant']]['label']} | "
                     f"{entry['fps']:.1f} [{lo:.1f}-{hi:.1f}] | {entry['cpu_percent']:.1f}% | "
                     f"{entry['p99_ms']:.3f} | {int(entry['min_one_second_fps_range'][0])} | "
                     f"{entry['peak_rss_mib']:.1f} |")
    lines += ["", "## Validation and limitations", "",
              "All included runs passed population/alive checks throughout the measurement, "
              "half human/half zombie roles, custom-weapon ownership, movement, damage and "
              "SCAR/Vector/AUGEX/MGL shots; no added busywork, probe overflow, server crash, "
              "AMXX runtime errors or observed container-local CPU throttling. Shutdown was clean. "
              "These checks do not rule out contention or bandwidth limits in ancestor cgroups.", "",
              "Workload uses scripted engine fake clients at 100 Hz simulation time, not real remote "
              "players or AI. It exercises game/plugin combat and movement, but not real-client "
              "networking, host-specific scheduling, database availability or long-running stability. "
              "This is launcher-only testing against a prepared ReHLDS engine fixture. "
              "Offline fixture services may issue expected connection warnings.", "",
              "Repeated scripted runs are descriptive, not proof of statistical significance or production stability. "
              "A 2000 launcher target is a pacing request, not a guarantee of 2000 accepted engine frames. "
              "Compare FPS and CPU ranges before interpreting small differences as improvements.", "",
              "## Reproducibility", "",
              "See manifest.json for exact artifact SHA-256s, image ID, flags, config, CPU, fixture "
              "fingerprints and campaign settings. measurements.csv/json and summary.json contain "
              "all per-run measurements; *.frames.bin preserve raw monotonic timestamps. "
              "Raw *.log files stay private/local; do not publish fixture configuration or credentials.", ""]
    lines += ["## Stability detail", "",
              "measurements.json/csv include p99.9, maximum gap and counts/rates above 5, 10 and 50 ms. "
              "Launcher reports are saved separately as *.launcher-metrics.json; their rolling "
              "percentiles must not be treated as full-window engine measurements."]
    if soak:
        lines += ["", "This lifecycle campaign adds resource sampling and console heartbeats. "
                  "Map changes, command bursts, warmups and probe-file dumps are outside measured windows. "
                  "Review runtime-results.json and resources.jsonl for those phases. This is not directly "
                  "comparable with an unmonitored steady-state benchmark."]
    (args.output / "REPORT.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"completed_runs": len(rows), "report": str(args.output / "REPORT.md")}), flush=True)


def parse_launcher_reports(text):
    """Extract numeric launcher telemetry only, never arbitrary console text."""
    reports = []
    allowed = {"launcher_iterations", "window", "engine_call_p99_us", "wake_p99_us", "work_p99_us",
               "launcher_work_p99_us", "window_max_frame_us", "max_frame_us", "window_over_budget",
               "over_budget_frames", "engine_reported_fps", "input_high_water", "input_rejected",
               "completion_rejected", "output_async", "output_pending", "output_high_water",
               "output_dropped", "output_write_failures", "reports_rejected", "reports_failed",
               "lifetime_launcher_gt_1ms", "lifetime_launcher_gt_5ms", "lifetime_launcher_gt_10ms",
               "lifetime_launcher_gt_50ms"}
    for line in text.splitlines():
        if not line.startswith("[ReHLDS-M] launcher_iterations="):
            continue
        reports.append({key: float(value) if "." in value else int(value)
                        for key, value in re.findall(r"\b([a-z][a-z0-9_]*)=(-?\d+(?:\.\d+)?)", line) if key in allowed})
    return reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("classic", "rc2", "candidate"):
        parser.add_argument("--" + name, type=Path)
    for name in ("instrumentation", "output", "config"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--image", default="rehldsm-player-load:20260927")
    parser.add_argument("--cpu", default="2")
    parser.add_argument("--seconds", type=int, default=30, choices=range(2, 121))
    parser.add_argument("--warmup", type=int, default=8)
    parser.add_argument("--repeats", type=int, default=3, choices=range(1, 11))
    parser.add_argument("--players", nargs="+", type=int, choices=(10, 32), default=[10, 32])
    parser.add_argument("--variants", nargs="+", choices=tuple(PROFILES), default=list(PROFILES))
    parser.add_argument("--seed", type=int, default=20261008)
    args = parser.parse_args()
    for variant in args.variants:
        if getattr(args, variant) is None:
            parser.error(f"--{variant} is required when testing that variant")
    for name in ("classic", "rc2", "candidate", "instrumentation", "output", "config"):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())
    if args.warmup < 0 or args.output.exists():
        raise RuntimeError("Choose a fresh output directory and non-negative warmup")
    artifacts = {name: {"path": str(getattr(args, name)), "sha256": sha(getattr(args, name))}
                 for name in args.variants}
    for name in ("rc2", "candidate"):
        if name not in args.variants:
            continue
        info = getattr(args, name).parent / "build-info.json"
        if info.exists():
            artifacts[name]["build_info"] = json.loads(info.read_text())
    artifacts.update({name: {"path": str(args.instrumentation / name),
                             "sha256": sha(args.instrumentation / name)}
                      for name in ("frame_probe_mm_i386.so", "player_load.amxx",
                                   "frame_probe.cpp", "player_load.sma")})
    cfg = args.config.read_text(encoding="utf-8")
    cfg, count = re.subn(r"(?m)^target_fps\s*=.*$", "target_fps = 2000", cfg)
    if count != 1:
        raise RuntimeError("Expected exactly one target_fps line in the candidate config")
    args.output.mkdir(parents=True)
    (args.output / "candidate.rehldsm.cfg").write_text(cfg, encoding="utf-8")
    environment = docker("run", "--rm", "--network", "none", "--entrypoint", "sh", args.image,
                         "-c", "uname -a; getconf GNU_LIBC_VERSION; nproc; "
                         "sha256sum /opt/hlds/engine_i486.so /opt/hlds/cstrike/dlls/cs.so "
                         "/opt/hlds/libstdc++.so.6 /opt/hlds/libgcc_s.so.1")
    rng = random.Random(args.seed)
    order = []
    for repeat in range(1, args.repeats + 1):
        cases = [(variant, n) for variant in args.variants for n in args.players]
        rng.shuffle(cases)
        order += [{"variant": variant, "players": n, "repeat": repeat} for variant, n in cases]
    manifest = {"config": {key: str(value) if isinstance(value, Path) else value
                           for key, value in vars(args).items()},
                "profiles": PROFILES, "artifacts": artifacts, "run_order": order,
                "image_id": docker("image", "inspect", args.image, "--format", "{{.Id}}").strip(),
                "environment": environment, "runner_sha256": sha(Path(__file__)),
                "candidate_config_sha256": sha(args.output / "candidate.rehldsm.cfg"),
                "scope": "Isolated scripted fake clients; same engine, fixture and runtimes; no network clients or AI",
                "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    save(args.output / "manifest.json", manifest)
    prefix = uuid.uuid4().hex[:8]
    current_run = None
    try:
        for item in order:
            current_run = item
            run_case(args, item["variant"], item["players"], item["repeat"], prefix)
        analyze(args, manifest)
    except Exception as error:
        save(args.output / "failure.json", {"error": str(error), "failed_run": current_run,
                                            "completed_runs": len(list(args.output.glob("*.validation.json")))})
        raise


if __name__ == "__main__":
    main()
