# Windows 11 Arm screen-capture diagnosis (Issue #2)

The read-only preflight and its original exact-pixel gate are preserved. The repair adds a separate, explicitly guarded provisioning step for disposable GitHub-hosted Windows 11 Arm machines. It also repairs the real-app harness and an independently observed inactive-hover defect. It does not change configuration on product users' machines.

## Repair and validation (2026-09-30)

The blocking UI is Windows CloudExperienceHost, not an inaccessible session or a broken GetPixel conversion. The earlier evidence below is retained as the diagnosis history; its “blocked/not started” status describes those earlier runs.

1. `Initialize-HostedDesktop.ps1` checks GitHub-hosted provenance, the Windows 11 Arm image label and client OS. It records the existing policy values, applies explicit privacy policies, and reads them back. Optional collection/location/advertising/personalization are disabled; diagnostic data is required-only. `DisablePrivacyExperience` suppresses the first-logon prompt for this disposable test account.
2. `HostedDesktopSetup.cs` identifies the visible, uncloaked `Microsoft account` CoreWindow by HWND, PID, session, process start time, the system `WWAHost.exe` path and `Microsoft.Windows.CloudExperienceHost_` package identity. It revalidates that identity before sending `WM_CLOSE`. It refuses an unknown/changed host and fails if the verified host does not close; no process termination fallback remains. The policy/UI transition is logged separately from the preflight.
3. Closing that UI alone exposed a second precondition: SearchHost retained foreground. A small, owned setup window receives one checked input click and grants normal `AllowSetForegroundWindow` permission. Input is sent only after checking the input desktop and verifying that the exact point belongs to the setup window; the window is disposed and the cursor restored. This is setup, never pixel evidence.
4. The same job then runs the unchanged two-color gate, including exact GetPixel/BitBlt RGB, foreground before/after, paint phase and active input context. The isolated diagnostic VM's success cannot substitute for the product VM's preflight.
5. Real-app discovery includes top-level CoreWindows using bounded `FindWindowEx` enumeration, because `EnumWindows` documents desktop-app-only enumeration on Windows 8+. Package identity, uncloaked/visible state, non-child style and a stable newly visible HWND are still required. A package-verified frame that existed only as a cloaked placeholder may become the target on the disposable hosted runner; an already-visible frame is refused. Each Windows 11 case obtains foreground permission before its baseline. The existing 12-second overlay startup deadline now requires a stable HWND/bounds before observation; the five hit points, eight steady-state samples, geometry, screen-pixel glyph/hover and shutdown checks remain mandatory. Failed foreground/hit-point identities and failed launch inventories are saved.
6. A separate x64 integration failure blocked building the EXE: after press cancellation, the cursor was inside the inactive button, capture was released and bounds/foreground stayed correct, but the button retained the normal RGB instead of the original hover RGB for the entire three seconds. Refresh now reconciles the hover state with the current cursor and actual hit window after placement probing. The test still requires the same three consecutive exact RGB matches within the same 30 × 100 ms budget. The exact prior mouse-message ordering was not traced and is not claimed.

No DXGI replacement, offscreen rendering, tolerance increase, extra preflight wait, `continue-on-error`, or late-sample rescue was introduced. A future image with different setup UI fails the original gate instead of being declared ready.

### Intermediate evidence

