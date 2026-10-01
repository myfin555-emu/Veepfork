import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
SCRIPT = ROOT / "ios/Vita3K/scripts/diagnostics.py"


class DiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="vita3k-diagnostic-test-build-")
        cls.fixture = Path(cls.build.name) / "fixture"
        subprocess.run(["clang++", "-std=c++20", "-O1", "-pthread", "-DFMT_HEADER_ONLY",
                        "-DSPDLOG_FMT_EXTERNAL", "-I" + str(ROOT / "external/fmt/include"),
                        "-I" + str(ROOT / "external/spdlog/include"),
                        "-I" + str(ROOT / "vita3k/util/include"),
                        str(ROOT / "vita3k/util/src/diagnostics.cpp"),
                        str(Path(__file__).with_name("diagnostics_fixture.cpp")),
                        "-o", str(cls.fixture)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vita3k-diagnostic-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.documents = self.root / "Documents"
        self.documents.mkdir()
        self.case = self.root / "case"

    def cli(self, *args, success=True):
        result = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def arm(self, mode="compat"):
        self.cli("arm", "--mode", mode, "--documents", self.documents, "--case", self.case)

    def test_capture_lifecycle_and_concurrent_metrics(self):
        self.arm("performance")
        self.cli("status", "--case", self.case, success=False)
        subprocess.run([self.fixture, self.documents], check=True)
        self.cli("status", "--case", self.case)
        # A stale ACK from a prior process is not sufficient for a newly armed case.
        another = self.root / "another"
        self.cli("arm", "--mode", "performance", "--documents", self.documents, "--case", another)
        self.cli("status", "--case", another, success=False)
        self.cli("collect", "--case", self.case)
        state = json.loads((self.case / "case.json").read_text())
        collected = Path(state["last_collection"])
        analysis = json.loads(self.cli("analyze", collected / "diagnostics").stdout)
        first = next(game for game in analysis["games"] if game["title_id"] == "TEST00001")
        metric = first["metrics"]["pipeline"]
        self.assertEqual(metric["count"], 2000)
        self.assertEqual(metric["sum_us"], 2000 * 1234)
        self.assertEqual(metric["p95_upper_bound_ms"], 2)
        self.assertEqual(analysis["malformed_lines"], 0)
        self.assertEqual({game["title_id"] for game in analysis["games"]}, {"", "TEST00001", "TEST00002"})
        self.assertTrue((collected / "sha256.json").is_file())
        # Collection is additive, never replaces a previous evidence package.
        self.cli("collect", "--case", self.case)
        self.assertEqual(len(list(self.case.glob("collection-*"))), 2)
        self.cli("disarm", "--case", self.case)
        self.assertEqual((self.documents / "diagnostics-mode.txt").read_text(), "off\n")
        subprocess.run([self.fixture, self.documents], check=True)
        self.assertIn("mode=off", (self.documents / "diagnostics/active.txt").read_text())
        self.assertEqual(len(list((self.documents / "diagnostics").glob("capture-*"))), 1)

    def test_abrupt_termination_and_next_boot_preserve_evidence(self):
        self.arm()
        process = subprocess.Popen([self.fixture, self.documents, "kill"], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(process.stdout.readline().strip(), "ready")
            process.kill()
            process.wait(timeout=5)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()
        original = next((self.documents / "diagnostics").glob("capture-*/telemetry.jsonl"))
        contents = original.read_bytes()
        rows = [json.loads(line) for line in contents.splitlines()]
        self.assertEqual(rows[-1]["event"], "before_termination")
        self.assertEqual(next(row["detail"] for row in rows if row["event"] == "quoted"), 'ação\n"value"\\path')
        subprocess.run([self.fixture, self.documents], check=True)
        self.assertEqual(original.read_bytes(), contents)
        self.assertEqual(len(list((self.documents / "diagnostics").glob("capture-*"))), 2)

    def test_invalid_mode_fails_closed(self):
        (self.documents / "diagnostics-mode.txt").write_text("typo\n")
        subprocess.run([self.fixture, self.documents], check=True)
        active = (self.documents / "diagnostics/active.txt").read_text()
        self.assertIn("mode=off", active)
        self.assertIn("error=invalid", active)
        self.assertFalse(list((self.documents / "diagnostics").glob("capture-*")))

    def test_all_modes_and_incomplete_json_tail(self):
        self.arm("graphics")
        subprocess.run([self.fixture, self.documents], check=True)
        self.cli("status", "--case", self.case)
        telemetry = next((self.documents / "diagnostics").glob("capture-*/telemetry.jsonl"))
        with telemetry.open("a") as stream:
            stream.write('{"incomplete":')
        analysis = json.loads(self.cli("analyze", self.documents / "diagnostics").stdout)
        self.assertEqual(analysis["malformed_lines"], 1)
        self.assertTrue(analysis["games"])


if __name__ == "__main__":
    unittest.main()
