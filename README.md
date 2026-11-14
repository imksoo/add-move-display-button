# MoveToMonitorButton

Windowsのタイトルバー付近に「別モニターへ移動」ボタンを追加する、無料・MITライセンスのユーティリティです。

## ダウンロードと使い方

[GitHub Releases](https://github.com/imksoo/add-move-display-button/releases/latest)の`MoveToMonitorButton.exe`またはWindows x64用ZIPを使います。旧版をトレイの「終了」で閉じ、通常権限で起動してください。既定では2画面以上の拡張表示で有効です。

- 左クリック：そのウィンドウを次のモニターへ移動。
- 右クリック：そのウィンドウの移動先を選択。
- トレイ：移動、一時停止、自動起動の設定、表示診断、終了。

**自動起動**は、トレイアイコンを右クリックし「Windowsへのサインイン時に起動」をオンにします。現在のユーザーだけに適用され、管理者権限は不要です。もう一度選ぶとオフになり、登録を削除します。初期状態はオフです。EXEを移動・削除する前にオフにし、移動先で必要なら再登録してください。Windows側の「アプリ → スタートアップ」で無効にした場合は、そちらも確認してください。

**非アクティブなウィンドウにも、見えているタイトルバーにボタンを表示します。** マウスを乗せただけでは対象を前面にしません。背景のアプリでも、最初のクリックから対応する操作を開始できます。背景用ボタンは対象の重なり順に合わせ、別のアプリを突き抜けて最前面に表示しない構成です。最小化・非表示・終了した対象のボタンは消します。

他アプリへDLL注入、サブクラス化、入力キューの結合はしません。ネットワーク通信や自動昇格も行いません。自動起動をオンにした場合だけ、現在のユーザーのRunキーへ引用符付きのEXEパスを保存します。通常権限と昇格アプリの間にはWindowsの操作制限があります。

## 外観と制限

アプリ名に依存せず、独自タイトルバーを使う通常のサイズ変更可能なウィンドウも検出します。標準フレーム属性が省略されている場合は、実際の最小化・最大化ボタンの当たり判定から位置を測ります。DPIごとの画面座標を使い、配置先の5点を入力先ウィンドウで検査します。安全なドラッグ領域を確認できないアプリには自動表示しません。

標準ボタンの幅・高さ・縦位置を取得して同寸法の配置を優先します。安全な余白が足りない独自タイトルバーでは、小型ボタンを縦中央に置く候補を先に試します。それも置けなければ別の安全な位置か非表示に戻します。タブなどを無条件に覆う例外は追加していません。

背景は元のタイトルバーをほぼ透かし、ホバー／押下時は薄い明暗を重ねます。DWMの完全複製ではありません。基準ボタン余白のRGBを5点だけ読み、白／黒の記号を選びます。他のウィンドウが重なっている場所は色取得に使いません。製品が画像・タイトル・ファイル名を保存／送信することはありません。

最大化ウィンドウは一度復元して移動し、移動先で再最大化します。独自フレーム、空き領域がない場合、昇格や保護された対象では表示／操作できない場合があります。トレイからの移動と個別の位置推定も用意しています。位置推定は既定で無効です。

## ソースとビルド

Visual Studio 2022／Build ToolsのC++デスクトップ開発、公式Windows SDK、CMakeを使用します。C++17、標準CRT初期化、静的ランタイム(`/MT`)です。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --verbose
./tools/Test-Startup.ps1 -ExecutablePath build/Release/MoveToMonitorButton.exe -OutputPath build/startup-result.json
```

起動試験は専用の対話デスクトップで実行してください。既に常駐している同アプリがある場合は試験しません。コードは[設計説明](https://github.com/imksoo/add-move-display-button/blob/main/docs/ARCHITECTURE.md)の単位に分けています。

## CIとリリース

`Real application caption compatibility`がMSVC Release／Debug、公式SDK試験、Linux ASan／UBSan、実アプリ描画試験と非アクティブ操作試験を実行します。2022／2025ではNotepad、Explorer、タスクスケジューラ、WPF／WinForms検証用アプリ、Chromeの計36ケースを検査します。Chrome以外の30ケースは標準ボタンとの寸法一致が必須です。

非アクティブ操作は、配布する実EXEと2枚の検証用ウィンドウを使い、ホバーでの非アクティブ維持、最初の左押下とキャンセル、最初の右クリックメニュー、重なり、最小化、位置追従、前面切替、終了を検査します。1画面での試験なので、物理モニター間の左クリック移動そのものを実証したものではありません。

すべての必須ジョブが成功した場合に限り、同じEXEのハッシュと結果を再照合してリリース用ファイルを作ります。`main`ではGitHub ReleasesへEXE・ポータブルZIP・SHA256SUMSを公開します。ポータブルZIPの内容はEXE・README・LICENSE・EXEのSHA256SUMSの4点です。試験結果、ログ、画面画像、ビルド情報はActionsの成果物に14〜30日保持し、配布ZIPには含めません。リリース説明には検証したActions実行へのリンクを載せます。公開前にアップロード済みファイルを再ダウンロードしてハッシュを確認し、既存リリースの同名資産は上書きしません。

Windows 11 Armの使い捨てランナーでは、初回privacy UIなどの環境準備を別ステップで行い、元の厳密な2色preflightの後、現行Notepad／Explorer／Store／電卓／Windows設定の通常・最大化・復元後を同じ必須jobで検査します。設定アプリは100%・150%・200%の実際の表示倍率で、UI Automationが返す標準3ボタンと追加ボタンの縦中心差が0.5画面ピクセル以内であることを確認します。倍率ごとに2色preflightも再実行します。[Issue #2の診断・修復と証跡](https://github.com/imksoo/add-move-display-button/blob/main/docs/WINDOWS11-DESKTOP-DIAGNOSIS.md)を参照してください。Store／電卓が未登録ならunavailableであり、Serverの成功で代替しません。この検証結果は記録したコミットのx64 EXEに対するもので、公開済みv0.1.8への遡及的な合格宣言ではありません。実際の複数モニター間の混在DPI切替や全テーマの実機確認は別の検証です。

ChatGPTとChatGPT ClassicはMicrosoft Storeの公式パッケージを使い、100%・150%・200%で通常／最大化／復元後の計18ケースを追加検査します。新ChatGPTでは独立したUI Automation計測との縦中心差が0.5画面ピクセル以内であることも必須です。使い捨て環境の未ログイン画面を対象とし、ログイン後の全画面構成を確認したものではありません。これらの証跡も配布物には含めません。

LinuxではWindows EXEを生成せず、APIダブルによるロジック試験のみを行います。

```sh
cmake -S . -B build-host -DCMAKE_CXX_COMPILER=clang++ -DMTMB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure --verbose
python tests/package_release_test.py
```

`--quit`は終了、`--startup-check`はローダー確認、`--help`は説明です。`--show-on-single-monitor`は表示試験専用です。製品は未署名です。組織の利用ポリシーに従い、セキュリティ機能を無効にしないでください。