- [36663572254](https://github.com/imksoo/add-move-display-button/actions/runs/36663572254): policy + verified `WM_CLOSE` removed the privacy screen; both original colors matched, but foreground remained SearchHost, so preflight correctly **failed**.
- [36664002389](https://github.com/imksoo/add-move-display-button/actions/runs/36664002389): adding owned-window input/foreground delegation made the Arm isolated and product-job preflights **pass**, with the original colors and all added context checks. Notepad, Explorer and Store passed all three states. Calculator discovery, other foreground cases and one initial Chrome hit test still failed; this was **not** a complete product pass. The x64 Release/Debug and both Server product suites passed; the formatting job failed and was corrected in the next commit.
- Earlier UI Automation experiments found no exposed child controls under the observed setup root. They were removed; the repair does not guess screen coordinates for privacy buttons or claim that all UI Automation is unavailable.

- [36664706996](https://github.com/imksoo/add-move-display-button/actions/runs/36664706996): Arm preflight passed again and foreground setup enabled MMC/WinForms observations. Full product success was still not established: Calculator was visibly running in ApplicationFrameHost but was not identified, and several cases lost foreground/stability. That evidence led to child-CoreWindow discovery and additional failure snapshots; no failed case was promoted to passed.

- [36665375215](https://github.com/imksoo/add-move-display-button/actions/runs/36665375215): the new failure snapshots identify **WindowsTerminal displaying `wsl.exe`'s interactive update prompt** as foreground during all three runtime foreground/stability failures. Active-window tracking was off. The product correctly stopped showing its active overlay when another window took foreground; relaxing that check would have hidden an environment failure. [Result excerpts](evidence/issue-2-36665375215/results-excerpt.json), [failure screen](evidence/issue-2-36665375215/MicrosoftStore-normal-failure.png), [frame excerpts](evidence/issue-2-36665375215/calculator-frames-excerpt.json), and [artifact manifest](evidence/issue-2-36665375215/manifest.json) are retained. The Calculator frame contains the correct package-owned child; child discovery alone was insufficient. The next change also handles activation of pre-created cloaked frames on the disposable hosted runner, while refusing already-visible windows. Both Server product suites passed. SDK Release separately failed an existing caption visibility check; that run is not reported as an all-green regression run.

For product jobs, provisioning installs the **runtime only** from Microsoft's official Arm64 WSL 3.0.1 MSI. Its release asset SHA-256 is pinned (`857ddbb335ec7d05ffa71d0fd2203750c0e8fc29bb164f8a95db92bd7bba4263`) and the Microsoft Authenticode signature must validate before execution. The MSI runs quietly with `/norestart`; its exit code and the noninteractive `wsl --version` result are retained. Installer result 3010 is recorded as a runtime install requiring reboot, not as proof that Linux VMs work; version-query success and every desktop test are still independently required. No Linux distribution is installed, no optional virtualization feature is enabled by the script, and no reboot is requested. Existing 60-second bootstrap prompts must expire before foreground setup/preflight. Terminal windows are never dismissed during product observations. This preparation is conditional on `-PrepareApplicationTests`; isolated preflight still tests the minimal privacy/input repair. See [Microsoft's MSI installation instructions](https://learn.microsoft.com/en-us/windows/wsl/install#offline-install) and [official release](https://github.com/microsoft/WSL/releases/tag/3.0.1).

- [36666140679](https://github.com/imksoo/add-move-display-button/actions/runs/36666140679): the initial WSL `--update` attempt failed closed because the inbox launcher reported that WSL itself was absent. The preparation command was corrected to install the runtime without a distribution, and direct process handles/raw stdout-stderr capture replace PowerShell's missing exit-code result.

- [36666467005](https://github.com/imksoo/add-move-display-button/actions/runs/36666467005): the inbox launcher also rejected `--install --no-distribution --web-download` with “not installed.” Its recorded bootstrap process was `wsl.exe --update --confirm --prompt-before-exit`. Preparation was changed to the official verified MSI, and extraneous void-task output was suppressed so command exit status is a scalar. This failed preparation never reached preflight or product observations.

- [36666847586](https://github.com/imksoo/add-move-display-button/actions/runs/36666847586): PowerShell's installer download stalled; this run was cancelled before the Arm product tests. Native curl with a bounded deadline replaced that download. Server 2022 independently exposed a topmost-band error in the inactive button: inserting after a topmost HWND promoted an ordinary button back into the topmost band. The repair uses [HWND_TOP at that boundary](https://devblogs.microsoft.com/oldnewthing/20170914-00/?p=97025) and adds an unrelated topmost neighbor to the existing demotion regression, preserving its assertion and timeout.

- [36667375257](https://github.com/imksoo/add-move-display-button/actions/runs/36667375257): WSL MSI download/install/version queries all returned 0, the signature was valid, and the same Arm product job passed strict preflight plus **all 12 required modern-app cases**. The Calculator launch inventory confirms a pre-created, cloaked ApplicationFrameWindow. All 24 normal/hover PNGs for the 12 required cases were reviewed: monitor glyph visible, hover shading present, and native caption controls unobscured. Overall Arm result was still failed: Chrome normal lost foreground to OneDrive's first-sign-in host (OneDriveReactNativeWin32WindowClass). This is retained as a failed run, not a full-suite pass. [Selected results and OneDrive foreground evidence](evidence/issue-2-36667375257/results-excerpt.json) and the raw [Calculator launch-before inventory](evidence/issue-2-36667375257/Calculator-launch-before.json) are retained.

Product-job provisioning now also applies DisableFileSyncNGSC on the disposable test account. Any existing same-session OneDrive process must use a standard installation path and a valid Microsoft Authenticode signature before its own /shutdown command is invoked. The script requires a zero exit code and no remaining same-session OneDrive process. It never terminates arbitrary processes or dismisses windows during product observations. OneDrive synchronization is outside this fixture's coverage.

- [36668291468](https://github.com/imksoo/add-move-display-button/actions/runs/36668291468): SDK Debug independently failed the existing caption visibility check; SDK Release and the actual EXE build passed, including the strengthened inactive topmost-neighbor regression. The caption fixture used Sleep(10) while its UI thread was the target of the EXE's 20 ms synchronous probes. Its pump now wakes on sent messages with [MsgWaitForMultipleObjectsEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex), preserving all deadlines and assertions. This fixes a concrete fixture responsiveness risk; the failed run did not record enough state to prove that scheduling was its sole cause. A hidden-overlay failure now records fixture/foreground HWND, PID and class. The Arm job and both Server product jobs passed. This run still failed overall because of SDK Debug; it is not described as an all-green workflow.

### Verified Arm result

Run [36668291468](https://github.com/imksoo/add-move-display-button/actions/runs/36668291468), attempt 1, Arm job `109737854758`, completed successfully. PR head `45da8a6945e95421a0e8b17118e177445eaa13a4`; tested merge commit `7d3311bf76a8a6d356588c8dad7ab3ffc6aa1024`. The same job completed provisioning, the original strict preflight, **24/24 real-app/framework cases**, and the actual-EXE inactive-window/first-click suite. All **12/12 required modern-app cases** passed without unavailable/skipped cases. Each passed all five input points, eight steady-state samples, geometry, screen-pixel glyph/hover, responsiveness and clean shutdown.

The downloaded x64 EXE SHA-256 was independently recomputed and matched the build metadata, real-app results and inactive results: `fe29dbeeebe540e054d54044d6cbabddd241ecc7b09c8ad67dcdaf18268e92f4`. This is a branch-built EXE with version metadata 0.1.8, not the previously published release asset.

| App / observed version | Normal | Maximized | Restored narrow |
|---|---|---|---|
| Notepad 11.2605.34.0 | PASS | PASS | PASS |
| Explorer 10.0.26100.8117 | PASS | PASS | PASS |
| Microsoft Store 22506.1400.2.0 | PASS | PASS | PASS |
| Calculator 11.2502.2.0 | PASS | PASS | PASS |

Environment: Windows 11 Enterprise build 26200, native Arm64 host / x64 product emulation, image `win11-vs2026-arm64` / `20260920.164.1`. Preflight observed Session 2 / WTSActive / visible WinSta0 / Default input desktop; each phase had the sentinel as foreground and point owner, with exact GetPixel and BitBlt matches. No late observations were needed. WSL download/install/version queries all exited 0. OneDrive was absent after policy application in this run, so its guarded `/shutdown` branch was not exercised.

Durable evidence: [complete results](evidence/issue-2-36668291468/results.json), [preflight context and pixels](evidence/issue-2-36668291468/desktop-diagnostic.json), [provisioning report](evidence/issue-2-36668291468/desktop-setup/desktop-setup.json), [inactive result](evidence/issue-2-36668291468/inactive-result.json), [image review](evidence/issue-2-36668291468/image-review.json), and [source IDs and hashes](evidence/issue-2-36668291468/manifest.json). The directory preserves the baseline/normal/hover PNGs for all 12 required cases. All 24 normal/hover images were visually reviewed: visible monitor glyph, hover shading and unobscured native caption controls. Results JSON retains every field and case with normalized whitespace; other listed artifact files preserve their original bytes, including UTF-16 where produced by Windows PowerShell.

Both Server product jobs passed in this run. SDK Debug's separate caption check remained failed; the fixture responsiveness change above is validated by the subsequent PR checks. Issue #2 remains open pending integration/review; no release asset was overwritten.

### Scope

The product is still the actual x64 EXE running under Windows 11 Arm emulation. These are single-monitor display, placement, input and screen-pixel tests. They do not validate physical monitor movement, native Arm64 product compilation or mixed-DPI hardware. Server success does not establish modern packaged-app compatibility.

### Provisioning API references

- [Privacy policy CSP](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-privacy): administrative first-logon privacy policy, location and advertising policy mappings.
- [Experience policy CSP](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-experience): Find My Device and diagnostic-data personalization.
- [TextInput policy CSP](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-textinput): linguistic data collection.
- [System policy CSP](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-system): required-only diagnostic data and DisableOneDriveFileSync policy mapping.
- [AllowSetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow): foreground permission lasts only until subsequent user input; it is not an unconditional activation override.
- [EnumWindows](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumwindows), [FindWindowEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-findwindowexw): top-level discovery and the desktop-app enumeration restriction.

## Historical diagnosis before provisioning

### 2026-09-30 再検証：修復前の記録

起点の[Issue #2](https://github.com/imksoo/add-move-display-button/issues/2)はopen、コメント0。[PR #3](https://github.com/imksoo/add-move-display-button/pull/3)と[PR #4](https://github.com/imksoo/add-move-display-button/pull/4)は9月27日merge済み、[v0.1.8](https://github.com/imksoo/add-move-display-button/releases/tag/v0.1.8)も公開済み。この公開はWindows 11 Armの合格を意味しない。

元のrun [36304010382](https://github.com/imksoo/add-move-display-button/actions/runs/36304010382)だけでは、2色とも`#F0F4EE`、foreground不一致、同一cropという結果までしか分からない。所有HWNDやinput desktopの記録がないため、そのrunの原因を後から断定しない。以下は同じArm image `20260920.164.1`で追加計測して再現した障害の診断である。

Windows 11 Armでは、初回privacy設定UIがsentinelを覆い、前面と採取点を占有している。今回の実行ではsession/window station/input desktopの不一致ではない。GetPixelとBitBltは両方、そのUIの画素を取得している。製品EXEはこの診断で起動していない。

**確定した範囲:** 画素検査が失敗する直接原因は、sentinelの採取点がprivacy UIに覆われていること。**未確定:** 初回UIがhosted imageの初期化後に残る上流原因、内部z-order band、初回設定完了後の同一Arm環境での合格。環境修復や製品試験完了を宣言するものではない。

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

最初のrun [36657232424](https://github.com/imksoo/add-move-display-button/actions/runs/36657232424)では変更していないinactive_overlay試験が `stableHoverSamples == 3` で失敗し、build依存のArmジョブがskipされた。このため製品EXEを必要としない診断だけを独立させた。run 36657431081でもSDK Releaseジョブ `109704681933` の同じチェックが失敗している。必須製品試験や公開ゲートは維持し、リトライで失敗履歴を隠していない。この時点のPRは診断用draftで、製品試験全体は未合格だった。後続の修復・検証は冒頭の履歴を参照。

### 別runによる再現確認

[36657796325](https://github.com/imksoo/add-move-display-button/actions/runs/36657796325)、attempt 1。PR head `0df8bb08570736243e8d21c13c7d4d85fde58b47`、実際のPR合成コミット `af65126b2df6e1f725ef5653f0cdcd8c7126e66d`。Arm job `109705766735`はenvironment-blocked、Server 2022対照job `109705766990`は成功した。診断コードは先のrunと同じ。

- Armは同じimage/build。全12観測の前後でSession 2 / WTSActive / WinSta0 / Default / UOI_IO=trueが一致。
- SentinelはPID 8476、HWND 721068。WM_PAINTは1から12まで進み、全phaseが一致。
- 全観測でforeground前後と採取点のrootはWWAHost PID 8872、HWND 66056、`Microsoft account`。全画面画像も初回privacy設定を示す。同じHWND数値が別runに現れることは、プロセスやVMが同じという意味ではない。
- 期待色`#1161AD` / `#AD6111`に対し、両GDI経路は両phaseとも`#F1F4F5`。背景のRGB値自体は先のrunと異なるが、期待色が出ないこととUI所有者は同じ。12枚のcropはrun内で同一ハッシュ。
- 2つのrunの全画面PNGには、期待する2色の完全一致画素がいずれも0個。採取座標だけがずれていたという説明を支持しない。

[再検証のJSON](evidence/issue-2-36657796325/arm-desktop-diagnostic.json)と[全画面PNG](evidence/issue-2-36657796325/arm-desktop-failure-full.png)、[取得元・ハッシュ](evidence/issue-2-36657796325/manifest.json)を保存した。原本JSONとPNGの対応座標のRGBを照合した。

このrunのSDK Debug/Releaseも`stableHoverSamples == 3`で失敗した。PR #4が修正した「待機直後の別GetPixel再読込」は既に除かれている。現在の失敗には待機中の実RGB・cursor/captureの記録がなく、同じ原因や単なるflaky testとは断定できない。これは別VMのx64製品統合試験であり、製品EXEを起動しないArm preflightの原因ではない。本診断でhover期待値や待機条件は変更しない。

## 候補の判別と最小変更

| 候補 | 必要な観測 | 今回の判定 |
|---|---|---|
| Session / window station / input desktopの不一致 | 各capture前後のSessionId、WTS state、station名/visible、thread/input desktop名とUOI_IO、API失敗 | 再現した両runで一致。不一致を支持しない。OpenInputDesktop成功だけで正常としない |
| 初回privacy UIの遮蔽 | foreground、採取点のroot HWND、PID/session/class/title/rect、全画面PNG | WWAHostの同一ウィンドウが前面と採取点を占有し、PNGもprivacy UI。直接原因を確認 |
| 座標/DPIまたはsentinel描画未処理 | DPI context、window rect、画面座標とbitmap座標、WM_PAINT/phase、全画面の期待色 | DPI96、phase処理済み。期待色は全画面にもない。可視化完了はpaint回数だけでは証明しない |
| GetPixel固有の不整合 | 同一screen DC・対応座標のraw COLORREFとBitBlt PNGのRGB | 両者が一致してprivacy UIを取得。GetPixelだけの問題を支持しない |
| Hosted desktopのcomposition / driver | 遮蔽物なし・context一致でもGDIだけが更新しない場合、独立したmonitor-level DXGI取得を追加 | 両APIともGDIなので全composition問題の不存在は証明できない。今回は遮蔽で説明でき、DXGIは次段階に留める |

初期の診断追加は次の3ファイルに限定した。上記の修復変更は、その診断結果に基づく別段階である。

1. `tests/desktop/DesktopReadiness.cs`: 既存sentinel処理を抽出し、上記context/window/paint/同一座標RGBを追記。失敗時だけ全画面・window inventory・期限付き追加観測を保存する。
2. `tools/Test-DesktopReadiness.ps1`: プローブをロードし、元の2色完全一致・失敗exitを保持。元判定と診断追加条件を別項目で記録する。
3. `.github/workflows/real-apps.yml`: 製品buildに依存しないpreflightだけのArm/Server対照ジョブと、失敗時artifact保存・timeout。製品試験の依存関係と公開条件は維持する。

原因の見えないまま取得APIを置換したり、許容誤差・待機期限を緩和したりする変更は必要ない。PrintWindowやDrawToBitmapでsentinelの内容が描けても、実画面の合格にはしない。DwmFlushは呼出元のDirectX更新を待つAPIで、session全体をflushする検査にはならない。

## Invariants

- Preserve the original two colors, original 20 x 50ms message-pump wait for each color, exact RGB comparison, and failing exit code.
- Save the original verdict before any late observations. Late success never promotes the run to ready.
- Additionally require the product's screen BitBlt path to match, the sentinel to be foreground before/after capture, and an active consistent input desktop.
- The **preflight** remains read only: no desktop switching, registry changes, privacy UI dismissal, sign-in changes, token elevation, or termination of unrelated processes. The separately named, guarded provisioning step above does apply policies and close the verified setup window before the gate.
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

## Completion criteria and verified status

| 段階 | 合格条件 | 現状 |
|---|---|---|
| 診断 | 同一run・phaseでcontext、前面/採取点の所有者、paint、対応画素、画像が揃い、不一致の境界を説明できる | 2回のArm再現で直接的な遮蔽を確認 |
| Preflight | 元の各20×50ms待機後、`#1161AD`と`#AD6111`が両方GetPixelとBitBltで完全一致。activeな同一input context、sentinelの前面・可視・非cloaked・該当paint phaseを確認し、追加観測に救済されず元判定がready | Armの同一製品jobで合格（36668291468）。Server対照で代替していない |
| Windows 11製品試験 | 合格したpreflightと**同じjob/desktop**で現行Notepad・Explorer・Store・Calculatorの通常/最大化/復元後12条件を実行。commit、image、パッケージversion、実EXE hashを記録し、既存geometry/pixel/hover/stability/shutdownと画像レビューを通す。unavailable/skipは合格に含めない | 同じArm jobで12/12合格、24枚のnormal/hover PNGを確認済み |
| Issue #2完了 | 上記Armのpreflightと12条件の証跡が揃う。物理複数モニター移動・混在DPIは別coverageとして明記 | 技術的条件を満たす証跡を保存済み。PRの統合・レビュー待ちでIssueはopen |

今回の修復では、準備後の同じプローブで前面/採取点がsentinelへ変わり両色が一致した。今後のimageでも、UIが消えたことだけでは合格にしない。UIが消えてもGDIが更新しない場合に限りDXGIのadapter/output、HRESULT、frame時刻を追加し、compositionの調査へ進む。

## API仕様の根拠

- [OpenInputDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openinputdesktop): disconnected sessionでもhandleが返り得るため、WTS stateと併記する。
- [GetUserObjectInformation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getuserobjectinformationa): UOI_IOは入力を受けるdesktopかどうかを表す。
- [BitBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt): SRCCOPYにCAPTUREBLTを含めた実screen DC取得を使用する。
- [DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush): session全体のrendering batchはflushしない。
