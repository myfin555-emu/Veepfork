from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]


class LoggingTests(unittest.TestCase):
    def test_disabled_arguments_once_and_diagnostic_precedence(self):
        with tempfile.TemporaryDirectory(prefix="vita3k-logging-test-") as directory:
            fixture = Path(directory) / "logging-fixture"
            subprocess.run([
                "clang++", "-std=c++23", "-O2", "-pthread", "-DFMT_HEADER_ONLY",
                "-DSPDLOG_FMT_EXTERNAL", "-I" + str(ROOT / "external/boost"),
                "-I" + str(ROOT / "external/fmt/include"),
                "-I" + str(ROOT / "external/spdlog/include"),
                "-I" + str(ROOT / "vita3k/util/include"),
                str(Path(__file__).with_name("logging_fixture.cpp")),
                "-o", str(fixture),
            ], check=True, capture_output=True)
            result = subprocess.run([fixture], check=True, capture_output=True, text=True)
            self.assertIn("0 argument evaluations", result.stdout)


if __name__ == "__main__":
    unittest.main()
