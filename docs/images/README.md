# Screenshot sources

These PNG files are unmodified captures of actual Windows application title bars, made by the desktop test harness. The files are copied byte-for-byte from the successful Actions artifacts; no generated UI or compositing is used.

- Run: https://github.com/imksoo/add-move-display-button/actions/runs/43558572140
- Source commit: `72d7f863099a20f3279fc02036be8f626b9f75a4` (v0.1.11 product code).
- Host: disposable Windows 11 Arm desktop; x64 product executable.
- Settings image: actual 200% display scale, maximized, hovered move button.
- ChatGPT image: official Store app `OpenAI.Codex`, actual 150% display scale, normal window, hovered move button, before sign-in.
- The harness captures the title-bar area, not the whole desktop. These images illustrate placement; they do not demonstrate a physical move between monitors.

| Repository file | Original artifact path | PNG SHA-256 |
|---|---|---|
| `settings-200-percent.png` | `settings-dpi-192/Settings-maximized-hover.png` | `846dcc62e97eb745aaecfb538bd537cbbedd8406693369666accb4419accef63` |
| `chatgpt-150-percent.png` | `settings-dpi-144/OpenAI.Codex-normal-hover.png` | `1828d2de641b372c18e19feddef9e6ffa40ec4bfb6443a665f60a9a91d352fcf` |
