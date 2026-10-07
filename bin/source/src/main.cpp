/**
 * WebView2 Native Host – talks to Node.js over stdin/stdout
 *
 * Protocol: length-prefixed JSON (4-byte little-endian length + UTF-8 JSON)
 *
 * Incoming (Node → C++):
 *   { "type": "create-window", "windowId": 1, "options": { ... } }
 *   { "type": "call", "windowId": 1, "method": "navigate", "args": ["https://..."] }
 *   { "type": "call", "windowId": 1, "method": "close", "args": [false] }
 *   { "type": "call", "windowId": 1, "method": "minimize" }
 *   { "type": "call", "windowId": 1, "method": "maximize" }
 *   { "type": "call", "windowId": 1, "method": "setTitle", "args": ["New Title"] }
 *   { "type": "call", "windowId": 1, "method": "setIcon", "args": ["./icon.ico"] }
 *   { "type": "call", "windowId": 1, "method": "flash" }
 *   { "type": "close-response", "windowId": 1, "requestId": "...", "allow": true/false }
 *   { "type": "dialog", "windowId": 1, "requestId": "...", "name": "selectFolder" }
 *
 * Outgoing (C++ → Node):
 *   { "type": "event", "windowId": 1, "event": "ready" }
 *   { "type": "event", "windowId": 1, "event": "resize", "data": { "width": 1000, "height": 700 } }
 *   { "type": "event", "windowId": 1, "event": "minimize" }
 *   { "type": "event", "windowId": 1, "event": "maximize" }
 *   { "type": "event", "windowId": 1, "event": "closed" }
 *   { "type": "close-request", "windowId": 1, "requestId": "..." }
 *   { "type": "dialog-response", "requestId": "...", "result": "C:\\folder" }
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <sstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <climits>
#include <cstdio>
#include <cstring>

#include "WebView2.h"

// ---------------------------------------------------------------
// Simple JSON helpers (minimal, no external dependency)
// ---------------------------------------------------------------

static std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

// Very small JSON value extractor (good enough for our protocol)
static std::string JsonGetString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    size_t start = pos + 1;
    size_t end = start;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
        if (json[end] == '"') break;
        end++;
    }
    return json.substr(start, end - start);
}

static int JsonGetInt(const std::string& json, const std::string& key, int def = 0) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    try {
        return std::stoi(json.substr(pos));
    } catch (...) {
        return def;
    }
}

static bool JsonGetBool(const std::string& json, const std::string& key, bool def = false) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return def;
}

// ---------------------------------------------------------------
// IPC – send messages to Node.js
// ---------------------------------------------------------------

static std::mutex g_outMutex;

static void SendToNode(const std::string& json) {
    std::lock_guard<std::mutex> lock(g_outMutex);
    uint32_t len = (uint32_t)json.size();
    // Write 4-byte little-endian length + payload to stdout
    fwrite(&len, 1, 4, stdout);
    fwrite(json.data(), 1, len, stdout);
    fflush(stdout);
}

static void SendEvent(int windowId, const std::string& event, const std::string& dataJson = "{}") {
    std::ostringstream ss;
    ss << "{\"type\":\"event\",\"windowId\":" << windowId
       << ",\"event\":\"" << EscapeJson(event) << "\",\"data\":" << dataJson << "}";
    SendToNode(ss.str());
}

static void SendCloseRequest(int windowId, const std::string& requestId) {
    std::ostringstream ss;
    ss << "{\"type\":\"close-request\",\"windowId\":" << windowId
       << ",\"requestId\":\"" << EscapeJson(requestId) << "\"}";
    SendToNode(ss.str());
}

static void SendDialogResponse(const std::string& requestId, const std::string& result, bool isError = false) {
    std::ostringstream ss;
    if (isError) {
        ss << "{\"type\":\"dialog-response\",\"requestId\":\"" << EscapeJson(requestId)
           << "\",\"error\":\"" << EscapeJson(result) << "\"}";
    } else {
        ss << "{\"type\":\"dialog-response\",\"requestId\":\"" << EscapeJson(requestId)
           << "\",\"result\":\"" << EscapeJson(result) << "\"}";
    }
    SendToNode(ss.str());
}

// ---------------------------------------------------------------
// DLL check
// ---------------------------------------------------------------

static bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static std::wstring GetExeDirectory() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return buf;
}

static void CheckRequiredDlls() {
    std::wstring loader = GetExeDirectory() + L"\\WebView2Loader.dll";
    if (!FileExists(loader)) {
        // When running as child process we should not show a MessageBox
        // that blocks. Write to stderr instead.
        fprintf(stderr, "ERROR: WebView2Loader.dll not found next to the executable.\n");
        ExitProcess(1);
    }
}

// ---------------------------------------------------------------
// Folder picker (modern + fallback)
// ---------------------------------------------------------------

static std::wstring SelectFolder(HWND owner) {
    std::wstring result;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        hr = dialog->Show(owner);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
        if (!result.empty()) return result;
    }

    // Classic fallback
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH] = {};
        if (SHGetPathFromIDListW(pidl, path)) result = path;
        CoTaskMemFree(pidl);
    }
    return result;
}

// ---------------------------------------------------------------
// COM callback helpers (MinGW friendly)
// ---------------------------------------------------------------

class EnvironmentCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    LONG ref_ = 1;
    std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> cb_;
public:
    explicit EnvironmentCompletedHandler(std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> cb) : cb_(std::move(cb)) {}
    virtual ~EnvironmentCompletedHandler() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG c = InterlockedDecrement(&ref_);
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        return cb_(result, env);
    }
};

class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    LONG ref_ = 1;
    std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> cb_;
public:
    explicit ControllerCompletedHandler(std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> cb) : cb_(std::move(cb)) {}
    virtual ~ControllerCompletedHandler() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG c = InterlockedDecrement(&ref_);
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        return cb_(result, controller);
    }
};

// ---------------------------------------------------------------
// Window class
// ---------------------------------------------------------------

struct WindowOptions {
    std::string url;
    std::string title = "WebView2";
    int width = 900;
    int height = 600;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int minWidth = 0;
    int minHeight = 0;
    int maxWidth = 0;
    int maxHeight = 0;
    std::string icon;
    std::string backgroundColor = "#ffffff";
    std::string userDataFolder;
    bool resizable = true;
    bool center = true;
};

class NativeWindow {
public:
    int id = 0;
    HWND hwnd = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* webview = nullptr;
    WindowOptions options;
    bool closeInProgress = false;
    std::string pendingCloseRequestId;
    bool allowClose = true;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool Create();
    void InitWebView2();
    void Navigate(const std::wstring& url);
    void SetTitle(const std::wstring& title);
    void SetIcon(const std::wstring& iconPath);
    void SetSize(int w, int h);
    void SetMinSize(int w, int h);
    void SetMaxSize(int w, int h);
    void SetPosition(int x, int y);
    void SetBackgroundColor(const std::string& color);
    void Focus();
    void Blur();
    void Center();
    void Minimize();
    void Maximize();
    void Restore();
    void Flash();
    void Show();
    void Hide();
    void Close(bool force = false);
    void Destroy();
};

// Global map of windows
static std::map<int, std::unique_ptr<NativeWindow>> g_windows;
static std::mutex g_windowsMutex;

static NativeWindow* FindWindowById(int id) {
    std::lock_guard<std::mutex> lock(g_windowsMutex);
    auto it = g_windows.find(id);
    return (it != g_windows.end()) ? it->second.get() : nullptr;
}

LRESULT CALLBACK NativeWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NativeWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<NativeWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd = hWnd;
    } else {
        self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_SIZE:
        if (self->controller) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            self->controller->put_Bounds(rc);
        }
        {
            std::ostringstream data;
            data << "{\"width\":" << LOWORD(lParam) << ",\"height\":" << HIWORD(lParam) << "}";
            SendEvent(self->id, "resize", data.str());
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (self->options.minWidth > 0)  mmi->ptMinTrackSize.x = self->options.minWidth;
        if (self->options.minHeight > 0) mmi->ptMinTrackSize.y = self->options.minHeight;
        if (self->options.maxWidth > 0)  mmi->ptMaxTrackSize.x = self->options.maxWidth;
        if (self->options.maxHeight > 0) mmi->ptMaxTrackSize.y = self->options.maxHeight;
        return 0;
    }

    case WM_SYSCOMMAND:
        switch (wParam & 0xFFF0) {
        case SC_MINIMIZE: SendEvent(self->id, "minimize"); break;
        case SC_MAXIMIZE: SendEvent(self->id, "maximize"); break;
        case SC_RESTORE:  SendEvent(self->id, "restore");  break;
        }
        break;

    case WM_CLOSE: {
        if (self->closeInProgress) return 0;
        self->closeInProgress = true;
        self->pendingCloseRequestId = "close_" + std::to_string(GetTickCount64());
        SendCloseRequest(self->id, self->pendingCloseRequestId);

        // Wait a short time for Node.js answer (simple timeout)
        // In a more advanced version we would use a proper async wait.
        // For now we default to allow after a brief period if no answer.
        SetTimer(hWnd, 1, 3000, nullptr); // 3 second timeout
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hWnd, 1);
            // Timeout – allow close
            if (self->closeInProgress) {
                self->Destroy();
            }
        }
        break;

    case WM_DESTROY:
        SendEvent(self->id, "closed");
        {
            std::lock_guard<std::mutex> lock(g_windowsMutex);
            g_windows.erase(self->id);
        }
        if (g_windows.empty()) {
            PostQuitMessage(0);
        }
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool NativeWindow::Create() {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"WebView2HostWindow";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!options.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    int x = options.x, y = options.y;
    if (options.center || x == CW_USEDEFAULT || y == CW_USEDEFAULT) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (x == CW_USEDEFAULT || options.center) x = (screenW - options.width) / 2;
        if (y == CW_USEDEFAULT || options.center) y = (screenH - options.height) / 2;
    }

    std::wstring title = Utf8ToWide(options.title);

    hwnd = CreateWindowExW(
        0, L"WebView2HostWindow", title.c_str(),
        style,
        x, y, options.width, options.height,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!hwnd) return false;

    if (!options.icon.empty()) {
        SetIcon(Utf8ToWide(options.icon));
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}

void NativeWindow::InitWebView2() {
    auto* envHandler = new EnvironmentCompletedHandler(
        [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
                fprintf(stderr, "Failed to create WebView2 Environment\n");
                return result;
            }

            auto* ctrlHandler = new ControllerCompletedHandler(
                [this](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (FAILED(result) || !ctrl) {
                        fprintf(stderr, "Failed to create WebView2 Controller\n");
                        return result;
                    }

                    controller = ctrl;
                    controller->AddRef();
                    controller->get_CoreWebView2(&webview);
                    webview->AddRef();

                    ICoreWebView2Settings* settings = nullptr;
                    if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
                        settings->put_IsScriptEnabled(TRUE);
                        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                        settings->put_IsWebMessageEnabled(TRUE);
                        settings->Release();
                    }

                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    controller->put_Bounds(rc);

                    // Apply background color if provided (#RRGGBB)
                    // put_DefaultBackgroundColor lives on ICoreWebView2Controller2
                    if (!options.backgroundColor.empty() && options.backgroundColor[0] == '#') {
                        unsigned int r = 255, g = 255, b = 255, a = 255;
                        const char* hex = options.backgroundColor.c_str() + 1;
                        if (strlen(hex) >= 6) {
                            sscanf(hex, "%02x%02x%02x", &r, &g, &b);
                            COREWEBVIEW2_COLOR color;
                            color.A = (BYTE)a;
                            color.R = (BYTE)r;
                            color.G = (BYTE)g;
                            color.B = (BYTE)b;
                            ICoreWebView2Controller2* c2 = nullptr;
                            if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(&c2))) && c2) {
                                c2->put_DefaultBackgroundColor(color);
                                c2->Release();
                            }
                        }
                    }

                    // Navigate to the requested URL (or a blank page)
                    if (!options.url.empty()) {
                        Navigate(Utf8ToWide(options.url));
                    } else {
                        Navigate(L"about:blank");
                    }

                    SendEvent(id, "ready");
                    SendEvent(id, "start");
                    return S_OK;
                });

            env->CreateCoreWebView2Controller(hwnd, ctrlHandler);
            ctrlHandler->Release();
            return S_OK;
        });

    // userDataFolder: empty string -> default; otherwise absolute path
    LPCWSTR userData = nullptr;
    std::wstring userDataW;
    if (!options.userDataFolder.empty()) {
        userDataW = Utf8ToWide(options.userDataFolder);
        userData = userDataW.c_str();
    }

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, userData, nullptr, envHandler);
    envHandler->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "CreateCoreWebView2EnvironmentWithOptions failed (hr=0x%08lx)\n", (unsigned long)hr);
    }
}

void NativeWindow::Navigate(const std::wstring& url) {
    if (webview) webview->Navigate(url.c_str());
}

void NativeWindow::SetTitle(const std::wstring& title) {
    if (hwnd) SetWindowTextW(hwnd, title.c_str());
}

void NativeWindow::SetIcon(const std::wstring& iconPath) {
    if (!hwnd) return;
    HICON hIcon = (HICON)LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 0, 0,
                                    LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (hIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
}

void NativeWindow::SetSize(int w, int h) {
    if (!hwnd) return;
    options.width = w;
    options.height = h;
    SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
}

void NativeWindow::SetMinSize(int w, int h) {
    options.minWidth = w;
    options.minHeight = h;
}

void NativeWindow::SetMaxSize(int w, int h) {
    options.maxWidth = w;
    options.maxHeight = h;
}

void NativeWindow::SetPosition(int x, int y) {
    if (!hwnd) return;
    options.x = x;
    options.y = y;
    options.center = false;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void NativeWindow::SetBackgroundColor(const std::string& color) {
    options.backgroundColor = color;
    if (!controller || color.empty() || color[0] != '#') return;
    unsigned int r = 255, g = 255, b = 255, a = 255;
    const char* hex = color.c_str() + 1;
    if (strlen(hex) >= 6) {
        sscanf(hex, "%02x%02x%02x", &r, &g, &b);
        COREWEBVIEW2_COLOR c;
        c.A = (BYTE)a; c.R = (BYTE)r; c.G = (BYTE)g; c.B = (BYTE)b;
        ICoreWebView2Controller2* c2 = nullptr;
        if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(&c2))) && c2) {
            c2->put_DefaultBackgroundColor(c);
            c2->Release();
        }
    }
}

void NativeWindow::Focus() {
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
}

void NativeWindow::Blur() {
    // No direct blur API; leave focus to another window if needed
}

void NativeWindow::Center() {
    if (!hwnd) return;
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    options.x = x;
    options.y = y;
}

void NativeWindow::Minimize() {
    if (hwnd) ShowWindow(hwnd, SW_MINIMIZE);
}

void NativeWindow::Maximize() {
    if (hwnd) ShowWindow(hwnd, SW_MAXIMIZE);
}

void NativeWindow::Restore() {
    if (hwnd) ShowWindow(hwnd, SW_RESTORE);
}

void NativeWindow::Flash() {
    if (!hwnd) return;
    FLASHWINFO fi = {};
    fi.cbSize = sizeof(fi);
    fi.hwnd = hwnd;
    fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    fi.uCount = 5;
    fi.dwTimeout = 0;
    FlashWindowEx(&fi);
}

void NativeWindow::Show() {
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    }
}

void NativeWindow::Hide() {
    if (hwnd) ShowWindow(hwnd, SW_HIDE);
}

void NativeWindow::Close(bool force) {
    if (force) {
        Destroy();
    } else if (hwnd) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void NativeWindow::Destroy() {
    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
}

// ---------------------------------------------------------------
// Handle incoming messages from Node.js
// ---------------------------------------------------------------

static void HandleMessage(const std::string& json) {
    std::string type = JsonGetString(json, "type");
    int windowId = JsonGetInt(json, "windowId");

    if (type == "create-window") {
        auto win = std::make_unique<NativeWindow>();
        win->id = windowId;

        // Parse options (very basic extraction)
        win->options.url = JsonGetString(json, "url");
        // The options are nested; for simplicity we also look at top-level keys
        // that Node.js may flatten, or we extract from the "options" object string.
        // A more robust parser would be better later.
        std::string title = JsonGetString(json, "title");
        if (!title.empty()) win->options.title = title;
        int w = JsonGetInt(json, "width", 900);
        int h = JsonGetInt(json, "height", 600);
        win->options.width = w;
        win->options.height = h;
        win->options.minWidth = JsonGetInt(json, "minWidth");
        win->options.minHeight = JsonGetInt(json, "minHeight");
        win->options.icon = JsonGetString(json, "icon");
        win->options.center = JsonGetBool(json, "center", true);
        win->options.resizable = JsonGetBool(json, "resizable", true);

        // Also try to pull from nested "options"
        // (Node sends the whole options object)
        size_t optPos = json.find("\"options\"");
        if (optPos != std::string::npos) {
            std::string optPart = json.substr(optPos);
            std::string u = JsonGetString(optPart, "url");
            if (!u.empty()) win->options.url = u;
            std::string t = JsonGetString(optPart, "title");
            if (!t.empty()) win->options.title = t;
            int ow = JsonGetInt(optPart, "width", 0);
            if (ow > 0) win->options.width = ow;
            int oh = JsonGetInt(optPart, "height", 0);
            if (oh > 0) win->options.height = oh;
            win->options.minWidth = JsonGetInt(optPart, "minWidth", win->options.minWidth);
            win->options.minHeight = JsonGetInt(optPart, "minHeight", win->options.minHeight);
            win->options.maxWidth = JsonGetInt(optPart, "maxWidth", win->options.maxWidth);
            win->options.maxHeight = JsonGetInt(optPart, "maxHeight", win->options.maxHeight);
            int ox = JsonGetInt(optPart, "x", INT_MIN);
            int oy = JsonGetInt(optPart, "y", INT_MIN);
            if (ox != INT_MIN) { win->options.x = ox; win->options.center = false; }
            if (oy != INT_MIN) { win->options.y = oy; win->options.center = false; }
            std::string ic = JsonGetString(optPart, "icon");
            if (!ic.empty()) win->options.icon = ic;
            std::string bg = JsonGetString(optPart, "backgroundColor");
            if (!bg.empty()) win->options.backgroundColor = bg;
            std::string ud = JsonGetString(optPart, "userDataFolder");
            if (!ud.empty()) win->options.userDataFolder = ud;
            win->options.center = JsonGetBool(optPart, "center", win->options.center);
            win->options.resizable = JsonGetBool(optPart, "resizable", win->options.resizable);
        }

        if (!win->Create()) {
            fprintf(stderr, "Failed to create window %d\n", windowId);
            return;
        }

        NativeWindow* raw = win.get();
        {
            std::lock_guard<std::mutex> lock(g_windowsMutex);
            g_windows[windowId] = std::move(win);
        }
        raw->InitWebView2();
        return;
    }

    if (type == "close-response") {
        NativeWindow* win = FindWindowById(windowId);
        if (!win) return;
        bool allow = JsonGetBool(json, "allow", true);
        std::string reqId = JsonGetString(json, "requestId");
        if (win->closeInProgress && (reqId.empty() || reqId == win->pendingCloseRequestId)) {
            KillTimer(win->hwnd, 1);
            if (allow) {
                win->Destroy();
            } else {
                win->closeInProgress = false;
                win->pendingCloseRequestId.clear();
            }
        }
        return;
    }

    if (type == "call") {
        NativeWindow* win = FindWindowById(windowId);
        if (!win) return;
        std::string method = JsonGetString(json, "method");

        if (method == "navigate") {
            // Extract first string argument roughly
            size_t argsPos = json.find("\"args\"");
            if (argsPos != std::string::npos) {
                std::string argsPart = json.substr(argsPos);
                std::string url = JsonGetString(argsPart, "0"); // not perfect, but works for simple cases
                // Better: look for the first string after [
                size_t bracket = argsPart.find('[');
                if (bracket != std::string::npos) {
                    size_t q1 = argsPart.find('"', bracket);
                    if (q1 != std::string::npos) {
                        size_t q2 = q1 + 1;
                        while (q2 < argsPart.size()) {
                            if (argsPart[q2] == '\\') { q2 += 2; continue; }
                            if (argsPart[q2] == '"') break;
                            q2++;
                        }
                        url = argsPart.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
                if (!url.empty()) win->Navigate(Utf8ToWide(url));
            }
        }
        else if (method == "close") {
            bool force = false;
            // crude check
            if (json.find("true") != std::string::npos) force = true;
            win->Close(force);
        }
        else if (method == "minimize") win->Minimize();
        else if (method == "maximize") win->Maximize();
        else if (method == "restore")  win->Restore();
        else if (method == "flash")    win->Flash();
        else if (method == "show")     win->Show();
        else if (method == "hide")     win->Hide();
        else if (method == "focus")    win->Focus();
        else if (method == "blur")     win->Blur();
        else if (method == "center")   win->Center();
        else if (method == "setSize" || method == "setMinSize" || method == "setMaxSize" || method == "setPosition") {
            // Extract two numeric args from args array
            size_t argsPos = json.find("\"args\"");
            int a1 = 0, a2 = 0;
            if (argsPos != std::string::npos) {
                std::string argsPart = json.substr(argsPos);
                size_t bracket = argsPart.find('[');
                if (bracket != std::string::npos) {
                    sscanf(argsPart.c_str() + bracket, "[%d,%d", &a1, &a2);
                }
            }
            if (method == "setSize") win->SetSize(a1, a2);
            else if (method == "setMinSize") win->SetMinSize(a1, a2);
            else if (method == "setMaxSize") win->SetMaxSize(a1, a2);
            else if (method == "setPosition") win->SetPosition(a1, a2);
        }
        else if (method == "setBackgroundColor") {
            size_t argsPos = json.find("\"args\"");
            if (argsPos != std::string::npos) {
                std::string argsPart = json.substr(argsPos);
                size_t bracket = argsPart.find('[');
                if (bracket != std::string::npos) {
                    size_t q1 = argsPart.find('"', bracket);
                    if (q1 != std::string::npos) {
                        size_t q2 = q1 + 1;
                        while (q2 < argsPart.size()) {
                            if (argsPart[q2] == '\\') { q2 += 2; continue; }
                            if (argsPart[q2] == '"') break;
                            q2++;
                        }
                        std::string color = argsPart.substr(q1 + 1, q2 - q1 - 1);
                        win->SetBackgroundColor(color);
                    }
                }
            }
        }
        else if (method == "setTitle") {
            size_t argsPos = json.find("\"args\"");
            if (argsPos != std::string::npos) {
                std::string argsPart = json.substr(argsPos);
                size_t q1 = argsPart.find('"');
                if (q1 != std::string::npos) {
                    q1 = argsPart.find('"', q1 + 1); // skip "args"
                    q1 = argsPart.find('"', q1 + 1);
                    if (q1 != std::string::npos) {
                        size_t q2 = q1 + 1;
                        while (q2 < argsPart.size() && argsPart[q2] != '"') q2++;
                        std::string title = argsPart.substr(q1 + 1, q2 - q1 - 1);
                        win->SetTitle(Utf8ToWide(title));
                    }
                }
            }
        }
        else if (method == "setIcon") {
            // similar extraction...
            size_t argsPos = json.find("\"args\"");
            if (argsPos != std::string::npos) {
                std::string argsPart = json.substr(argsPos);
                size_t bracket = argsPart.find('[');
                if (bracket != std::string::npos) {
                    size_t q1 = argsPart.find('"', bracket);
                    if (q1 != std::string::npos) {
                        size_t q2 = q1 + 1;
                        while (q2 < argsPart.size()) {
                            if (argsPart[q2] == '\\') { q2 += 2; continue; }
                            if (argsPart[q2] == '"') break;
                            q2++;
                        }
                        std::string icon = argsPart.substr(q1 + 1, q2 - q1 - 1);
                        win->SetIcon(Utf8ToWide(icon));
                    }
                }
            }
        }
        return;
    }

    if (type == "dialog") {
        NativeWindow* win = FindWindowById(windowId);
        std::string requestId = JsonGetString(json, "requestId");
        std::string name = JsonGetString(json, "name");

        if (name == "selectFolder") {
            HWND owner = win ? win->hwnd : nullptr;
            std::wstring folder = SelectFolder(owner);
            SendDialogResponse(requestId, WideToUtf8(folder));
        }
        else if (name == "info" || name == "error" || name == "warning" || name == "confirm") {
            // Prefer top-level message/title (sent by fixed JS), fall back to args
            std::string message = JsonGetString(json, "message");
            std::string title = JsonGetString(json, "title");
            if (message.empty()) {
                // try first string in args
                size_t argsPos = json.find("\"args\"");
                if (argsPos != std::string::npos) {
                    std::string argsPart = json.substr(argsPos);
                    size_t bracket = argsPart.find('[');
                    if (bracket != std::string::npos) {
                        size_t q1 = argsPart.find('"', bracket);
                        if (q1 != std::string::npos) {
                            size_t q2 = q1 + 1;
                            while (q2 < argsPart.size()) {
                                if (argsPart[q2] == '\\') { q2 += 2; continue; }
                                if (argsPart[q2] == '"') break;
                                q2++;
                            }
                            message = argsPart.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                }
            }
            if (title.empty()) title = "Dialog";

            UINT flags = MB_OK;
            if (name == "error") flags |= MB_ICONERROR;
            else if (name == "warning") flags |= MB_ICONWARNING;
            else if (name == "confirm") flags = MB_YESNO | MB_ICONQUESTION;
            else flags |= MB_ICONINFORMATION;

            int result = MessageBoxW(win ? win->hwnd : nullptr,
                                     Utf8ToWide(message).c_str(),
                                     Utf8ToWide(title).c_str(), flags);
            if (name == "confirm") {
                SendDialogResponse(requestId, (result == IDYES) ? "true" : "false");
            } else {
                SendDialogResponse(requestId, "ok");
            }
        }
        return;
    }
}

// ---------------------------------------------------------------
// Stdin reader thread
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// UI-thread dispatch
//
// Win32 windows and WebView2 MUST be created/used on the same thread
// that runs the message loop. The stdin reader only enqueues JSON and
// wakes the main thread via PostThreadMessage.
// ---------------------------------------------------------------

static DWORD g_mainThreadId = 0;
static std::mutex g_ipcQueueMutex;
static std::vector<std::string> g_ipcQueue;

#define WM_IPC_MESSAGE (WM_APP + 1)

static void EnqueueIpcMessage(std::string json) {
    {
        std::lock_guard<std::mutex> lock(g_ipcQueueMutex);
        g_ipcQueue.push_back(std::move(json));
    }
    if (g_mainThreadId != 0) {
        PostThreadMessageW(g_mainThreadId, WM_IPC_MESSAGE, 0, 0);
    }
}

static void DrainIpcQueue() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(g_ipcQueueMutex);
        batch.swap(g_ipcQueue);
    }
    for (const auto& json : batch) {
        HandleMessage(json);
    }
}

static void StdinReaderThread() {
    std::vector<char> buffer;
    buffer.reserve(65536);

    while (true) {
        uint32_t len = 0;
        size_t got = fread(&len, 1, 4, stdin);
        if (got != 4) break; // EOF or error

        if (len > 10 * 1024 * 1024) {
            fprintf(stderr, "Message too large: %u\n", len);
            break;
        }

        buffer.resize(len);
        got = fread(buffer.data(), 1, len, stdin);
        if (got != len) break;

        EnqueueIpcMessage(std::string(buffer.data(), len));
    }

    // stdin closed → ask the MAIN thread to quit (PostQuitMessage only
    // affects the calling thread's queue, so we must not call it here).
    if (g_mainThreadId != 0) {
        PostThreadMessageW(g_mainThreadId, WM_QUIT, 0, 0);
    }
}

// ---------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------

int main() {
    // Hide the black console window.
    // Stay as a console-subsystem process so stdin/stdout pipes work with Node.
    if (HWND console = GetConsoleWindow()) {
        ShowWindow(console, SW_HIDE);
    }

    CheckRequiredDlls();

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        fprintf(stderr, "CoInitializeEx failed\n");
        return 1;
    }

    g_mainThreadId = GetCurrentThreadId();

    // Start stdin reader AFTER we know the main thread id
    std::thread reader(StdinReaderThread);
    reader.detach();

    // Message loop – also handles WM_IPC_MESSAGE from the stdin thread.
    // Thread messages have hwnd == NULL.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_IPC_MESSAGE) {
            DrainIpcQueue();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Process any last IPC messages that arrived with WM_QUIT
    DrainIpcQueue();

    CoUninitialize();
    return 0;
}
