# MoveToMonitorButton

Windowsの前面ウィンドウのタイトルバーに「別モニターへ移動」ボタンを重ねる、MITライセンスの小さなユーティリティです。

## ダウンロード

[最新リリース](https://github.com/imksoo/add-move-display-button/releases/latest)のAssetsから、`MoveToMonitorButton.exe`または`MoveToMonitorButton-v0.1.8-windows-x64.zip`を取得してください。ZIPには使い方とビルド情報を同梱しています。証跡ZIPとSHA256SUMS.txtも別アセットとして公開します。

## 0.1.8：小型配置の中央優先とReleases配布

小型ボタンを使う場合も、標準ボタン列の縦中央にそろう安全な候補を最初に探索します。中央を置けないときは従来の安全な候補へ戻し、タブや独自ボタンを上書きしません。アイコンの倍率、標準サイズの配置、移動手順は変更していません。

GitHub Releasesはmainへのpush後、MSVC Release/Debugと回帰試験、Server 2022/2025の実アプリ試験が通った場合に版ごとに一度だけ公開します。試験したEXEを再ビルドせず使用し、アップロード後のダウンロード・ハッシュ照合も行います。既存のリリース資産は上書きしません。

## 使い方

Windows 10/11 x64でZIPを展開し、`MoveToMonitorButton.exe`を通常権限で起動します。旧版は先にトレイの「終了」で閉じてください。既定では2画面以上の拡張表示で有効です。

- 左クリック：次のモニターへ移動。
- 右クリック：移動先モニターを選択。
- トレイ：移動、一時停止、表示診断、終了。

対象は前面ウィンドウ1個です。DLL注入、通信、自動起動登録、設定の永続保存、自動昇格は行いません。昇格したアプリの操作には権限差による制限があります。最大化は一度復元して移動後に再最大化します。

標準ボタンの幅・高さ・縦位置を取得して同寸法の配置を優先します。元のタイトルバーをほぼ透かして記号を描き、ホバー／押下時は薄い明暗を重ねます。DWM標準ボタンの完全複製ではありません。基準ボタン周辺のRGBを5点だけ読みアイコン色を選択しますが、画像やウィンドウのタイトル・ファイル名は保存／送信しません。高コントラスト時はシステム色を使います。

独自フレームや空き領域のないアプリでは小型の配置へ戻るか非表示になります。トレイからの移動も利用できます。位置推定は明示的なウィンドウ単位の選択だけで、既定では無効です。

## 0.1.7から継承：タブ付きExplorerの寸法追従

入力用子ウィンドウの上端リサイズ領域と、最大化時の不可視枠による座標差を限定的に扱い、正しい同寸法候補を棄却しないようにしました。アプリ名やクラス名の例外登録は追加していません。詳細は[修正根拠](docs/EXPLORER-SIZING-FIX.md)を参照してください。

## ビルド・試験

Visual Studio 2022／Build ToolsのC++デスクトップ開発、公式Windows SDK、CMakeを使用します。C++17、通常のCRT初期化、静的ランタイム(`/MT`)です。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --verbose
./tools/Test-Startup.ps1 -ExecutablePath build/Release/MoveToMonitorButton.exe -OutputPath build/startup-result.json
```

起動試験は、同じツールを常駐させていない検証専用セッションで実行してください。ActionsはRelease／Debugビルド、実SDK・描画・起動・応答・終了を検査します。

`Real application caption compatibility`は2022／2025の実アプリを各3状態で検査します。Explorerも幅・上端・下端の一致を必須とし、小型表示を同寸法の成功にはしません。両環境で成功し、EXEハッシュと30同寸法ケース＋Chromeの6表示ケースを再確認した場合のみ`MoveToMonitorButton-0.1.8-real-app-tested`を公開します。試験したものと同じEXE、起動記録、環境別の結果と画像を含みます。

Windows 11 Armは画面前提が未成立のため、手動実行の`include_windows11=true`で診断します。画素確認や失敗判定を無効にしたものではありません。Serverでの成功はWindows 11の現行NotepadやStoreアプリ、物理モニター間移動、混在DPIの保証ではありません。Store／電卓の未登録はunavailableとして記録します。

## ソースの構造

| ファイル | 担当 |
|---|---|
| `src/main.cpp` | 起動 |
| `src/app.hpp`、`app.cpp` | 所有状態、イベント、移動の状態遷移 |
| `src/placement.hpp`、`placement.cpp` | 入力先と配置候補の判定 |
| `src/diagnostics.cpp` | 診断表示 |
| `src/caption_appearance.*` | アルファ描画と外観 |
| `src/win32_helpers.hpp` | 所有リソースと一時状態のスコープ管理 |
| `src/layout.hpp` | Windowsに依存しない座標計算 |

[設計説明](docs/ARCHITECTURE.md)と[実アプリ試験の経緯](docs/REAL-APP-TESTS.md)も参照してください。

Linuxでは製品と同じ実装にAPIダブルを組み合わせて回帰試験を実行し、Windows EXEは生成しません。

```sh
cmake -S . -B build-host -DCMAKE_CXX_COMPILER=clang++ -DMTMB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure --verbose
```

`--quit`は終了、`--startup-check`はローダー確認、`--help`は説明です。`--show-on-single-monitor`は表示試験用で、モニター数を増やしません。未署名のため組織の利用ポリシーに従い、セキュリティ機能は無効にしないでください。
