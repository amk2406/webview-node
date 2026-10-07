# WebView2 Native Host

Lightweight native host for a Node.js WebView2 framework.  
It creates real Windows windows powered by **Microsoft Edge WebView2** and talks to Node.js over **stdin/stdout** using a simple length-prefixed JSON protocol.

This is **not** the third-party `webview/webview` library — it uses the official Microsoft WebView2 Runtime and `WebView2Loader.dll`.

---

## Features

- Official Microsoft WebView2 (Edge)
- Multiple windows (identified by `windowId`)
- Length-prefixed JSON IPC with Node.js
- Preventable close protocol (`close-request` / `close-response`)
- Window control: minimize, maximize, restore, flash, setTitle, setIcon, navigate
- Modern folder picker (`IFileOpenDialog`) with classic `SHBrowseForFolder` fallback
- Basic dialogs (info / error / warning / confirm)
- Compiles with **MinGW-w64 / g++**
- Checks for `WebView2Loader.dll` at startup and exits cleanly if missing

---

## Requirements

| Item | Notes |
|------|--------|
| Compiler | MinGW-w64 (g++ with C++17) |
| WebView2 SDK | Headers + `WebView2Loader.dll` from the NuGet package |
| WebView2 Runtime | Must be installed on the machine (Windows 11 usually has it) |
| OS | Windows 10 / 11 (x64) |

### Getting the WebView2 SDK files

1. Download the NuGet package:  
   https://www.nuget.org/packages/Microsoft.Web.WebView2

2. Extract it and copy:

```text
build/native/include/WebView2.h          →  include/WebView2.h
build/native/x64/WebView2Loader.dll      →  lib/x64/WebView2Loader.dll
```

3. Generate the import library (once):

```bash
cd lib/x64
gendef WebView2Loader.dll
dlltool -d WebView2Loader.def -l libWebView2Loader.a -D WebView2Loader.dll
cd ../..
```

---

## Build

```bash
make check          # verify headers + DLL + import lib
make gen-import-lib # create libWebView2Loader.a if missing
make                # builds bin/webview2-host.exe
```

Output:

```text
bin/webview2-host.exe
bin/WebView2Loader.dll
```

### Manual compile (if you prefer)

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas \
    -Iinclude src/main.cpp \
    -o bin/webview2-host.exe \
    -Llib/x64 -lWebView2Loader \
    -lole32 -loleaut32 -luuid -lshell32 -lshlwapi \
    -luser32 -lcomdlg32 -lgdi32 -ladvapi32 -lversion
```

---

## How it talks to Node.js

The host is started by the Node.js module as a **child process**.

Communication uses **stdin / stdout** with this framing:

```text
[4-byte little-endian length][UTF-8 JSON payload]
```

### Messages Node.js → C++

| type | Purpose |
|------|---------|
| `create-window` | Create a new window + WebView2 |
| `call` | Call a method (`navigate`, `close`, `minimize`, `maximize`, `setTitle`, `setIcon`, `flash`…) |
| `close-response` | Answer to a close request (`allow: true/false`) |
| `dialog` | Show a native dialog (`selectFolder`, `info`, `confirm`…) |

Example – create a window:

```json
{
  "type": "create-window",
  "windowId": 1,
  "options": {
    "url": "https://www.bing.com",
    "title": "My App",
    "width": 1100,
    "height": 750,
    "minWidth": 500,
    "minHeight": 400,
    "center": true,
    "resizable": true,
    "icon": "./icon.ico"
  }
}
```

Example – navigate:

```json
{
  "type": "call",
  "windowId": 1,
  "method": "navigate",
  "args": ["https://github.com"]
}
```

### Messages C++ → Node.js

| type | Purpose |
|------|---------|
| `event` | Window event (`ready`, `start`, `resize`, `minimize`, `maximize`, `restore`, `closed`…) |
| `close-request` | User clicked the X – Node can prevent the close |
| `dialog-response` | Result of a dialog |

Example – window ready:

```json
{
  "type": "event",
  "windowId": 1,
  "event": "ready",
  "data": {}
}
```

Example – close request:

```json
{
  "type": "close-request",
  "windowId": 1,
  "requestId": "close_123456"
}
```

Node must reply with:

```json
{
  "type": "close-response",
  "windowId": 1,
  "requestId": "close_123456",
  "allow": true
}
```

---

## Project layout

```text
webview2-framework/
├── src/
│   └── main.cpp              # Full native host
├── include/                  # WebView2.h goes here
├── lib/
│   └── x64/
│       ├── WebView2Loader.dll
│       └── libWebView2Loader.a
├── bin/                      # Output (webview2-host.exe + DLL)
├── Makefile
└── README.md
```

---

## Using with the Node.js module

1. Build this host → `bin/webview2-host.exe`
2. Copy `webview2-host.exe` + `WebView2Loader.dll` next to your Node project (or into a `bin/` folder)
3. In Node:

```js
const { WebView, app } = require('webview-node');

const win = new WebView({
  title: 'Hello',
  width: 1000,
  height: 700,
  url: 'https://www.bing.com',
});
win.show()
win.on('ready', () => console.log('ready'));
win.on('closed', () => console.log('closed'));

app.on('all-closed', () => process.exit(0));
```

The Node module will automatically spawn `webview2-host.exe` as a child process.

You can also force the path:

```bash
set WEBVIEW2_HOST=C:\path\to\webview2-host.exe
```

---

## Close protocol (important)

When the user clicks the window close button:

1. C++ intercepts `WM_CLOSE` and does **not** destroy the window yet
2. C++ sends `close-request` to Node.js
3. Node.js fires the `close` event (you can call `event.preventDefault()`)
4. Node.js replies with `close-response` + `allow: true/false`
5. If allowed → window is destroyed and `closed` is emitted  
   If not allowed → window stays open

A 3-second timeout exists so the window cannot hang forever if Node never answers.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `WebView2Loader.dll not found` | Place the DLL next to `webview2-host.exe` |
| `undefined reference to CreateCoreWebView2EnvironmentWithOptions` | Run `make gen-import-lib` |
| Window never appears | Check that the WebView2 Runtime is installed |
| Node cannot find the host | Set `WEBVIEW2_HOST` or put the exe in one of the searched paths |
| stdin/stdout not working | Make sure you did **not** compile with `-mwindows` |

---

## License

Free to use and modify for your own framework.
