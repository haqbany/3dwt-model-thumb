// ModelThumbLite - provider implementation.
// Architecture mirrors NCrewsShellExtensions (IThumbnailProvider over a
// file/stream/item-initialized model path); all code below is original.
#include "Provider.h"
#include "ThumbGuid.h"
#include "Loaders.h"
#include "Raster.h"
#include <shlwapi.h>
#include <cwctype>
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")

namespace {
void LogFail(const wchar_t* what) {
    // Best-effort one-line diagnostics; never throws, never blocks.
    try {
        wchar_t tmp[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring log = std::wstring(tmp) + L"ModelThumbLite.log";
        HANDLE h = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            SYSTEMTIME st; GetLocalTime(&st);
            wchar_t line[512];
            int n = swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u %s\r\n",
                               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, what);
            DWORD w = 0;
            if (n > 0) WriteFile(h, line, (DWORD)n * sizeof(wchar_t), &w, nullptr);
            // keep log small: truncate past 64KB
            LARGE_INTEGER sz{};
            if (GetFileSizeEx(h, &sz) && sz.QuadPart > 65536) {
                SetFilePointer(h, 0, nullptr, FILE_BEGIN);
                SetEndOfFile(h);
            }
            CloseHandle(h);
        }
    } catch (...) {}
}
} // namespace

CThumbProvider::CThumbProvider() { DllAddRef(); }
CThumbProvider::~CThumbProvider() { DllRelease(); }

STDMETHODIMP CThumbProvider::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IThumbnailProvider))
        *ppv = static_cast<IThumbnailProvider*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithFile))
        *ppv = static_cast<IInitializeWithFile*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithStream))
        *ppv = static_cast<IInitializeWithStream*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithItem))
        *ppv = static_cast<IInitializeWithItem*>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) CThumbProvider::AddRef() { return InterlockedIncrement(&m_ref); }
STDMETHODIMP_(ULONG) CThumbProvider::Release() {
    LONG r = InterlockedDecrement(&m_ref);
    if (!r) delete this;
    return r;
}

STDMETHODIMP CThumbProvider::Initialize(LPCWSTR pszFilePath, DWORD) {
    if (!pszFilePath) return E_INVALIDARG;
    try { m_path = pszFilePath; } catch (...) { return E_FAIL; }
    return S_OK;
}

STDMETHODIMP CThumbProvider::Initialize(IShellItem* psi, DWORD) {
    if (!psi) return E_INVALIDARG;
    LPWSTR name = nullptr;
    HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &name);
    if (SUCCEEDED(hr) && name) {
        try { m_path = name; } catch (...) { hr = E_FAIL; }
        CoTaskMemFree(name);
    }
    return hr;
}

STDMETHODIMP CThumbProvider::Initialize(IStream* pstream, DWORD) {
    if (!pstream) return E_INVALIDARG;
    STATSTG st{};
    if (FAILED(pstream->Read(nullptr, 0, nullptr))) {
        // some streams reject zero reads; fall through to Stat
    }
    if (FAILED(pstream->Stat(&st, STATFLAG_NONAME)) || st.cbSize.QuadPart == 0)
        return E_FAIL;
    ULONGLONG total = st.cbSize.QuadPart;
    if (total > 512ULL * 1024 * 1024) return E_FAIL; // sanity cap
    std::vector<char> buf;
    try { buf.resize((size_t)total); } catch (...) { return E_FAIL; }
    ULONG got = 0;
    LARGE_INTEGER zero{};
    pstream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(pstream->Read(buf.data(), (ULONG)total, &got)) || !got) return E_FAIL;
    std::wstring ext = SniffModelExtension(buf.data(), got);
    if (ext.empty()) return E_FAIL;
    wchar_t tmp[MAX_PATH] = {0};
    if (!GetTempFileNameW(std::filesystem::temp_directory_path().wstring().c_str(), L"mtl", 0, tmp))
        return E_FAIL;
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    std::filesystem::path p = tmp;
    p.replace_extension(ext);
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"wb") != 0 || !f) return E_FAIL;
    bool wok = fwrite(buf.data(), 1, got, f) == got;
    fclose(f);
    if (!wok) { std::filesystem::remove(p, ec); return E_FAIL; }
    m_path = p.wstring();
    return S_OK;
}

