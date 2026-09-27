import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("package_release", Path(__file__).parents[1] / "tools/package_release.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class ReleaseEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "MoveToMonitorButton.exe").write_bytes(b"test fixture, not an executable")
        self.digest = PACKAGE.sha256(self.root / "MoveToMonitorButton.exe")
        self.info = dict(version="0.1.8", commit="abc", compiler="MSVC", officialSdk=True,
                         executableSha256=self.digest)
        self.write("build-info.json", self.info)
        self.write("startup-result.json", dict(passed=True, executableSha256=self.digest))
        self.cases = [dict(app=app, state=mode, status="passed", nativeSizeMatched=app != "Chrome",
                           hitPoints=5, stableSamples=8, pixelGlyph={"Changed": 10}, pixelHover={"Changed": 20})
                      for app in ("Notepad", "Explorer", "TaskScheduler", "WPF-fixture", "WinForms-fixture", "Chrome")
                      for mode in ("normal", "maximized", "restored-narrow")]
        self.results = dict(commit="abc", executableSha256=self.digest, cases=self.cases, fatalError=None)
        for platform in ("windows-2022", "windows-2025"):
            folder = "real-app-evidence/real-app-evidence-" + platform + "/"
            self.write(folder + "desktop-readiness.json", {"status": "ready"})
            self.write(folder + "results.json", self.results)

    def write(self, name, value):
        file = self.root / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(json.dumps(value), encoding="utf-8")

    def check_bad_result(self, update):
        value = copy.deepcopy(self.results)
        update(value)
        self.write("real-app-evidence/real-app-evidence-windows-2025/results.json", value)
        with self.assertRaises(ValueError):
            PACKAGE.verify(self.root, "abc")

    def test_valid_evidence(self):
        self.assertEqual(PACKAGE.verify(self.root, "abc"), ("0.1.8", self.digest))

    def test_mismatched_executable(self):
        (self.root / "MoveToMonitorButton.exe").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            PACKAGE.verify(self.root, "abc")

    def test_mismatched_commit(self):
        self.check_bad_result(lambda r: r.update(commit="other"))

    def test_missing_chrome(self):
        self.check_bad_result(lambda r: r.update(cases=r["cases"][:-3]))

    def test_native_size_failure(self):
        self.check_bad_result(lambda r: r["cases"][3].update(nativeSizeMatched=False))

    def test_empty_pixels(self):
        self.check_bad_result(lambda r: r["cases"][-1].update(pixelGlyph={"Changed": 0}))

    def test_blocked_desktop(self):
        self.write("real-app-evidence/real-app-evidence-windows-2025/desktop-readiness.json",
                   {"status": "environment-blocked"})
        with self.assertRaises(ValueError):
            PACKAGE.verify(self.root, "abc")

    def test_startup_failure(self):
        self.write("startup-result.json", dict(passed=False, executableSha256=self.digest))
        with self.assertRaises(ValueError):
            PACKAGE.verify(self.root, "abc")


if __name__ == "__main__":
    unittest.main()
