# Windows 11 Arm screen-capture diagnosis (Issue #2)

This change instruments the desktop preflight. It changes no product code or Windows settings.

## 2026-09-30 実行結果：直接原因を特定

Windows 11 Armでは、初回privacy設定UIがsentinelを覆い、前面と採取点を占有している。今回の実行ではsession/window station/input desktopの不一致ではない。GetPixelとBitBltは両方、そのUIの画素を取得している。製品EXEはこの診断で起動していない。

- 実行: [36657431081](https://github.com/imksoo/add-move-display-button/actions/runs/36657431081)
- PR head: `90af64a9ab241c3e432b4e0f64bd3d120f5adfe9`
- 実際のPR合成コミット: `3c95b4323b1de1e23c392c74ded4a11af6d83fc5`
- Arm job: `109704681677`。Server 2022対照job: `109704681595`。
- Arm: Windows 11 Enterprise build 26200、image `win11-vs2026-arm64` / `20260920.164.1`。
- Armプローブは `IsWow64Process2: processMachine=0, nativeMachine=0xAA64`。ネイティブArm64のPowerShellで、製品x64エミュレーションは失敗の前提ではない。

| 観測 | Armの実測 |
|---|---|
| Session | 2、WTSActive (0) |
| Window station | WinSta0、visible flag=1 |
| Thread / input desktop | 両方Default、両方UOI_IO=true。全12観測の前後で一致 |
| Sentinel | HWND 393698、PID 1092、DPI96、visible、非最小化、cloaked=0 |
| 描画要求の処理 | 元の2色でWM_PAINT数1→2、追加観測で12まで増加。全phase一致 |
| Foreground / WindowFromPoint root | ともにHWND 66056、PID 5968、WWAHost、Windows.UI.Core.CoreWindow、タイトルMicrosoft account |
| 前面UIの矩形 | (0,0)-(1024,768)、採取点(128,131)を包含 |
| 元の期待色 | #1161AD / #AD6111 |
| GetPixel / BitBlt | 両方とも#FBFAFB。追加10観測も同じ |
| 全画面PNG | Choose privacy settings for your device。採取点の実RGB=(251,250,251) |
| 関連shell窓 | 同一sessionにExplorerのShell_OOBEProxy、タイトルMicrosoft account |

原本: [Arm JSON](evidence/issue-2-36657431081/arm-desktop-diagnostic.json)、[全画面PNG](evidence/issue-2-36657431081/arm-desktop-failure-full.png)、[Server対照JSON](evidence/issue-2-36657431081/server-desktop-diagnostic.json)。原本JSONは改変していない。元のartifact ZIPのSHA-256はArm `b38f99d93f9efba27f70be1e0052280601d4f755989f68bd0c7d2e0d1d82941c`、Server `cc4f8f1196201dbc8a44d74c882e56578fd116511ab3d868289927de3db26fcc`（ダウンロード後に照合済み）。

同じコードのServer 2022対照は、2色ともGetPixel/BitBlt/foreground/desktopの追加条件を含めてready。Armはenvironment-blockedのまま。Armの12枚のcropは全てSHA-256が同一だった。

これにより今回の直接的な遮蔽原因は説明できる。DPI座標誤り、message pump未処理、GetPixelだけの不整合、切断session、別入力desktopを原因にする証拠はない。一般的なDWM/driver不具合の不存在を証明するものではないが、本件を説明するためのDXGI追加実装は不要。初回UIの内部z-order bandや、hosted image初期化でこのUIが残る上流理由までは断定しない。

次の修復対象はテスト判定ではなく、初回設定が完了した使い捨てWindows 11テスト環境の用意。現在の診断は同意ボタン押下、レジストリ改変、WWAHost/Explorer終了などで画面を退けない。環境を用意した後も、この厳密なpreflightを通してから12条件の製品試験を行う。Issue #2はその完了までopenのままとする。

### 診断以外のCI失敗

最初のrun [36657232424](https://github.com/imksoo/add-move-display-button/actions/runs/36657232424)では変更していないinactive_overlay試験が `stableHoverSamples == 3` で失敗し、build依存のArmジョブがskipされた。このため製品EXEを必要としない診断だけを独立させた。run 36657431081でもSDK Releaseジョブ `109704681933` の同じチェックが失敗している。必須製品試験や公開ゲートは維持し、リトライで失敗履歴を隠していない。このPRは診断用draftであり、これらの失敗を直した／製品試験全体が合格したとは扱わない。

## Invariants

- Preserve the original two colors, original 20 x 50ms message-pump wait for each color, exact RGB comparison, and failing exit code.
- Save the original verdict before any late observations. Late success never promotes the run to ready.
- Additionally require the product's screen BitBlt path to match, the sentinel to be foreground before/after capture, and an active consistent input desktop.
- Read only: no desktop switching, registry changes, privacy consent dismissal, sign-in changes, token elevation, or termination of unrelated processes.
- Product tests execute only after successful preflight. A blocked environment is neither a product pass nor a product failure.

## Execution

The existing manual workflow accepts `include_windows11=true`. Pull requests from explicitly named `diagnose/desktop-*` branches also opt into an isolated preflight-only matrix (Server 2022 control and Windows 11 Arm). This matrix needs no product EXE and runs independently of the existing build/product tests. It cannot establish product coverage. Both Server jobs remain required and unchanged in scope. No release is published for a PR.

The preflight step has a three-minute process limit; `always()` uploads its incremental evidence even when it fails. Only disposable hosted desktops are captured by default; the existing local capture opt-in remains required.

## Evidence

`desktop-readiness.json` retains the original observations and status. `desktop-diagnostic.json` is checkpointed before/after captures and includes:

- Process/native architecture, thread apartment and DPI context, DWM enablement (not proof of a current composed frame).
- For each original/late sample: timestamps, session/WTS state, station name/visibility flags, thread/input desktop names and UOI_IO, plus query errors.
- Sentinel, foreground before/after, and hit-point root HWND identity, process/session, class/title, rectangle, visibility/minimization/cloaking and DPI.
- WM_PAINT/WM_ERASEBKGND counts and paint phase (request processing, not proof of visible output).
- GetPixel's raw COLORREF, explicit CLR_INVALID handling, and RGB from the actual BitBlt bitmap at the same recorded screen coordinate.
- On failure: both accessible desktop window inventories, relevant shell/setup process session IDs, one full virtual-desktop capture, and at most ten late captures bounded by a five-second loop deadline. A blocking native call is additionally bounded by the workflow process limit.

Input desktop handles opened with DESKTOP_READOBJECTS are closed. Borrowed GetThreadDesktop/GetProcessWindowStation handles are not closed. Names/UOI_IO are compared within the same process station/session, never raw handle equality. OpenInputDesktop success alone is insufficient because it can also succeed in a disconnected session.

## Interpretation

1. Inactive/disconnected session or thread/input desktop mismatch: environment context failure.
2. Same input desktop and privacy/setup UI covering the capture point: identify the owning window/process and corroborate with the full image. Process presence alone is insufficient.
3. Sentinel pixels elsewhere in the full image: coordinate/DPI investigation.
4. Paint phase does not advance: sentinel/message processing investigation.
5. BitBlt sees the changing sentinel but GetPixel does not: capture-method discrepancy.
6. Context, coordinates and paint processing all agree but GDI output stays stale: add an independent monitor-level DXGI Desktop Duplication probe, matching adapter/output and recording HRESULT/frame timestamps. Do not label this a DWM defect solely from GDI or DwmIsCompositionEnabled.

PrintWindow/DrawToBitmap are not screen proof. DwmFlush is not a session-wide flush. Neither can replace the screen-pixel gate. Access denied or unsupported DXGI is diagnostic data, never a pass.

## Completion criteria

Diagnose the failing boundary using correlated context/window/pixel evidence. Then establish both original exact colors through GetPixel and the product's BitBlt path with consistent active input context and foreground. Finally validate current Windows 11 Notepad, Explorer, Store and Calculator in normal, maximized and restored states (12 real-app cases), reviewing screenshots and retaining the existing geometry, pixel, hover, stability and shutdown checks. Physical monitor movement and mixed DPI remain separate coverage. Issue #2 remains open until these conditions hold.