STDMETHODIMP CThumbProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    if (!phbmp) return E_INVALIDARG;
    *phbmp = nullptr;
    if (pdwAlpha) *pdwAlpha = WTSAT_ARGB;
    if (cx == 0) return E_INVALIDARG;
    bool isTemp = false;
    try {
        if (m_path.empty()) return E_FAIL;
        LiteMesh mesh; LitePoints cloud;
        if (!LoadModelFile(m_path, mesh, cloud) || (mesh.empty() && cloud.empty())) {
            LogFail(L"load failed");
            return E_FAIL;
        }
        // badge label: extension without dot, uppercased ("STL", "3MF", ...)
        wchar_t label[16] = {0};
        {
            size_t dot = m_path.find_last_of(L'.');
            size_t sep = m_path.find_last_of(L"\\/");
            if (dot != std::wstring::npos && (sep == std::wstring::npos || sep < dot)) {
                std::wstring ext = m_path.substr(dot + 1, 14);
                for (size_t i = 0; i < ext.size() && i < 14; i++)
                    label[i] = (wchar_t)towupper(ext[i]);
            }
        }
        HBITMAP hb = !mesh.empty() ? RenderMeshThumb(mesh, cx, label[0] ? label : nullptr)
                                   : RenderCloudThumb(cloud, cx, label[0] ? label : nullptr);
        if (!hb) { LogFail(L"render failed"); return E_FAIL; }
        *phbmp = hb;
        // clean up stream-sourced temp file
        std::wstring tmpDir = std::filesystem::temp_directory_path().wstring();
        if (m_path.size() > tmpDir.size() &&
            _wcsnicmp(m_path.c_str(), tmpDir.c_str(), tmpDir.size()) == 0)
            isTemp = true;
        if (isTemp) {
            std::error_code ec;
            std::filesystem::remove(m_path, ec);
        }
        return S_OK;
    } catch (...) {
        LogFail(L"exception");
        return E_FAIL;
    }
}

// ---------------- one-click preview handler ----------------
namespace {
LONG g_previewWndInit = 0;
constexpr wchar_t kPreviewWndClass[] = L"ModelThumbLitePreview";
} // namespace

CPreviewHandler::CPreviewHandler() { DllAddRef(); }
CPreviewHandler::~CPreviewHandler() { Unload(); DllRelease(); }

STDMETHODIMP CPreviewHandler::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPreviewHandler))
        *ppv = static_cast<IPreviewHandler*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithFile))
        *ppv = static_cast<IInitializeWithFile*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithStream))
        *ppv = static_cast<IInitializeWithStream*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithItem))
        *ppv = static_cast<IInitializeWithItem*>(this);
    else if (IsEqualIID(riid, IID_IOleWindow))
        *ppv = static_cast<IOleWindow*>(this);
    else if (IsEqualIID(riid, IID_IPreviewHandlerVisuals))
        *ppv = static_cast<IPreviewHandlerVisuals*>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) CPreviewHandler::AddRef() { return InterlockedIncrement(&m_ref); }
STDMETHODIMP_(ULONG) CPreviewHandler::Release() {
    LONG r = InterlockedDecrement(&m_ref);
    if (!r) delete this;
    return r;
}

STDMETHODIMP CPreviewHandler::Initialize(LPCWSTR pszFilePath, DWORD) {
    if (!pszFilePath) return E_INVALIDARG;
    try { m_path = pszFilePath; } catch (...) { return E_FAIL; }
    return S_OK;
}
STDMETHODIMP CPreviewHandler::Initialize(IShellItem* psi, DWORD) {
    if (!psi) return E_INVALIDARG;
    LPWSTR name = nullptr;
    HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &name);
    if (SUCCEEDED(hr) && name) {
        try { m_path = name; } catch (...) { hr = E_FAIL; }
        CoTaskMemFree(name);
    }
    return hr;
}
STDMETHODIMP CPreviewHandler::Initialize(IStream* pstream, DWORD) {
    if (!pstream) return E_INVALIDARG;
    STATSTG st{};
    if (FAILED(pstream->Stat(&st, STATFLAG_NONAME)) || st.cbSize.QuadPart == 0)
        return E_FAIL;
    ULONGLONG total = st.cbSize.QuadPart;
    if (total > 512ULL * 1024 * 1024) return E_FAIL;
    std::vector<char> buf;
    try { buf.resize((size_t)total); } catch (...) { return E_FAIL; }
    ULONG got = 0;
    LARGE_INTEGER zero{};
    pstream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(pstream->Read(buf.data(), (ULONG)total, &got)) || !got) return E_FAIL;
    std::wstring ext = SniffModelExtension(buf.data(), got);
    if (ext.empty()) return E_FAIL;
    wchar_t tmp[MAX_PATH] = {0};
    if (!GetTempFileNameW(std::filesystem::temp_directory_path().wstring().c_str(), L"mtl", 0, tmp))
        return E_FAIL;
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    std::filesystem::path p = tmp;
    p.replace_extension(ext);
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"wb") != 0 || !f) return E_FAIL;
    bool wok = fwrite(buf.data(), 1, got, f) == got;
    fclose(f);
    if (!wok) { std::filesystem::remove(p, ec); return E_FAIL; }
    m_path = p.wstring();
    return S_OK;
}

