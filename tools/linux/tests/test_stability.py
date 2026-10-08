"""Offline tests for the local qualification helpers (never start Docker)."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parents[1] / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bench = load("bench_tests", "compare-player-load.py")
soak = load("soak_tests", "soak-candidate.py")


class StabilityHelpersTest(unittest.TestCase):
    def test_report_parser_only_exports_known_numeric_telemetry(self):
        text = "private configuration password=123\n[ReHLDS-M] launcher_iterations=42 window=2048 "
        text += "engine_call_p99_us=123.4 output_dropped=0 lifetime_launcher_gt_5ms=7 password=123\n"
        reports = bench.parse_launcher_reports(text)
        self.assertEqual(reports, [{"launcher_iterations": 42, "window": 2048,
                                  "engine_call_p99_us": 123.4, "output_dropped": 0, "lifetime_launcher_gt_5ms": 7}])

    def test_resource_reader_preserves_counters_and_marks_missing_fields(self):
        text = "section status\nVmRSS: 180000\nThreads: 4\nsection open_fds\n19\nsection cpu.stat\nnr_throttled 0\n"
        result = soak.parse_resources(text)
        self.assertEqual(result["VmRSS"], 180000)
        self.assertEqual(result["open_fds"], 19)
        self.assertEqual(result["sections"]["cpu.stat"], ["nr_throttled 0"])
        with self.assertRaises(ValueError):
            soak.parse_resources("section status\nThreads: 4\n")
        with self.assertRaises(ValueError):
            soak.parse_resources(text.replace("19\n", "0\n"))

    def test_window_validation_rejects_lost_population_and_probe_overflow(self):
        probe = dict(capacity_reached=False, added_work_us=0, connected_min=32, connected_max=32,
                     alive_min=32, alive_max=32, simulation_seconds=120, wall_seconds=120, sys_ticrate=0)
        activity = dict(connected=32, alive=32, humans=16, zombies=16, custom=16, hits=10,
                        distance=1000, commands=32 * 120 * 100, dead_steps=0)
        activity.update({"shots_" + key: 10 for key in bench.WEAPONS})
        self.assertTrue(all(soak.validate_window(probe, activity, 32, 120).values()))
        for changed in ({"alive_min": 31}, {"capacity_reached": True}, {"simulation_seconds": 100}):
            with self.assertRaises(RuntimeError):
                soak.validate_window(dict(probe, **changed), activity, 32, 120)

    def test_activity_parser_uses_latest_report(self):
        self.assertEqual(soak.activity_report("[SIM] REPORT hits=1\n[SIM] REPORT hits=2 alive=32\n"),
                         {"hits": 2, "alive": 32})
        with self.assertRaises(RuntimeError):
            soak.activity_report("[SIM] READY population=32")

    def test_cumulative_counters_cannot_disappear_reset_or_invert(self):
        report = dict(launcher_iterations=100)
        report.update({f"lifetime_launcher_gt_{key}ms": value
                       for key, value in zip((1, 5, 10, 50), (10, 5, 2, 1))})
        self.assertEqual(soak.validate_stall_counters([report, report])["lifetime_launcher_gt_5ms"], 5)
        for invalid in ({"lifetime_launcher_gt_5ms": 4}, {"lifetime_launcher_gt_10ms": 6},
                        {"launcher_iterations": 9}):
            with self.assertRaises(RuntimeError):
                soak.validate_stall_counters([report, dict(report, **invalid)])
        with self.assertRaises(RuntimeError):
            soak.validate_stall_counters([{"launcher_iterations": 100}])

    def test_health_requires_engine_response_not_the_input_thread_echo(self):
        token = "stability_health_w1_0"
        self.assertFalse(soak.has_marker("echo " + token + "\n", token, exact_line=True))
        self.assertFalse(soak.has_marker(token + "_stale\n", token, exact_line=True))
        self.assertTrue(soak.has_marker("echo " + token + "\n" + token + "\r\n", token, exact_line=True))
        # The fixture's engine echo appends a space before its newline.
        self.assertTrue(soak.has_marker(token + " \n", token, exact_line=True))

    def test_resource_summary_rejects_missing_samples_and_resource_limit_events(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "resources.jsonl"
            sample = dict(VmRSS=180000, VmSize=500000, VmData=100000, Threads=4, open_fds=19,
                          sections={"cpu.stat": ["nr_throttled 0"], "memory.events": ["oom_kill 0"]})
            path.write_text(json.dumps(sample) + "\n" + json.dumps(sample) + "\n")
            self.assertEqual(soak.summarize_resources(path)["nr_throttled_delta"], 0)
            changed = dict(sample, sections={"cpu.stat": ["nr_throttled 1"], "memory.events": ["oom_kill 0"]})
            path.write_text(json.dumps(sample) + "\n" + json.dumps(changed) + "\n")
            with self.assertRaises(RuntimeError):
                soak.summarize_resources(path)
            path.write_text(json.dumps(sample) + '\n{"missing_sample":"timeout"}\n')
            with self.assertRaises(RuntimeError):
                soak.summarize_resources(path)

    def test_candidate_only_requires_no_other_launcher_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary, config, instrumentation, output = root / "hlds_linux", root / "config", root / "instrumentation", root / "evidence"
            binary.write_bytes(b"test-binary")
            config.write_text("target_fps=1500\n")
            instrumentation.mkdir()
            for name in ("frame_probe_mm_i386.so", "player_load.amxx", "frame_probe.cpp", "player_load.sma"):
                (instrumentation / name).write_bytes(b"test-input")
            argv = ["compare-player-load.py", "--candidate", str(binary), "--config", str(config),
                    "--instrumentation", str(instrumentation), "--output", str(output), "--variants", "candidate",
                    "--players", "32", "--repeats", "1"]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(bench, "docker", return_value="fixture"), \
                 mock.patch.object(bench, "run_case") as run_case, mock.patch.object(bench, "analyze"):
                bench.main()
            self.assertEqual(run_case.call_args.args[1], "candidate")
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertNotIn("rc2", manifest["artifacts"])
            self.assertNotIn("classic", manifest["artifacts"])
            self.assertIn("target_fps = 2000", (output / "candidate.rehldsm.cfg").read_text())

    def test_analysis_exports_tail_rates_and_truthful_soak_scope(self):
        import numpy as np
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            name = "candidate_r1_p32"
            times = np.arange(3000, dtype=np.uint64) * 500000
            times[1000:] += 11000000
            times.tofile(root / f"{name}.frames.bin")
            bench.save(root / f"{name}.probe.json", dict(frames=len(times), wall_seconds=1.51,
                       cpu_percent_one_core=17, maxrss_kib=180000, simulation_seconds=1.51))
            activity = dict(seconds=1.51, commands=4800, hits=10, dead_steps=0, skipped_steps=0)
            activity.update({"shots_" + key: 10 for key in bench.WEAPONS})
            bench.save(root / f"{name}.activity.json", activity)
            (root / f"{name}.log").write_text("[ReHLDS-M] launcher_iterations=3000 output_dropped=0\n")
            args = SimpleNamespace(output=root, players=[32], variants=["candidate"], repeats=1, warmup=60, seconds=120)
            bench.analyze(args, {"mode": "single_process_soak", "run_order": [dict(variant="candidate", players=32, repeat=1)]})
            measurements = json.loads((root / "measurements.json").read_text())[0]
            self.assertEqual(measurements["stalls_over_10ms"], 1)
            self.assertEqual(measurements["stalls_over_50ms"], 0)
            self.assertAlmostEqual(measurements["stalls_over_10ms_per_minute"], 60 / 1.51)
            report = (root / "REPORT.md").read_text()
            self.assertIn("windows are not independent runs", report)
            self.assertNotIn("- RC2:", report)


if __name__ == "__main__":
    unittest.main()
