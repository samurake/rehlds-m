"""Isolated startup/version qualification. Never contacts FTP or a production server."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time
import uuid

PASSWORD = "rehldsm-isolated-test-only"


def rcon(command):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(4)
        address = ("127.0.0.1", 27015)
        sock.sendto(b"\xff\xff\xff\xffchallenge rcon\n", address)
        challenge = re.search(rb"challenge rcon (\d+)", sock.recv(4096))
        if not challenge:
            raise RuntimeError("No RCON challenge")
        packet = b"\xff\xff\xff\xffrcon " + challenge[1] + b' "' + PASSWORD.encode() + b'" ' + command.encode() + b"\n"
        sock.sendto(packet, address)
        return sock.recv(65536).decode(errors="replace").replace("\x00", "")


def prepare():
    destination = Path("/opt/hlds")
    shutil.copytree("/fixture", destination, dirs_exist_ok=True)
    for filename in ("hlds_linux", "libstdc++.so.6", "libgcc_s.so.1"):
        shutil.copy2(Path("/candidate") / filename, destination / filename)
    (destination / "hlds_linux").chmod(0o755)
    cfg = destination / "cstrike/server.cfg"
    with cfg.open("a") as stream:
        stream.write(f'\nrcon_password "{PASSWORD}"\nfps_max 0\nsys_ticrate 0\nsv_lan 1\n')
    (destination / "rehldsm.cfg").write_text(
        "target_fps=1500\nframe_stats_seconds=1\nasync_output=true\n"
        "output_queue_capacity=1024\noutput_flush_ms=10\n")


def check_config():
    env = dict(os.environ, LD_LIBRARY_PATH="/candidate")
    with tempfile.TemporaryDirectory(prefix="rehldsm-startup-") as temporary:
        root = Path(temporary)
        binary = root / "hlds_linux"
        shutil.copy2("/candidate/hlds_linux", binary)
        binary.chmod(0o755)
        cfg = root / "rehldsm.cfg"
        def invoke(*options):
            return subprocess.run([str(binary), *options], cwd="/", env=env, text=True, capture_output=True, timeout=5)
        missing = invoke("--check-config", "-pingboost", "5")
        assert missing.returncode == 0 and "target_fps=0 [default]" in missing.stdout
        cfg.write_text("target_fps=2000\nasync_output=true\n")
        overridden = invoke("--check-config", "-pingboost", "5")
        assert overridden.returncode == 0 and "target_fps=2000 [rehldsm.cfg]" in overridden.stdout
        removed = invoke("--check-config", "-targetfps", "1000")
        assert removed.returncode == 2 and "has been removed" in removed.stderr
        cfg.write_text("target_fps=2001\n")
        capped = invoke("--check-config")
        assert capped.returncode == 2 and "expected 0-2000" in capped.stderr
        cfg.write_text("output_flush_ms=9\n")
        flush_limit = invoke("--check-config")
        assert flush_limit.returncode == 2 and "expected 10-1000" in flush_limit.stderr
        cfg.write_text("unknown_setting=true\n")
        invalid = invoke("--check-config")
        assert invalid.returncode == 2 and "Startup configuration error" in invalid.stderr
        version = invoke("--version")
        assert version.returncode == 0 and version.stdout.startswith("ReHLDS-M ")
        print(json.dumps({"config_checks": "passed", "executable_directory_lookup": True,
                          "cfg_only_target": True, "target_ceiling": 2000,
                          "removed_cli_rejected": True, "flush_minimum_ms": 10,
                          "invalid_config_rejected_before_engine_load": True}), flush=True)


def qualify(args):
    candidate = args.candidate.resolve()
    fixture = args.fixture.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = "rehldsm-startup-" + uuid.uuid4().hex[:10]
    def run(*command, **kw):
        return subprocess.run(command, check=True, capture_output=True, text=True, **kw)
    def execute(*command, **kw):
        return run("docker", "exec", name, *command, **kw)
    def python_execute(*command, **kw):
        # The game image intentionally has no Python. A disposable helper shares
        # only this isolated container's network and volumes.
        return run("docker", "run", "--rm", "--network", "container:" + name,
                   "--volumes-from", name, "--entrypoint", "python3", args.helper_image, *command, **kw)
    def command(text):
        return python_execute("/audit/qualify-startup.py", "--rcon-command", text, timeout=8).stdout
    process = None
    created = False
    results = {}
    try:
        run("docker", "run", "-d", "--name", name, "--network", "none", "--security-opt", "no-new-privileges",
            *(["--cpuset-cpus", args.cpu] if args.cpu else []),
            "--memory", "1g", "--volume", "/opt/hlds", "--mount", f"type=bind,source={fixture},target=/fixture,readonly",
            "--mount", f"type=bind,source={candidate},target=/candidate,readonly",
            "--mount", f"type=bind,source={Path(__file__).resolve().parent},target=/audit,readonly",
            "--entrypoint", "sleep", args.image, "infinity")
        created = True
        print(python_execute("/audit/qualify-startup.py", "--check-config").stdout, flush=True)
        python_execute("/audit/qualify-startup.py", "--prepare-fixture", timeout=120)
        banner = execute("env", "LD_LIBRARY_PATH=/candidate", "/candidate/hlds_linux", "--version").stdout.strip()
        with (output / "runtime.log").open("wb") as log:
            process = subprocess.Popen(["docker", "exec", "-i", "-e", "LD_LIBRARY_PATH=/opt/hlds", "-w", "/opt/hlds", name,
                "./hlds_linux", "-game", "cstrike", "-console", "-insecure", "-nomaster",
                "-pingboost", "5", "-framestats", "30", "+ip", "127.0.0.1", "+maxplayers", "32", "+map", "zm_deko2"],
                stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 45
            while True:
                if process.poll() is not None:
                    raise RuntimeError("Launcher exited during startup")
                try:
                    version = command("rehldsm_version")
                    if banner in version:
                        break
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                    pass
                if time.monotonic() >= deadline:
                    raise RuntimeError("rehldsm_version did not respond through RCON")
                time.sleep(0.5)
            results["banner"] = banner
            results["rcon_version"] = banner in version
            results["rcon_case"] = banner in command("ReHLDSM_VERSION")
            # RCON executes one engine command directly; Cbuf console input is
            # the appropriate path to exercise semicolon command chaining.
            process.stdin.write(b"rehldsm_version; echo version_chain_ok\nmeta list\namxx modules\namxx plugins\n")
            process.stdin.flush()
            print(json.dumps({"rcon_version": results["rcon_version"], "banner": banner}), flush=True)
            command("sys_ticrate 0")
            command("fps_max 0")
            python_execute("-c", "from pathlib import Path; p=Path('/opt/hlds/rehldsm.cfg'); p.write_text(p.read_text().replace('target_fps=1500','target_fps=2000'))")
            command("echo configuration_edited_without_restart")
            time.sleep(3)
            results["stats"] = command("stats")
            for cycle in range(3):
                command("changelevel zm_deko2")
                time.sleep(4)
                assert banner in command("rehldsm_version")
                print(json.dumps({"map_reload": cycle + 1, "rcon_version": "passed"}), flush=True)
            process.stdin.write(b"rehldsm_stats\nquit\n")
            process.stdin.flush()
            results["exit_code"] = process.wait(timeout=15)
        text = (output / "runtime.log").read_text(errors="replace")
        results["config_target_and_report_precedence"] = "target_fps=1500 [rehldsm.cfg]" in text and "frame_stats_seconds=1 [rehldsm.cfg]" in text
        frame_medians = re.findall(r"frame_us\(p50/p95/p99\)=([0-9.]+)/", text)
        results["startup_only"] = "target_fps=2000" not in text and bool(frame_medians) and 620 <= float(frame_medians[-1]) <= 720
        results["console_chaining"] = "version_chain_ok" in text
        results["periodic_reports"] = text.count("launcher_iterations=") >= 3
        results["new_metrics"] = all(field in text for field in (
            "launcher_work_p99_us=", "window_over_budget=", "output_dropped=", "output_write_failures=", "reports_rejected="))
        results["no_bad_module_load"] = "bad load" not in text.lower() and "fail load" not in text.lower()
        results["metrics"] = [line for line in text.splitlines() if "launcher_iterations=" in line]
        # A second isolated startup deliberately selects the formerly uncapped
        # yield mode. Config target=0 must preserve that mode without bypassing
        # the fixed minimum iteration period.
        python_execute("-c", "from pathlib import Path; p=Path('/opt/hlds/rehldsm.cfg'); p.write_text(p.read_text().replace('target_fps=2000','target_fps=0'))")
        with (output / "pingboost-runtime.log").open("wb") as log:
            process = subprocess.Popen(["docker", "exec", "-i", "-e", "LD_LIBRARY_PATH=/opt/hlds", "-w", "/opt/hlds", name,
                "./hlds_linux", "-game", "cstrike", "-console", "-insecure", "-nomaster", "-pingboost", "5",
                "+ip", "127.0.0.1", "+maxplayers", "32", "+map", "zm_deko2"],
                stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 45
            while True:
                if process.poll() is not None:
                    raise RuntimeError("Launcher exited during pingboost startup")
                try:
                    if banner in command("rehldsm_version"):
                        break
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                    pass
                if time.monotonic() >= deadline:
                    raise RuntimeError("Pingboost fixture did not respond through RCON")
                time.sleep(0.5)
            time.sleep(3)
            process.stdin.write(b"rehldsm_stats\nquit\n"); process.stdin.flush()
            results["pingboost_exit_code"] = process.wait(timeout=15)
        pingboost_text = (output / "pingboost-runtime.log").read_text(errors="replace")
        pingboost_medians = [float(value) for value in re.findall(r"frame_us\(p50/p95/p99\)=([0-9.]+)/", pingboost_text)]
        results["pingboost_ceiling"] = bool(pingboost_medians) and min(pingboost_medians) >= 500 and results["pingboost_exit_code"] == 0
        results["pingboost_frame_p50_us"] = pingboost_medians
        print(json.dumps({"pingboost_ceiling": results["pingboost_ceiling"], "pingboost_frame_p50_us": pingboost_medians}), flush=True)
        results["limitations"] = "Isolated idle ZP fixture; no external network or real players; not production activation."
        (output / "runtime-results.json").write_text(json.dumps(results, indent=2) + "\n")
        required = ("rcon_version", "rcon_case", "console_chaining", "config_target_and_report_precedence", "startup_only", "periodic_reports", "new_metrics", "no_bad_module_load", "pingboost_ceiling")
        failures = [key for key in required if not results[key]]
        if failures or results["exit_code"] != 0:
            raise RuntimeError("Runtime qualification failed: " + ", ".join(failures) + f"; exit={results['exit_code']}")
        print(json.dumps({key: results[key] for key in (*required, "exit_code")}, indent=2), flush=True)
    finally:
        if process and process.poll() is None:
            process.kill()
            process.wait()
        if created:
            subprocess.run(["docker", "rm", "-f", "-v", name], capture_output=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--image", default="persistence-v4-rehlds-hlds:latest")
    parser.add_argument("--helper-image", default="rehldsm-linux-builder:compat-140rc2")
    parser.add_argument("--cpu", help="Optional allowed CPU set for the isolated fixture")
    parser.add_argument("--prepare-fixture", action="store_true")
    parser.add_argument("--check-config", action="store_true")
    parser.add_argument("--rcon-command")
    args = parser.parse_args()
    if args.prepare_fixture:
        prepare()
    elif args.check_config:
        check_config()
    elif args.rcon_command:
        print(rcon(args.rcon_command))
    else:
        if not all((args.candidate, args.fixture, args.output)):
            parser.error("--candidate, --fixture and --output are required")
        qualify(args)