STDMETHODIMP CPreviewHandler::SetWindow(HWND hwnd, const RECT* prc) {
    if (!hwnd || !prc) return E_INVALIDARG;
    m_parent = hwnd;
    return SetRect(prc);
}
STDMETHODIMP CPreviewHandler::SetRect(const RECT* prc) {
    if (!prc) return E_INVALIDARG;
    m_rc = *prc;
    if (m_view) {
        MoveWindow(m_view, m_rc.left, m_rc.top,
                   m_rc.right - m_rc.left, m_rc.bottom - m_rc.top, TRUE);
    }
    return S_OK;
}
STDMETHODIMP CPreviewHandler::SetFocus() {
    if (m_view) ::SetFocus(m_view);
    return S_OK;
}
STDMETHODIMP CPreviewHandler::QueryFocus(HWND* phwnd) {
    if (!phwnd) return E_INVALIDARG;
    *phwnd = m_view ? m_view : m_parent;
    return S_OK;
}
STDMETHODIMP CPreviewHandler::GetWindow(HWND* phwnd) {
    if (!phwnd) return E_INVALIDARG;
    *phwnd = m_parent;
    return S_OK;
}

LRESULT CALLBACK CPreviewHandler::PaintWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CPreviewHandler* self = (CPreviewHandler*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_PAINT && self && self->m_bmp) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, self->m_bmp);
        BITMAP bm{};
        GetObjectW(self->m_bmp, sizeof(bm), &bm);
        int bw = bm.bmWidth, bh = abs(bm.bmHeight);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        float s = (float)cw / (bw ? bw : 1);
        float s2 = (float)ch / (bh ? bh : 1);
        if (s2 < s) s = s2;
        int dw = (int)(bw * s), dh = (int)(bh * s);
        int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
        HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
        FillRect(hdc, &rc, white);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, dx, dy, dw, dh, mem, 0, 0, bw, bh, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

STDMETHODIMP CPreviewHandler::DoPreview() {
    if (!m_parent || m_path.empty()) return E_FAIL;
    HWND savedParent = m_parent;
    RECT savedRect = m_rc;
    if (InterlockedCompareExchange(&g_previewWndInit, 1, 0) == 0) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &CPreviewHandler::PaintWndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = kPreviewWndClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }
    Unload();
    // Unload clears m_parent; DoPreview continues with a live parent, so restore it.
    m_parent = savedParent;
    m_rc = savedRect;
    LiteMesh mesh; LitePoints cloud;
    wchar_t label[16] = {0};
    try {
        if (!LoadModelFile(m_path, mesh, cloud) || (mesh.empty() && cloud.empty()))
            return E_FAIL;
        size_t dot = m_path.find_last_of(L'.');
        size_t sep = m_path.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (sep == std::wstring::npos || sep < dot)) {
            std::wstring ext = m_path.substr(dot + 1, 14);
            for (size_t i = 0; i < ext.size() && i < 14; i++)
                label[i] = (wchar_t)towupper(ext[i]);
        }
    } catch (...) { return E_FAIL; }
    int w = m_rc.right - m_rc.left, h = m_rc.bottom - m_rc.top;
    if (w < 1 || h < 1) return E_FAIL;
    UINT size = (UINT)(w > h ? w : h);
    if (size > 1024) size = 1024;
    HBITMAP hb = !mesh.empty() ? RenderMeshThumb(mesh, size, label[0] ? label : nullptr)
                               : RenderCloudThumb(cloud, size, label[0] ? label : nullptr);
    if (!hb) return E_FAIL;
    m_bmp = hb;
    m_view = CreateWindowExW(0, kPreviewWndClass, L"", WS_CHILD | WS_VISIBLE,
                             m_rc.left, m_rc.top, w, h, m_parent, nullptr, g_hInst, nullptr);
    if (!m_view) { DeleteObject(m_bmp); m_bmp = nullptr; return E_FAIL; }
    SetWindowLongPtrW(m_view, GWLP_USERDATA, (LONG_PTR)this);
    ShowWindow(m_view, SW_SHOW);
    InvalidateRect(m_view, nullptr, TRUE);
    return S_OK;
}

STDMETHODIMP CPreviewHandler::Unload() {
    if (m_view) { DestroyWindow(m_view); m_view = nullptr; }
    if (m_bmp) { DeleteObject(m_bmp); m_bmp = nullptr; }
    m_parent = nullptr;
    return S_OK;
}
// ---------------- class factory ----------------
CClassFactory::CClassFactory(REFCLSID clsid) : m_clsid(clsid) { DllAddRef(); }
CClassFactory::~CClassFactory() { DllRelease(); }
STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) CClassFactory::AddRef() { return InterlockedIncrement(&m_ref); }
STDMETHODIMP_(ULONG) CClassFactory::Release() {
    LONG r = InterlockedDecrement(&m_ref);
    if (!r) delete this;
    return r;
}
STDMETHODIMP CClassFactory::CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppv) {
    if (punkOuter) return CLASS_E_NOAGGREGATION;
    if (IsEqualCLSID(m_clsid, CLSID_ModelPreviewLite)) {
        CPreviewHandler* v = new (std::nothrow) CPreviewHandler();
        if (!v) return E_OUTOFMEMORY;
        HRESULT hr = v->QueryInterface(riid, ppv);
        v->Release();
        return hr;
    }
    CThumbProvider* p = new (std::nothrow) CThumbProvider();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}
STDMETHODIMP CClassFactory::LockServer(BOOL fLock) {
    if (fLock) DllAddRef(); else DllRelease();
    return S_OK;
}
