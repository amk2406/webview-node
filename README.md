# webview-node – Complete Guide (A to Z)

A lightweight **Windows desktop window** library for Node.js.  
It gives you an Electron-like API, but the real window is created by a small native host that uses **Microsoft Edge WebView2**.

You write normal Node.js code. The module starts a helper program (`webview2-host.exe`), creates real Windows windows, and shows web pages (or local HTML) inside them.

This README explains **everything** – from the idea to every method and event – with many clear examples and simple English.

---

## What's new in 0.1.3

**JS layer (works immediately after `npm install webview-node@0.1.3`):**

- `loadURL` / `loadUrl` safe before `show()` (navigation is queued)
- `loadUrl` alias added
- Constructor options: `x`, `y`, `minWidth`, `minHeight`, `maxWidth`, `maxHeight`, `userDataFolder`, `backgroundColor`
- Setters (`setPosition`, `setSize`, `setMinSize`, `setMaxSize`, `setBackgroundColor`, …) queue until `show()`
- Dialogs send `message` + `title` correctly; `confirm` returns a real boolean
- Clear errors if dialogs are used before `show()`

**Native host (rebuild `webview2-host.exe` from `bin/source` to unlock):**

- `userDataFolder` is passed to WebView2
- Min/max size enforced via `WM_GETMINMAXINFO`
- `setPosition` / `setSize` / `setMinSize` / `setMaxSize` / `setBackgroundColor` / `focus` / `center` implemented
- Initial `x`/`y` and `backgroundColor` applied on create
- Dialogs read top-level `message`/`title`

> **Important:** The published `bin/webview2-host.exe` must be rebuilt from the updated `bin/source/src/main.cpp` for native-side fixes to take effect. The JS fixes work with the existing host for queuing/safety.

---


## Table of Contents

