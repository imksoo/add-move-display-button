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
    for platform in ("windows-2022", "windows-2025", "windows-11-arm"):
        folder = root / "real-app-evidence" / ("real-app-evidence-" + platform)
        require(read_json(folder / "desktop-readiness.json")["status"] == "ready", "Desktop not ready")
        inactive = read_json(folder / "inactive-result.json")
        require(inactive["passed"] is True, "Inactive overlay integration failed")
        require(inactive["executableSha256"] == digest and inactive["commit"] == commit,
                "Inactive evidence is for a different commit or EXE")
        apps = ("Notepad", "Explorer", "TaskScheduler", "WPF-fixture", "WinForms-fixture", "Chrome")
        if platform == "windows-11-arm":
            apps += ("MicrosoftStore", "Calculator", "Settings")
        verify_cases(folder, commit, digest, apps, 96 if platform == "windows-11-arm" else 0)
        if platform == "windows-11-arm":
            verify_cases(folder / "paired", commit, digest, ("Settings",), 96, 2)
        if platform == "windows-11-arm":
            for dpi in (144, 192):
                scaled = folder / f"settings-dpi-{dpi}"
                require(read_json(scaled / "desktop-readiness.json")["status"] == "ready",
                        "HiDPI desktop not ready")
                verify_cases(scaled, commit, digest, ("Settings",), dpi)
                verify_cases(scaled / "paired", commit, digest, ("Settings",), dpi, 2)
    chatgpt = root / "real-app-evidence" / "real-app-evidence-chatgpt"
    for dpi in (96, 144, 192):
        folder = chatgpt if dpi == 96 else chatgpt / f"settings-dpi-{dpi}"
        require(read_json(folder / "desktop-readiness.json")["status"] == "ready",
                "ChatGPT desktop not ready")
        verify_cases(folder, commit, digest, ("OpenAI.Codex", "OpenAI.ChatGPT-Desktop"), dpi)
        verify_cases(folder / "paired", commit, digest, ("OpenAI.Codex", "OpenAI.ChatGPT-Desktop"), dpi, 2)
    multi = read_json(root / "real-app-evidence/real-app-evidence-multi-monitor/multi-monitor-result.json")
    require(multi["commit"] == commit and multi["executableSha256"] == digest, "Multi-monitor evidence mismatch")
    require(multi["passed"] is True, "Multi-monitor navigation failed")
    details = multi["details"]
    require(details["threeDisplays"] == 3 and details["twoDisplays"] == 2 and details["restoredDisplays"] == 3,
            "Missing actual 3-to-2-to-3 topology coverage")
    require(details["movementCases"] == 9 and details["crossButtonCancelled"] is True and details["hotplugGestureCancelled"] is True,
            "Incomplete actual multi-monitor input coverage")
    return version, digest


def verify_cases(folder: Path, commit: str, digest: str, apps: tuple[str, ...], dpi: int, button_count: int = 1) -> None:
    results = read_json(folder / "results.json")
    require(results["commit"] == commit and results["executableSha256"] == digest,
            "Desktop evidence is for a different commit or EXE")
    require(results.get("fatalError") is None, "Fatal desktop test error")
    require(results.get("buttonCount", 1) == button_count, "Wrong button count")
    require(not any(case["status"] == "failed" for case in results["cases"]), "A real-app case failed")
    for app in apps:
        for mode in ("normal", "maximized", "restored-narrow"):
            cases = [c for c in results["cases"] if c["app"] == app and c["state"] == mode]
            label = f"{folder.name}/{app}/{mode}"
            require(len(cases) == 1 and cases[0]["status"] == "passed", f"Missing or failed: {label}")
            case = cases[0]
            if app not in ("Chrome", "MicrosoftStore", "Calculator", "Settings", "OpenAI.Codex", "OpenAI.ChatGPT-Desktop") and not (dpi == 96 and app == "Notepad"):
                require(case["nativeSizeMatched"] is True, f"Native size mismatch: {label}")
            if app in ("Settings", "OpenAI.Codex"):
                errors = case.get("verticalCenterErrors", [])
                require(len(errors) == 3 and all(abs(e) <= 0.5 for e in errors),
                        f"Caption center mismatch: {label}")
                require(case["caption"]["Dpi"] == dpi, f"Actual window DPI mismatch: {label}")
            require(case["hitPoints"] == 5 * button_count and case["stableSamples"] == 8, f"Incomplete probes: {label}")
            if button_count == 2:
                cells = case.get("buttonPixels", [])
                require(len(cells) == 2 and {c["part"] for c in cells} == {0, 1}, f"Missing button pixels: {label}")
                require(all(c["glyph"]["Changed"] >= 6 and c["hover"]["Changed"] >= 6 for c in cells), f"One paired button is invisible: {label}")
                require(all(c.get("hoverSamples", 0) >= 3 and c.get("hoverPresence", {}).get("Changed", 0) >= 6 for c in cells), f"Paired hover was not visibly stable: {label}")
            require(case["pixelGlyph"]["Changed"] >= 6 and case["pixelHover"]["Changed"] >= 6,
                    f"Rendering not observed: {label}")


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
        "MoveToMonitorButton.exe", "README.md", "LICENSE", "SHA256SUMS.txt")]
    portable = output / f"MoveToMonitorButton-v{version}-windows-x64.zip"
    write_zip(portable, application, root)
    (output / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256(file)}  {file.name}\n" for file in sorted(output.iterdir())), encoding="ascii")
    print(f"Verified {version}: Server + Windows 11 real apps, Settings and ChatGPT at 100/150/200 percent; EXE {digest}")
    return version, digest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    package(args.root, args.output, args.commit)
