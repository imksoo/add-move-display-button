# MoveToMonitorButton — quick start / 使い方

English guide: https://github.com/imksoo/add-move-display-button#readme
日本語: https://github.com/imksoo/add-move-display-button/blob/main/README.ja.md

Use two or more monitors in Extend mode, then run MoveToMonitorButton.exe with normal user permissions. Two displays use one button; three or more use previous/next buttons in display-number order, with wraparound. Hover to check the destination, or right-click to choose it.

モニターを2台以上接続し、表示モードを「拡張」にして、EXEを通常権限で起動します。2画面では1ボタン、3画面以上では番号順の前・次の2ボタンになります。端では循環します。マウスを重ねると移動先番号を表示し、右クリックで移動先を選べます。

If numbering differs from Windows Identify, use “Match Windows display numbers…” in the right-click menu. Windowsの「識別」と番号が違う場合は、同メニューで番号を補正・保存できます。

Language / 表示言語:
The UI follows Windows display language (Japanese, otherwise English). Use the tray's “Language / 言語” submenu to choose English, 日本語, or automatic selection. Changes apply immediately and persist for the current user.
Windowsの表示言語に従って日本語／英語を選びます。トレイの「言語 / Language」で日本語・English・自動選択を変更でき、すぐに反映・保存します。

Optional startup / 自動起動:
Right-click the tray icon and select “Start when signing in to Windows”. Default: off. Current user only. Select again to remove it. Turn it off before moving or deleting the EXE, and register again after moving it.

トレイの「Windowsへのサインイン時に起動」で設定します。初期状態はオフ、現在のユーザーだけに適用します。再選択で解除します。EXEを移動・削除する前にオフにし、移動後は再登録してください。

Update / 更新:
Exit the old version through “Exit” in its tray menu before replacing the EXE.
旧版をトレイの「終了」で閉じてからEXEを入れ替えてください。

Download and updates: https://github.com/imksoo/add-move-display-button/releases/latest
MIT License. x64 executable; unsigned. Some custom or elevated windows may not support the button. Maximize-button long press is not implemented.
