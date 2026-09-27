# MoveToMonitorButton

Windowsの前面ウィンドウのタイトルバー付近に「別モニターへ移動」ボタンを重ねる、MITライセンスの小さなユーティリティです。

## 使い方

Windows 10/11 x64でZIPを展開し、`MoveToMonitorButton.exe`を通常権限で起動します。既定では2台以上のモニターを「拡張」にしている場合に表示します。

- ボタンを左クリック：次のモニターへ移動。
- ボタンを右クリック：移動先モニターを選択。
- トレイメニュー：移動、表示の一時停止、表示診断、終了。

対象は前面ウィンドウ1個です。最小化・最大化・閉じるボタンを直接改造せず、独立したオーバーレイを使用します。DLL注入、ネットワーク通信、自動起動登録、設定の永続保存は行いません。昇格したアプリの操作には権限差による制限がありますが、本ツールは自動昇格しません。

最大化ウィンドウは一度復元して移動し、移動先で再最大化します。サイズはDPIと作業領域を考慮して調整します。独自タイトルバー、全画面アプリなどでは表示しない場合があり、トレイからの移動も利用できます。位置推定は既定で無効で、明示的に選んだウィンドウだけに適用します。

## 0.1.4：Windowsの正式なビルド環境へ移行

エクスプローラーへの表示が確認された0.1.3の入力先判定・ボタン配置・移動処理を引き継ぎ、配布ビルドをMSVC＋公式Windows SDKに限定しました。

製品はSDKのヘッダーとインポートライブラリ、通常の`wWinMain`／CRT初期化、静的MSVCランタイム（`/MT`）を使用します。独自ABI、手作りインポートライブラリ、独自エントリーポイント、CRTなしのビルドは配布経路から削除しています。`tests/win32_test_api.hpp`はLinux上のAPI模擬試験専用で、WindowsのEXEには使いません。

## Windowsでビルド

Visual Studio 2022またはBuild Toolsの「C++によるデスクトップ開発」とWindows SDK、CMakeを用意します。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --verbose
./tools/Test-Startup.ps1 -ExecutablePath build/Release/MoveToMonitorButton.exe -OutputPath build/startup-result.json
```

最後の試験は検証専用のWindowsセッションで実行してください。同じアプリを常駐させたまま実行すると二重起動の検査により失敗します。

## GitHub Actionsと成果物

`Windows SDK build and startup tests`は`windows-2022`でRelease／Debugをビルドします。警告はエラー扱いです。

1. 座標計算の回帰試験と公式SDKの構造体サイズ・オフセット確認。
2. **配布対象と同じEXE**を`--startup-check`で実行し、Windowsローダー・CRT・エントリーポイントと終了コードを確認。
3. **引数なしで通常起動**し、ホスト／オーバーレイウィンドウの生成、数秒後のプロセス生存、UIスレッドの応答、`--quit`での正常終了を確認。

Releaseの全手順が成功した場合だけ、`MoveToMonitorButton-0.1.4-windows-x64-msvc`をアップロードします。EXE、README、LICENSE、`build-info.json`、`startup-result.json`、`test-results.xml`、`SHA256SUMS.txt`を含みます。MSVC・SDKの実際の版、コミット、Actions実行URL、EXEのSHA-256を記録します。

これらの起動試験は、Windows 11上でのエクスプローラーへの実際の表示、クリック移動、異なるDPIの実モニター間移動を網羅する試験ではありません。LinuxのAPI模擬試験も別ジョブとして残し、Windows実行試験とは明確に分離しています。

成果物は未署名です。セキュリティ機能を無効化せず、組織の利用ポリシーに従ってください。

## コマンドライン

`--quit`で常駐を終了、`--startup-check`でローダーの起動確認、`--help`で説明を表示します。`--show-on-single-monitor`は1画面で表示を調べるためのオプションで、移動先モニターを増やすものではありません。

## Linuxでのロジック試験

```sh
cmake -S . -B build-host -DCMAKE_CXX_COMPILER=clang++ -DMTMB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure --verbose
```

LinuxではEXEを生成しません。旧クロスビルド用のスクリプト／ランタイムはこのリポジトリに含めていません。
