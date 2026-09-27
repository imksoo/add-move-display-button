# MoveToMonitorButton

Windowsの前面ウィンドウのタイトルバー付近に「別モニターへ移動」ボタンを重ねる、MITライセンスの小さなユーティリティです。

## 使い方

Windows 10/11 x64でZIPを展開し、`MoveToMonitorButton.exe`を通常権限で起動します。起動中の旧版は、先にトレイの「終了」で閉じてください。既定では2台以上のモニターを「拡張」にしている場合に表示します。

- ボタンを左クリック：次のモニターへ移動。
- ボタンを右クリック：移動先モニターを選択。
- トレイメニュー：移動、表示の一時停止、表示診断、終了。

対象は前面ウィンドウ1個です。最小化・最大化・閉じるボタンを直接改造せず、独立したオーバーレイを使用します。DLL注入、ネットワーク通信、自動起動登録、設定の永続保存は行いません。昇格したアプリの操作には権限差による制限がありますが、本ツールは自動昇格しません。

最大化ウィンドウは一度復元して移動し、移動先で再最大化します。サイズはDPIと作業領域を考慮して調整します。独自タイトルバー、全画面アプリなどでは表示しない場合があり、トレイからの移動も利用できます。位置推定は既定で無効で、明示的に選んだウィンドウだけに適用します。

## 0.1.6：標準タイトルバーボタンへの追従

`WM_GETTITLEBARINFOEX`で最小化・最大化・閉じるボタンの個別矩形を取得し、まず同じ幅・高さ・縦位置・間隔の配置を試します。値が不正、DWMの範囲と矛盾、空き領域なしの場合は0.1.5の配置に戻します。新しい例外登録は不要です。

背景はレイヤードウィンドウのアルファ合成で元のタイトルバーをほぼそのまま透かし、モニター移動の記号だけを描きます。通常時は全領域をalpha=1/255にして、空白部分でもクリック可能にしています。ホバー・押下は控えめな明暗のオーバーレイで、DWM標準ボタンの完全複製ではありません。高コントラストではシステム色の不透明表示に切り替えます。

アイコンの白黒を決めるため、前面ウィンドウの基準ボタンの余白からRGBを5点だけ読みます。画像・タイトル文字列・ファイル名は保存・送信しません。読めない場合は公開UxThemeのシステム色を使います。Micaなどの背景は再描画しないので残りますが、標準ボタンのホバーアニメーションを複製するものではありません。

診断画面に基準ボタン、実測矩形、実測サイズ採用、間隔、寸法照会エラーを追加しました。`実測サイズ採用: 1`が同寸法で配置できたことを示します。サイズ変更、DPI変更、テーマ変更時に更新します。

Windows CIでは通常のWin32テストウィンドウを作り、実際の配布EXEの追加ボタンを検査します。矩形一致、透明な空白部分のヒットテスト、繰り返し更新、通常／ホバーの画面記録を含みます。DPI100/125/150/200%の描画とalphaの検査はメモリー画像での試験であり、実モニターの混在DPI試験とは区別しています。

## 0.1.5：動作条件を維持した可読性の改善

0.1.4の配置条件・入力先判定・移動手順を維持し、SDK補助関数とC++17の標準ライブラリで内部を整理しました。エクスプローラー用の例外登録や、新しい自動推定条件を追加する変更ではありません。

| 役割 | 読む場所 |
|---|---|
| 起動 | `src/main.cpp` |
| アプリの状態・イベント・移動の状態遷移 | `src/app.hpp`、`src/app.cpp` |
| ボタン位置と診断値の算出 | `src/placement.hpp`、`src/placement.cpp` |
| 診断値を日本語の文章にする処理 | `src/diagnostics.cpp` |
| 所有ハンドルと一時状態のスコープ管理、SDKによる引数解析 | `src/win32_helpers.hpp` |
| Windowsに依存しない座標計算（変更なし） | `src/layout.hpp` |

詳細は [設計・リファクタリング方針](docs/ARCHITECTURE.md) を参照してください。独自の文字列変換・引数解析は削除し、Win32型やメッセージの`switch`はSDKとの接点に残しています。外部の実行時依存ライブラリは追加していません。

## Windowsでビルド・検証

Visual Studio 2022またはBuild Toolsの「C++によるデスクトップ開発」と公式Windows SDK、CMakeを用意します。製品はMSVC、SDKのヘッダー／ライブラリ、通常の`wWinMain`と静的CRT（`/MT`）を使用します。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --verbose
./tools/Test-Startup.ps1 -ExecutablePath build/Release/MoveToMonitorButton.exe -OutputPath build/startup-result.json
```

最後の試験は検証専用のWindowsセッションで実行してください。同じアプリを常駐させたまま実行すると二重起動の検査により失敗します。

GitHub Actionsの`Windows SDK build and startup tests`は`windows-2022`でRelease／Debugをビルドし、警告をエラー扱いにします。座標・公式SDK ABI・実際のSDK補助機能とRAIIの試験に加え、配布対象のEXEを起動して検査します。引数なしの通常起動、ホスト／オーバーレイウィンドウ生成、常駐後のUIスレッド応答、`--quit`による正常終了が対象です。

Releaseのビルド・Windows試験がすべて成功した場合だけ、`MoveToMonitorButton-0.1.6-windows-x64-msvc`をアップロードします。EXE、使い方、設計説明、ビルド環境、試験結果、SHA-256を同梱し、コミットとActions実行URLも記録します。

これらはWindows 11のエクスプローラーへの見た目や、異なるDPIの実モニター間のクリック移動を網羅するGUI試験ではありません。成果物は未署名です。セキュリティ機能を無効化せず、組織の利用ポリシーに従ってください。

## Linuxでのロジック試験

```sh
cmake -S . -B build-host -DCMAKE_CXX_COMPILER=clang++ -DMTMB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure --verbose
```

LinuxではEXEを生成しません。製品と同じ実装を別翻訳単位としてコンパイルし、Win32 APIだけを模擬します。`main.cpp`の直接取り込みは廃止しています。`tests/win32_test_api.hpp`の定義はWindows製品やWindows試験には使いません。

変更対象のC++ファイルには`.clang-format`を適用し、CIでもclang-format 18.1.8で検査します。フォーマッターは開発用であり、アプリの実行には不要です。

## コマンドライン

`--quit`で常駐を終了、`--startup-check`でローダーの起動確認、`--help`で説明を表示します。`--show-on-single-monitor`は1画面で表示を調べるためのオプションで、移動先モニターを増やすものではありません。
