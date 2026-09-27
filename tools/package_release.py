"""Package only the exact EXE whose startup and real-desktop evidence passed."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile


def read_json(path: Path) -> dict:
    # PowerShell 5 Tee-Object writes UTF-16 with a BOM; pwsh/Set-Content
    # commonly write UTF-8. json.loads(bytes) detects Unicode encodings without
    # lossy replacement, so preserve the evidence and still reject invalid JSON.
    return json.loads(path.read_bytes())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(root: Path, commit: str) -> tuple[str, str]:
    info = read_json(root / "build-info.json")
    version = info["version"]
    require(bool(re.fullmatch(r"\d+\.\d+\.\d+", version)), "Invalid release version")
    require(info["commit"] == commit, "Artifact commit does not match this workflow")
    require(info["compiler"] == "MSVC" and info["officialSdk"] is True, "Not an official SDK build")
    digest = sha256(root / "MoveToMonitorButton.exe")
    require(info["executableSha256"] == digest, "Build hash mismatch")
    startup = read_json(root / "startup-result.json")
    require(startup["executableSha256"] == digest and startup["passed"] is True, "Startup not verified")
    for platform in ("windows-2022", "windows-2025"):
        folder = root / "real-app-evidence" / ("real-app-evidence-" + platform)
        require(read_json(folder / "desktop-readiness.json")["status"] == "ready", "Desktop not ready")
        inactive = read_json(folder / "inactive-result.json")
        require(inactive["passed"] is True, "Inactive overlay integration failed")
        require(inactive["executableSha256"] == digest and inactive["commit"] == commit,
                "Inactive evidence is for a different commit or EXE")
        results = read_json(folder / "results.json")
        require(results["commit"] == commit and results["executableSha256"] == digest,
                "Desktop evidence is for a different commit or EXE")
        require(results.get("fatalError") is None, "Fatal desktop test error")
        require(not any(case["status"] == "failed" for case in results["cases"]), "A real-app case failed")
        for app in ("Notepad", "Explorer", "TaskScheduler", "WPF-fixture", "WinForms-fixture", "Chrome"):
            for mode in ("normal", "maximized", "restored-narrow"):
                cases = [c for c in results["cases"] if c["app"] == app and c["state"] == mode]
                label = f"{platform}/{app}/{mode}"
                require(len(cases) == 1 and cases[0]["status"] == "passed", f"Missing or failed: {label}")
                case = cases[0]
                if app != "Chrome":
                    require(case["nativeSizeMatched"] is True, f"Native size mismatch: {label}")
                require(case["hitPoints"] == 5 and case["stableSamples"] == 8, f"Incomplete probes: {label}")
                require(case["pixelGlyph"]["Changed"] >= 6 and case["pixelHover"]["Changed"] >= 6,
                        f"Rendering not observed: {label}")
    return version, digest


def write_zip(path: Path, files: list[Path], root: Path) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(files):
            require(file.is_file() and not file.is_symlink(), "Only regular files may be packaged")
            archive.write(file, file.relative_to(root).as_posix())


def package(root: Path, output: Path, commit: str) -> tuple[str, str]:
    version, digest = verify(root, commit)
    require(not output.exists(), "Refusing to replace existing release files")
    output.mkdir(parents=True)
    shutil.copy2(root / "MoveToMonitorButton.exe", output / "MoveToMonitorButton.exe")
    application = [root / name for name in (
        "MoveToMonitorButton.exe", "README.md", "LICENSE", "Check-Startup.cmd",
        "build-info.json", "startup-result.json", "test-results.xml", "SHA256SUMS.txt")]
    application += list((root / "docs").glob("*.md"))
    portable = output / f"MoveToMonitorButton-v{version}-windows-x64.zip"
    write_zip(portable, application, root)
    evidence = [file for file in (root / "real-app-evidence").rglob("*")
                if file.is_file() and file.suffix.lower() in (".json", ".png", ".bmp", ".md", ".txt")]
    evidence += [root / "build-info.json", root / "startup-result.json", root / "test-results.xml"]
    write_zip(output / f"MoveToMonitorButton-v{version}-test-evidence.zip", evidence, root)
    (output / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256(file)}  {file.name}\n" for file in sorted(output.iterdir())), encoding="ascii")
    print(f"Verified {version}: 36 real-app cases, including 30 native-size matches; EXE {digest}")
    return version, digest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    package(args.root, args.output, args.commit)
