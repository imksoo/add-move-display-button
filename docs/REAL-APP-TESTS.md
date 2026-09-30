# 実アプリのタイトルバー描画試験

## 目的と対象

同じコミットからMSVCと公式SDKでビルドしたx64 EXEを実際に起動し、Notepad、Explorer、Chrome、タスクスケジューラ、Microsoft Store、電卓のタイトルバー上で検査する。WPFとWinFormsには別途、明示的にfixtureと名付けた最小の検証用アプリを使う。fixtureの成功を市販アプリやWinUI/UWPの成功として扱わない。

環境の診断・修復と検証履歴は[Windows 11 Arm診断](WINDOWS11-DESKTOP-DIAGNOSIS.md)に記録する。

## 環境の成立を先に確認

`Test-DesktopReadiness.ps1`は自分の検証用ウィンドウを2色に描き、画面上の実画素が両方とも一致することを確認する。Foreground HWNDやUserInteractiveが正しくても、画面取得結果が実アプリと一致しないランナーがあるため、これらのAPI値だけでは合格にしない。

この前提が不成立なら`desktop-readiness.json`に`environment-blocked`を記録し、ジョブを失敗させ、アプリ試験を行わない。「環境不成立」を製品の不合格や合格へ読み替えない。preflight自体は設定を書き換えない。Windows 11 Armの使い捨てhosted runnerでは、その前に別の`Initialize-HostedDesktop.ps1`ステップでprivacy policyと入力の前提を準備する。製品試験用には署名とSHA-256を検証したWSL runtimeのみを導入し、初回更新Terminalを解消する。OneDrive同期をpolicyで抑止し、既存clientがあれば署名・パスを検証して終了を要求する。対象を検証したCloudExperienceHostだけにWM_CLOSEを送り、その成功とは別に同じjobで元の厳密な2色検査を要求する。製品やローカル試験がこの環境初期化を自動実行することはない。

## アプリの識別と観測

新しく開いたトップレベルウィンドウをプロセス名、クラス、パッケージ識別子で確認する。Storeアプリの起動途中に現れる子CoreWindowや、PowerShellの起動用コンソールを対象にしない。標準APIから矩形を読む独立したC#プローブを使い、製品の配置アルゴリズム自体は試験コードへ取り込まない。

各アプリで通常幅、最大化、復元後の狭い幅の3条件を検査する。各条件を確定してから新しい製品EXEを起動するため、これを「製品を起動したまま連続リサイズする試験」とは呼ばない。

- 対象と作業領域内に追加ボタンがあり、DWMの既存ボタン群と重ならない。
- 個別ボタン情報を取得できる場合は幅と上下位置の一致を記録する。標準フレームのNotepad、MMC、WPF、WinFormsでは一致を必須にする。
- 追加ボタンの中央と四隅が実際に入力先になり、HTCLIENTを返す。
- 250ms間隔の8回の観測で表示と矩形が安定する。
- EXE起動前、通常表示、ホバーの画面を取得し、追加ボタン内の画素変化を確認する。
- UI応答と製品の正常終了を確認する。

画素差だけでは記号の形や全ての操作部との重なりの正しさは証明できないため、PNGを別途目視確認する。標準DWMボタンのピクセル完全一致を主張しない。

## CIと再実行

`.github/workflows/real-apps.yml`はMSVCと公式SDKで作った同じx64 EXEを、Windows Server 2022／2025 x64で実行する。`workflow_dispatch`の`include_windows11=true`、または`diagnose/desktop-*`からのPRではWindows 11 Arm64も実行する。後者はx64エミュレーションであり、Windows 11 x64ネイティブの試験ではない。ランナーのOS、アーキテクチャ、画像版、パッケージ版、EXEのSHA-256を成果物へ保存する。

テスト対象の作業領域は1モニターで、`--show-on-single-monitor`を使用する。物理的な画面間移動、異なる実モニターの混在DPI、長時間負荷、全テーマの網羅試験ではない。CIは管理者環境なので、通常ユーザーと昇格アプリの権限差も別途検証が必要。

成果物は`real-app-evidence-windows-2022`、`real-app-evidence-windows-2025`と`real-app-evidence-windows-11-arm`。結果JSONとスクリーンショットを14日保持する。アプリが未インストールの場合は`unavailable`、起動後の失敗は`failed`。欠測を合格へ置き換えない。Windows 11の`-RequireWindows11Coverage`は現行Notepad・Explorer・Store・電卓の各3状態、計12条件すべてを必須とする。

ローカルの使い捨てWindows検証環境では、Windows PowerShell 5.1で次を実行する。自分の普段のデスクトップや機密画面があるセッションでは実行しない。起動中の本ツールがあれば、試験は開始を拒否する。

```powershell
./tools/Test-DesktopReadiness.ps1 -OutputDirectory evidence -AllowDesktopCapture
./tools/Test-RealApps.ps1 -ExecutablePath ./MoveToMonitorButton.exe -OutputDirectory evidence -AllowDesktopCapture
```

前段が失敗した場合は後段を実行しない。試験の目的で起動したウィンドウだけをWM_CLOSEで閉じ、Explorerプロセスや無関係なアプリを停止しない。

## 2026-09-30 Windows 11 Armでの修復後の結果

[run 36668291468](https://github.com/imksoo/add-move-display-button/actions/runs/36668291468)の同じArm jobで、厳密なpreflight、Notepad・Explorer・Store・電卓の必須12条件を含む24/24ケース、非アクティブ操作試験が合格した。必須12条件のnormal/hover計24枚を目視確認し、記号とホバー表示、既存caption操作部を覆わないことを確認した。[コミット・EXE hash・画像・JSON](WINDOWS11-DESKTOP-DIAGNOSIS.md#verified-arm-result)を保存している。公開済みv0.1.8の資産を遡って合格とするものではない。

両Serverの製品試験も合格したが、このrun全体では別のSDK Debug caption試験が失敗した。後続のPRチェックでfixture修正を検証する。以下は修復前の歴史的な記録である。

## 2026-09-27時点で観測した結果

製品基準コミット: `5131f1d344d47a2f320ddd1e38f9c233ea17490c`（v0.1.6）。試験実装`6a3afb5ef170fb1a6f6e4346bf04c370774c6c03`のActions実行`36303004391`で、Server側は次の15条件に合格した。PNGも別途確認した。

| 対象 | 通常 | 最大化 | 復元後の狭い幅 |
|---|---|---|---|
| 旧Win32 Notepad（Server同梱版） | PASS | PASS | PASS |
| Explorer（Server同梱のリボンUI版） | PASS | PASS | PASS |
| タスクスケジューラ（MMC） | PASS | PASS | PASS |
| WPF fixture | PASS | PASS | PASS |
| WinForms fixture | PASS | PASS | PASS |

追加ボタンは通常・復元後47×30px、最大化47×22pxで、基準ボタンと幅・上端・下端が一致した。Microsoft Storeと電卓はServerに存在せずunavailable。後の実行`36303506029`でもServerのジョブは成功した。

Windows 11の画像`20260920.164.1`（Enterprise build26200、Arm64）にはNotepad11.2605.34.0、Microsoft Store22506.1400.2.0、電卓11.2502.2.0がインストールされていた。しかしスクリーンショットには実アプリではなく初回のプライバシー設定画面が写り、既知色のsentinelも画面上で確認できなかった。観測と表示の不一致の根本原因は未確定。これらのWindows 11アプリの描画を合格とも製品不具合とも判定していない。

Windows 11の実画面を検証できる環境が成立するまで、同ジョブは赤のまま残す。合格に見せるためのcontinue-on-errorや画素検査の無効化は行わない。

