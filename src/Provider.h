// ModelThumbLite - COM provider declaration.
#pragma once
#include <windows.h>
#include <thumbcache.h>  // IThumbnailProvider
#include <shlobj.h>      // IInitializeWithFile / Stream / Item
#include <shobjidl.h>    // IPreviewHandler
#include "ThumbGuid.h"
#include <string>

class CThumbProvider : public IThumbnailProvider,
                       public IInitializeWithFile,
                       public IInitializeWithStream,
                       public IInitializeWithItem {
public:
    CThumbProvider();
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;
    // IInitializeWithFile / Stream / Item
    STDMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode) override;
    STDMETHODIMP Initialize(IStream* pstream, DWORD grfMode) override;
    STDMETHODIMP Initialize(IShellItem* psi, DWORD grfMode) override;
private:
    ~CThumbProvider();
    LONG m_ref = 1;
    std::wstring m_path;
};

class CClassFactory : public IClassFactory {
public:
    explicit CClassFactory(REFCLSID clsid);
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;
private:
    ~CClassFactory();
    LONG m_ref = 1;
    CLSID m_clsid = CLSID_ModelThumbLite;
};

void DllAddRef();
void DllRelease();
extern HINSTANCE g_hInst;
extern LONG g_dllRefs;

// One-click preview-pane handler: hosts our HBITMAP in the Explorer preview pane.
class CPreviewHandler : public IPreviewHandler,
                        public IOleWindow,
                        public IPreviewHandlerVisuals,
                        public IInitializeWithFile,
                        public IInitializeWithStream,
                        public IInitializeWithItem {
public:
    CPreviewHandler();
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IOleWindow
    STDMETHODIMP GetWindow(HWND* phwnd) override;
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return S_OK; }
    // IPreviewHandler
    STDMETHODIMP SetWindow(HWND hwnd, const RECT* prc) override;
    STDMETHODIMP SetRect(const RECT* prc) override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP QueryFocus(HWND* phwnd) override;
    STDMETHODIMP TranslateAccelerator(MSG*) override { return S_FALSE; }
    STDMETHODIMP DoPreview() override;
    STDMETHODIMP Unload() override;
    // IPreviewHandlerVisuals
    STDMETHODIMP SetBackgroundColor(COLORREF) override { return S_OK; }
    STDMETHODIMP SetTextColor(COLORREF) override { return S_OK; }
    STDMETHODIMP SetFont(const LOGFONTW*) override { return S_OK; }
    // init
    STDMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode) override;
    STDMETHODIMP Initialize(IStream* pstream, DWORD grfMode) override;
    STDMETHODIMP Initialize(IShellItem* psi, DWORD grfMode) override;
private:
    ~CPreviewHandler();
    static LRESULT CALLBACK PaintWndProc(HWND, UINT, WPARAM, LPARAM);
    LONG m_ref = 1;
    std::wstring m_path;
    HWND m_parent = nullptr;
    HWND m_view = nullptr;
    RECT m_rc{};
    HBITMAP m_bmp = nullptr;
};
