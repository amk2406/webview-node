# Changelog

All notable changes to this project will be documented in this file.

## [0.1.3] – 2026-10-07

### Fixed
- `loadURL` / `loadUrl` no longer crash when called before `show()` — navigation is queued until the window is ready
- Added `loadUrl` alias (same as `loadURL`)
- Constructor options `x`, `y`, `minWidth`, `minHeight`, `maxWidth`, `maxHeight`, `userDataFolder`, `backgroundColor` are now correctly forwarded
- `setPosition`, `setSize`, `setMinSize`, `setMaxSize`, `setBackgroundColor`, `focus`, `blur`, `center` are queued before `show()` and sent after
- Dialog helpers now send both top-level `message`/`title` fields and `args` for reliable native parsing
- `dialog.confirm` normalizes string results (`"true"` / `"false"`) to a real boolean
- Dialogs reject clearly if called before `show()`
- Dialog promises time out after 120s instead of hanging forever

### Native host (rebuild required)
- Apply `userDataFolder` when creating the WebView2 environment
- Honour min/max size via `WM_GETMINMAXINFO`
- Implement `setPosition`, `setSize`, `setMinSize`, `setMaxSize`, `setBackgroundColor`, `focus`, `blur`, `center`
- Apply initial `x`/`y` and `backgroundColor` on window creation
- Improve dialog message/title extraction for info/error/warning/confirm
- Page-side file/folder picker bridge (from HTML) is **not** fully wired yet — still planned

### Documentation
- README updated for 0.1.3 APIs and known limitations

## [0.1.1]

### Added
- `windowsHide` constructor option to control whether Node hides the native host process window when it starts

### Documentation
- Clarified that windows are created and displayed with `win.show()`

## [0.1.0] – 2026-10-04

### Changed
- Package name is `webview-node`
- `new WebView()` no longer opens a window. Call `win.show()` to create and show it.
- Added `win.hide()` to hide without destroying
- One shared native host process for all windows
- Host path env: `WEBVIEW_HOST` (still accepts `WEBVIEW2_HOST`)

### Added
- Initial `WebView` class with Electron-like API
- Constructor options, window control, content methods, setters, events, dialogs, global `app`
- TypeScript definitions

### Notes
- Requires the native host (`webview2-host.exe`)
- Currently Windows-only