1. [What is this?](#1-what-is-this)
2. [What you need before starting](#2-what-you-need-before-starting)
3. [Installation / project layout](#3-installation--project-layout)
4. [Quick start (first window in 30 seconds)](#4-quick-start-first-window-in-30-seconds)
5. [Creating a window – constructor options](#5-creating-a-window--constructor-options)
6. [Loading content – loadURL & loadFile](#6-loading-content--loadurl--loadfile)
7. [Window control methods](#7-window-control-methods)
8. [Setters – title, icon, size, position](#8-setters--title-icon-size-position)
9. [State helpers – isReady, isMinimized, …](#9-state-helpers--isready-isminimized-)
10. [Events – ready, close, closed, resize, …](#10-events--ready-close-closed-resize-)
11. [Preventing close (close event)](#11-preventing-close-close-event)
12. [Dialogs – info, error, confirm, selectFolder](#12-dialogs--info-error-confirm-selectfolder)
13. [The global `app` object](#13-the-global-app-object)
14. [How Node talks to the native host](#14-how-node-talks-to-the-native-host)
15. [Finding webview2-host.exe](#15-finding-webview2-hostexe)
16. [Complete real-world examples](#16-complete-real-world-examples)
17. [Best practices](#17-best-practices)
18. [Troubleshooting](#18-troubleshooting)
19. [Quick reference (cheat sheet)](#19-quick-reference-cheat-sheet)

---

## 1. What is this?

Imagine you want a small desktop app that shows a website or your own HTML/JS UI, without shipping a full browser like Electron.

This module does that:

- **Node.js** is the boss (your logic, files, network, etc.)
- A small **C++ host** (`webview2-host.exe`) creates the real Windows window and embeds **WebView2** (Edge)
- They talk using short JSON messages over stdin/stdout

**What you get:**

- `new WebView({ url, width, height, ... })`, then `win.show()` to create and display the window
- Methods: `loadURL`, `close`, `minimize`, `maximize`, `setTitle`, `setIcon`, …
- Events: `ready`, `close`, `closed`, `resize`, `minimize`, …
- Simple dialogs: `dialog.confirm()`, `dialog.selectFolder()`, …
- A global `app` with `all-closed` when the last window is gone

**What it is NOT:**

- Not Electron (no Chromium bundle, much smaller)
- Not a browser for end users to type URLs by default (you control the URL)
- Not for macOS or Linux yet (Windows + WebView2 only)
- Not a full browser security sandbox product – treat loaded content carefully

When the Node process exits, the host should exit too. When the last window closes, you usually call `process.exit(0)` from the `all-closed` event.

---

## 2. What you need before starting

| Requirement | Why |
|-------------|-----|
| **Windows 10 / 11** | WebView2 is Windows-only |
| **Node.js 16+** | Modern JS and `child_process` |
| **WebView2 Runtime** | Usually already on Windows 11; on Windows 10 install the Evergreen Runtime from Microsoft if needed |
| **`webview2-host.exe`** | The native helper built from the C++ side of this project |
| **`WebView2Loader.dll`** | Must sit next to `webview2-host.exe` |

Without the host EXE + DLL, the JS module cannot open a window. It will emit an error about the host not found.

---

## 3. Installation / project layout

### Option A – local folder (development)

Copy the `webview-node` folder into your project (or keep it next to the C++ build):

```text
my-app/
  package.json
  index.js
  webview-node/           ← this module
    src/
    types/
    package.json
    README.md
  bin/
    webview2-host.exe     ← from C++ build
    WebView2Loader.dll
```

In code:

```js
const { WebView, app } = require('./webview-node/src');
// or if you point "main" correctly:
const { WebView, app } = require('./webview-node');
```

### Option B – environment variable for the host

```bash
set WEBVIEW2_HOST=C:\path\to\webview2-host.exe
```

### Option C – later as an npm package

When published:

```bash
npm install webview-node
```

```js
const { WebView, app } = require('webview-node');
```

You still must ship or install `webview2-host.exe` + `WebView2Loader.dll` for end users.

---

## 4. Quick start (first window in 30 seconds)

```js
'use strict';

const { WebView, app } = require('webview-node');

const win = new WebView({
  title: 'Hello WebView2',
  width: 1000,
  height: 700,
  url: 'https://www.bing.com',
});

win.on('ready', () => {
  console.log('Window is ready');
});

win.on('closed', () => {
  console.log('Window closed');
});

// Creating the object does not open a window; show it explicitly.
win.show();

app.on('all-closed', () => {
  console.log('No windows left – exit');
  process.exit(0);
});
```

Run:

```bash
node index.js
```

You should see a normal Windows window with Bing (or whatever URL you set).

---

## 5. Creating a window – constructor options

```js
const win = new WebView({
  // Content
  url: 'https://example.com',     // or a local path / file:// URL

  // Size
  width: 1100,
  height: 750,
  minWidth: 500,
  minHeight: 400,
  maxWidth: 1920,                 // optional
  maxHeight: 1080,                // optional

  // Appearance
  title: 'My App',
  icon: './icon.ico',             // path to .ico (or later data URL)

  // Behavior
  resizable: true,
  maximizable: true,
  minimizable: true,
  closable: true,
  center: true,                   // center on screen
  frame: true,                    // false = frameless (when host supports it)
  transparent: false,
  alwaysOnTop: false,
  fullscreen: false,
  windowsHide: true,              // hide the native host process window

  // WebView
  backgroundColor: '#ffffff',
  devTools: true,                 // allow F12 when host enables it
  // userDataFolder: './webview-data',
});
```

### Options explained

| Option | Type | Default | Meaning |
|--------|------|---------|---------|
| `url` | string | — | Page to load. `https://…`, `file:///…`, or a relative file path (resolved to `file://`) |
| `title` | string | `"WebView2"` | Window title bar text |
| `width` / `height` | number | `900` / `600` | Initial size |
| `minWidth` / `minHeight` | number | — | Minimum size |
| `maxWidth` / `maxHeight` | number | — | Maximum size |
| `icon` | string | — | Path to icon file |
| `resizable` | boolean | `true` | User can resize |
| `center` | boolean | `true` | Center on the screen at start |
| `devTools` | boolean | `false` | Allow developer tools when supported |
| `windowsHide` | boolean | `false` | Hide the native host process window when it starts |
| `backgroundColor` | string | `"#ffffff"` | Background color |

`windowsHide` is passed to Node.js when starting the shared native host process. It is determined by the first window that calls `show()`; subsequent windows reuse the already-started host.

Relative paths in `url` that are not `http(s):` or `file:` or `data:` are turned into absolute `file:///` URLs automatically.

```js
// These are equivalent in spirit:
new WebView({ url: './ui/index.html' });
new WebView({ url: 'file:///C:/my-app/ui/index.html' });
```

---

## 6. Loading content – loadURL & loadFile

You can change the page after the window exists.

### loadURL(url) / loadUrl(url)

Safe to call before or after `show()`. If the window is not ready yet, the URL is queued.

```js
win.loadURL('https://www.google.com');
win.loadUrl('https://www.google.com'); // alias
win.loadURL('file:///C:/projects/my-app/ui/index.html');
win.loadURL('data:text/html,<h1>Hello</h1>');
```

### loadFile(filePath)

Convenience for local files:

```js
win.loadFile('./ui/index.html');
// same as loadURL with an absolute file:// URL
```

**Example – switch page after ready:**

```js
win.on('ready', () => {
  win.loadFile('./dashboard.html');
});
```

---

## 7. Window control methods

Call `show()` to create and display the native window. Constructing a `WebView` alone does not open it; calling `show()` again makes a hidden window visible.

```js
win.show();        // create and display the window (or show it again)
win.hide();        // hide without destroying it
win.minimize();   // to taskbar
win.maximize();   // fill work area
win.restore();    // back from minimize/maximize
win.focus();      // bring to front
win.blur();       // remove focus (when supported)
win.flash();      // flash taskbar button
win.center();     // center on screen again
win.close();      // ask to close (see preventable close)
win.close({ force: true }); // force close when host supports it
```

**Example – flash when something important happens:**

```js
function notifyUser() {
  win.flash();
  win.focus();
}
```

---

## 8. Setters – title, icon, size, position

```js
win.setTitle('New title');
win.setIcon('./assets/app.ico');
win.setSize(1200, 800);
win.setMinSize(400, 300);
win.setMaxSize(1600, 900);
win.setPosition(100, 50);       // x, y on screen
win.setBackgroundColor('#1a1a1a');
```

**Example – update title when the in-page route changes** (from your frontend via a future bridge, or from Node):

```js
function onRouteChange(name) {
  win.setTitle('My App – ' + name);
}
```

---

## 9. State helpers – isReady, isMinimized, …

```js
win.isReady();      // true after first ready/start
win.isDestroyed();  // true after the window is gone
win.isFocused();
win.isMinimized();
win.isMaximized();
win.getBounds();    // may be null until native pushes bounds
```

**Example:**

```js
if (!win.isDestroyed()) {
  win.setTitle('Still open');
}
```

Use events for most UI logic; these helpers are for quick checks.

---

## 10. Events – ready, close, closed, resize, …

`WebView` extends Node’s `EventEmitter`.

| Event | When | Typical use |
|-------|------|-------------|
| `ready` / `start` | Window + WebView2 are ready | First load, inject, log |
| `close` | User (or code) asked to close | Save work, `preventDefault()` |
| `closed` | Window is fully destroyed | Cleanup, remove references |
| `resize` | Size changed | `{ width, height }` |
| `move` | Position changed | `{ x, y }` (when host sends it) |
| `minimize` | Minimized | |
| `maximize` | Maximized | |
| `restore` / `unmaximize` | Restored | |
| `focus` / `blur` | Focus changed | |
| `error` | Host / IPC problem | Log and recover |

```js
win.on('ready', () => {
  console.log('ready');
});

win.on('resize', ({ width, height }) => {
  console.log('size', width, height);
});

win.on('minimize', () => console.log('minimized'));
win.on('closed', () => console.log('gone'));
```

`once` works as usual:

```js
win.once('ready', () => {
  win.loadURL('https://example.com/app');
});
```

---

## 11. Preventing close (close event)

When the user clicks the **X**, the native host does **not** destroy the window immediately. It asks Node first.

```js
win.on('close', (event) => {
  // If the user has unsaved work:
  if (hasUnsavedChanges) {
    event.preventDefault(); // window stays open
    // show your own “Save?” UI inside the page or a dialog
  }
});
```

Flow:

1. User clicks X  
2. Host → Node: `close-request`  
3. Node runs your `close` listeners  
4. If nobody called `preventDefault()`, Node replies `allow: true` and the window closes  
5. Host emits `closed` → Node emits `closed`

There is a short timeout on the host so the window cannot hang forever if Node never answers.

```js
win.on('close', (e) => {
  e.preventDefault();
  win.dialog.confirm('Discard changes?').then((ok) => {
    if (ok) win.close({ force: true }); // when force is implemented
  });
});
```

---

## 12. Dialogs – info, error, confirm, selectFolder

Dialogs are exposed as `win.dialog.*` and return Promises.

```js
await win.dialog.info('Saved successfully');
await win.dialog.error('Something went wrong');
await win.dialog.warning('Disk is almost full');

const ok = await win.dialog.confirm('Delete this file?');
if (ok) {
  // user chose Yes
}

const folder = await win.dialog.selectFolder();
if (folder) {
  console.log('User picked', folder);
}

const file = await win.dialog.selectFile({
  multiple: false,
  filters: [{ name: 'Images', extensions: ['png', 'jpg'] }],
});
```

**Notes:**

- `selectFolder` uses the modern Windows folder picker when possible, with a classic fallback in the host.
- Dialogs are modal to that window when the host passes the HWND.
- Always `await` or `.then()` – they are asynchronous because they round-trip to the native process.

---

## 13. The global `app` object

```js
const { WebView, app } = require('./webview-node/src');
```

| Method / event | Meaning |
|----------------|---------|
| `app.getAllWindows()` | Array of open `WebView` instances |
| `app.getFocusedWindow()` | Focused window or `null` |
| `app.closeAll()` | Try to close every window |
| `app.closeAll({ force: true })` | Force when supported |
| `app.on('all-closed', …)` | Fired when the last window has closed |

**Standard desktop pattern:**

```js
app.on('all-closed', () => {
  // On Windows/Linux apps often quit when the last window closes.
  process.exit(0);
});
```

```js
// Later somewhere:
const list = app.getAllWindows();
console.log('Open windows:', list.length);
```

---

## 14. How Node talks to the native host

You normally do **not** send raw messages yourself. The module does it.

Conceptually:

```text
Node (WebView class)
    │  length-prefixed JSON
    ▼
webview2-host.exe
    │  WebView2 + Win32
    ▼
Real window on screen
```

Examples of messages (for understanding only):

**Node → Host**

```json
{ "type": "create-window", "windowId": 1, "options": { "url": "https://bing.com", "width": 1000, "height": 700 } }
```

```json
{ "type": "call", "windowId": 1, "method": "navigate", "args": ["https://github.com"] }
```

**Host → Node**

```json
{ "type": "event", "windowId": 1, "event": "ready", "data": {} }
```

```json
{ "type": "close-request", "windowId": 1, "requestId": "close_123" }
```

Framing: 4-byte little-endian length + UTF-8 JSON body on stdin/stdout.

---

## 15. Finding webview2-host.exe

The IPC layer searches in this order:

1. `process.env.WEBVIEW2_HOST`
2. `<module>/native/webview2-host.exe`
3. `<module>/bin/webview2-host.exe`
4. `<module>/webview2-host.exe`
5. `./webview2-host.exe` (current working directory)
6. `./bin/webview2-host.exe`

Recommended layout for apps you ship:

```text
MyApp/
  MyApp.exe          (or node + your script)
  webview2-host.exe
  WebView2Loader.dll
  app/
    index.js
    ...
```

Or set the env var in a launcher script:

```bat
set WEBVIEW2_HOST=%~dp0webview2-host.exe
node app\index.js
```

---

## 16. Complete real-world examples

### 16.1 Simple browser-like window

```js
'use strict';

const { WebView, app } = require('./webview-node/src');

const win = new WebView({
  title: 'Mini Browser',
  width: 1200,
  height: 800,
  minWidth: 600,
  minHeight: 400,
  center: true,
  url: 'https://www.bing.com',
  devTools: true,
});

win.on('ready', () => console.log('Ready'));
win.on('resize', ({ width, height }) => {
  // optional: save size to disk
});

app.on('all-closed', () => process.exit(0));

win.show();
```

### 16.2 Local HTML UI

```js
const path = require('path');

const win = new WebView({
  title: 'My Tools',
  width: 900,
  height: 600,
  url: path.join(__dirname, 'ui', 'index.html'), // auto file://
});

win.on('ready', () => {
  console.log('UI loaded');
});

win.show();
```

### 16.3 Confirm before exit

```js
let canClose = false;

win.on('close', (e) => {
  if (!canClose) {
    e.preventDefault();
    win.dialog.confirm('Quit the application?').then((yes) => {
      if (yes) {
        canClose = true;
        win.close();
      }
    });
  }
});
```

### 16.4 Two windows

```js
const main = new WebView({ title: 'Main', width: 900, height: 600, url: 'https://example.com' });
const help = new WebView({ title: 'Help', width: 500, height: 400, url: './help.html' });

app.on('all-closed', () => process.exit(0));

main.show();
help.show();
```

### 16.5 Open a folder and show it in the title

```js
win.on('ready', async () => {
  const folder = await win.dialog.selectFolder();
  if (folder) {
    win.setTitle('Folder: ' + folder);
  }
});
```

---

## 17. Best practices

1. **Always handle `all-closed`**  
   Otherwise Node may keep running with no windows.

2. **Prefer events over polling**  
   Use `resize`, `closed`, `ready` instead of timers checking `isDestroyed()`.

3. **Keep the host next to the app**  
   Or set `WEBVIEW2_HOST` in production installers.

4. **Ship `WebView2Loader.dll`**  
   Same folder as `webview2-host.exe`.

5. **Do not use `-mwindows` on the host build**  
   Stdin/stdout must stay connected for IPC. The host hides the console itself.

6. **Treat web content carefully**  
   If you load remote URLs, think about what the page can do. Prefer local UI for trusted apps.

7. **One host process, many windows**  
   The design allows multiple `WebView` instances; avoid starting extra hosts unless you change the IPC layer.

8. **Clean up on `closed`**  
   Drop references so memory can be freed.

```js
const windows = new Set();

function create() {
  const w = new WebView({ url: 'https://example.com' });
  windows.add(w);
  w.on('closed', () => windows.delete(w));
  w.show();
  return w;
}
```

---

## 18. Troubleshooting

| Problem | What to try |
|---------|-------------|
| `Native host not found` | Put `webview2-host.exe` in `bin/` or set `WEBVIEW2_HOST` |
| Window never appears | Install WebView2 Runtime; check Task Manager for `webview2-host.exe` |
| Black console window | Set `windowsHide: true` in the first `WebView` options passed to `show()` |
| `undefined reference to wWinMain` when building host | Do **not** use `-municode` / `-mwindows` on the host |
| Close button does nothing for 3 seconds | Node did not answer `close-request` – check your `close` handler |
| `selectFolder` does nothing | Host must implement dialog; ensure COM init succeeded |
| Page stays blank | Call `loadURL` only after `ready`, or pass `url` in the constructor |

Enable logging by listening to `error`:

```js
win.on('error', (err) => console.error('WebView error', err));
```

---

## 19. Quick reference (cheat sheet)

```js
const { WebView, app } = require('./webview-node/src');

// Create
const win = new WebView({
  title: 'App',
  width: 1000,
  height: 700,
  url: 'https://example.com',
  center: true,
});

// Content
win.loadURL('https://…');
win.loadFile('./index.html');

// Control
win.show();
win.hide();
win.minimize();
win.maximize();
win.restore();
win.focus();
win.flash();
win.close();

// Setters
win.setTitle('…');
win.setIcon('./icon.ico');
win.setSize(w, h);
win.setPosition(x, y);

// Events
win.on('ready', () => {});
win.on('close', (e) => { /* e.preventDefault() */ });
win.on('closed', () => {});
win.on('resize', ({ width, height }) => {});

// Dialogs
await win.dialog.confirm('Sure?');
await win.dialog.selectFolder();

// App
app.getAllWindows();
app.on('all-closed', () => process.exit(0));
```

---

## License

MIT (same idea as the rest of the framework – free to use and modify).

---

Happy building with WebView2 and Node.js.
