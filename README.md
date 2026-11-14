# MoveToMonitorButton

**Move a window to another display with one click from its title bar.**

**English** · [日本語](README.ja.md)

[**Download for Windows**](https://github.com/imksoo/add-move-display-button/releases/latest) · [Report a problem](https://github.com/imksoo/add-move-display-button/issues)

Working across two or more monitors? Keep a document beside your browser, move a chat window to your secondary display, or move a maximized app without dragging it across your desktop. MoveToMonitorButton adds a small monitor-and-arrow button beside a window's existing controls.

![MoveToMonitorButton beside the minimize, maximize and close controls in Windows Settings at 200% scaling](docs/images/settings-200-percent.png)

*Windows Settings at 200% scaling. The monitor-and-arrow icon to the left of Minimize is the added button. This is a capture of the actual Windows application.*

## A familiar place for one more useful action

| What you want to do | How it works |
|---|---|
| Move a window to your next display | Left-click the added button. |
| Choose a specific display | Right-click the button and select a destination. |
| Move a maximized window | The app restores it, moves it, and maximizes it again. |
| Work with a window in the background | Visible, unobstructed title bars can show their own button. Hovering does not bring the window to the front. |
| Keep it ready after signing in | Turn on the optional Windows sign-in startup setting in the tray menu. |

- **A visible shortcut:** the control stays with the window you want to move.
- **Portable:** download one EXE and run it. No installer is required.
- **Fits modern desktops:** supports standard title bars and compatible custom frames; placement accounts for display scaling.
- **Free and open source:** MIT licensed, with no account required by this utility.

## Works with compatible custom title bars

The button can also appear on apps such as the new ChatGPT desktop app. Detection uses window capabilities and caption hit-testing, rather than a list of application names.

![The added move button on the actual ChatGPT desktop title bar at 150% scaling](docs/images/chatgpt-150-percent.png)

*ChatGPT at 150% scaling. Actual title-bar capture from the app before sign-in; the move button sits to the left of its existing controls.*

The app checks for space where the window reports a draggable title bar. If it cannot confirm a suitable area, it leaves the button hidden. Custom frames, crowded title bars, and apps running with higher privileges can limit availability.

## Get started

1. Use **Extend** display mode with at least two monitors. The button is hidden on a single-monitor desktop by default.
2. Download `MoveToMonitorButton.exe`, or extract the portable ZIP, from [Releases](https://github.com/imksoo/add-move-display-button/releases/latest).
3. Put the EXE in a folder you intend to keep and run it with normal user permissions.
4. Use the added button to move windows. Right-click the notification-area icon for startup, pause, diagnostics, and exit.

The download is an **x64 EXE**. That same executable is also tested on **Windows 11 Arm** under x64 emulation. The tray menus are currently in Japanese; this README provides the English guide.

### Start with Windows

Right-click the tray icon and select **「Windowsへのサインイン時に起動」** (“Start when signing in to Windows”). Startup is off by default and applies only to your current user. Select it again to remove the registration.

Before moving or deleting the EXE, turn startup off. If you move it, enable startup again from the new location. If Windows has disabled it under **Settings → Apps → Startup**, check that setting too.

### Update or remove

Choose **「終了」** (“Exit”) from the old version's tray menu before replacing it. To remove the utility, turn off its startup setting, exit it, and delete the EXE.

## Compatibility and transparency

Windows Settings and the ChatGPT title bar are checked at **100%, 150%, and 200% scaling**, including normal, maximized, and restored windows. Other real-app checks include Explorer, Notepad, Chrome, Microsoft Store, and Calculator. The exact tested app versions and results are recorded in [GitHub Actions](https://github.com/imksoo/add-move-display-button/actions/workflows/real-apps.yml).

These tests cover real application windows on disposable desktops. ChatGPT is tested before sign-in. Physical moves between monitors with different DPI settings and every possible app layout are not covered by those CI checks.

The utility does not inject DLLs into other apps or make network requests. It does not save or transmit screenshots, window titles, or filenames. The EXE is currently unsigned.

[Build and test the source](docs/DEVELOPMENT.md) · [Architecture (Japanese)](docs/ARCHITECTURE.md) · [MIT License](LICENSE)
