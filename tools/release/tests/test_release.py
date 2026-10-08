"""Offline checks for release identity, build inputs and documentation links."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parents[1] / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load("release_package", "package.py")
docs = load("release_docs", "check_docs.py")
notes = load("release_notes", "notes.py")
windows_runtime = load("windows_runtime", "../windows/runtime.py")


class ReleaseTests(unittest.TestCase):
    def test_stable_and_prerelease_identity(self):
        with patch.object(package, "git", return_value="a" * 40):
            version = package.identity()["version"]
            self.assertEqual(package.identity("v" + version)["prerelease"], "false")
            self.assertEqual(package.identity("v" + version + "-rc.2")["prerelease"], "true")
            with self.assertRaises(ValueError):
                package.identity("v999.999.999")

    def test_build_inputs_require_provenance_and_matching_config(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            cfg = (package.ROOT / "data/rehldsm.cfg").read_bytes()
            (source / "rehldsm.cfg").write_bytes(cfg)
            with self.assertRaises(FileNotFoundError):
                package.validate_build_inputs(source)
            record = {"cpu_baseline": "i386 / SSE4.2; no march=native", "exceptions": True}
            (source / "build-provenance.json").write_text(json.dumps(record))
            package.validate_build_inputs(source)
            (source / "rehldsm.cfg").write_bytes(cfg + b"# changed\n")
            with self.assertRaisesRegex(ValueError, "configuration"):
                package.validate_build_inputs(source)

    def test_native_or_no_exception_build_is_not_a_portable_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            for record in ({"cpu_baseline": "native", "exceptions": True},
                           {"cpu_baseline": "i386 / SSE4.2; no march=native", "exceptions": False}):
                (source / "build-provenance.json").write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, "portable CPU"):
                    package.validate_build_inputs(source)

    def test_relative_documentation_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "docs").mkdir()
            (root / "docs/config.md").write_text("# Configuration\n")
            (root / "README.md").write_text(
                "[cfg](docs/config.md#settings) [web](https://example.com) [bad](docs/missing.md)\n")
            self.assertEqual(docs.broken_links(root), ["README.md:1: docs/missing.md"])

    def test_release_notes_pin_links_and_include_both_installation_guides(self):
        revision = "a" * 40
        with patch.object(package, "git", return_value=revision):
            version = package.identity()["version"]
        rendered = notes.render(version, "samurake/rehlds-m", revision)
        self.assertTrue(rendered.startswith("# ReHLDS-M " + version))
        self.assertIn("Linux installation: files and destination", rendered)
        self.assertIn("Windows installation: files and destination", rendered)
        self.assertIn("rehldsm_version", rendered)
        self.assertIn(f"https://github.com/samurake/rehlds-m/blob/{revision}/", rendered)
        self.assertNotIn("](../../", rendered)

    def test_release_notes_reject_invalid_identity_and_missing_links(self):
        with self.assertRaises(ValueError):
            notes.render("1.4.0", "samurake/rehlds-m", "short")
        with self.assertRaises(ValueError):
            notes.render("1.4.0", "invalid/repo/extra", "a" * 40)
        with self.assertRaises(ValueError):
            notes.source_links("[bad](missing.md)", notes.ROOT / "README.md", "owner/repo", "a" * 40)

    def test_windows_runtime_stages_only_transitive_launcher_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            crt = root / "redist/x86/Microsoft.VC143.CRT"
            crt.mkdir(parents=True)
            notice = root / "vs/Licenses/1033/Redist.txt"
            notice.parent.mkdir(parents=True)
            notice.write_text("test redistribution notice")
            destination = root / "staging"
            destination.mkdir()
            (destination / "hlds.exe").write_bytes(b"launcher")
            imports = {"hlds.exe": ["MSVCP140.dll", "MSVCP140_ATOMIC_WAIT.dll", "KERNEL32.dll"],
                       "msvcp140.dll": ["VCRUNTIME140.dll"],
                       "msvcp140_atomic_wait.dll": ["MSVCP140.dll", "VCRUNTIME140.dll"],
                       "vcruntime140.dll": ["api-ms-win-crt-runtime-l1-1-0.dll"]}
            for name in (*list(imports)[1:], "unused.dll"):
                (crt / name).write_bytes(name.encode())
            environment = {"VCToolsRedistDir": str(root / "redist"), "VSINSTALLDIR": str(root / "vs")}
            with patch.dict(os.environ, environment), patch.object(
                    windows_runtime, "inspect_dependencies", side_effect=lambda file: imports[file.name]):
                windows_runtime.stage(destination)
            manifest = json.loads((destination / "artifact-manifest.json").read_text())
            self.assertEqual({record["file"] for record in manifest["files"]}, set(imports))
            self.assertFalse((destination / "unused.dll").exists())
            self.assertIn("kernel32.dll", manifest["host_libraries"])

    def test_windows_runtime_rejects_unknown_imports(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            crt = root / "x86/Microsoft.VC143.CRT"
            crt.mkdir(parents=True)
            (crt / "unused.dll").write_bytes(b"unused")
            with patch.dict(os.environ, {"VCToolsRedistDir": str(root)}), patch.object(
                    windows_runtime, "inspect_dependencies", return_value=["unknown.dll"]):
                with self.assertRaisesRegex(RuntimeError, "Unbundled dependency"):
                    windows_runtime.stage(root)


if __name__ == "__main__":
    unittest.main()
