// ModelThumbLite - preview-handler test: real parent window + SetWindow +
// DoPreview, message pump, then read back pixels (must be non-blank).
// Usage: prevtest.exe <model> <out.bmp> [size]
#include <windows.h>
#include <shobjidl.h>
#include <cstdio>
#include <vector>
// {D9C5E3F2-6A4B-4C8D-9E2F-1A3B5C7D9E01}
static const CLSID CLSID_Prev =
    {0xD9C5E3F2, 0x6A4B, 0x4C8D, {0x9E, 0x2F, 0x1A, 0x3B, 0x5C, 0x7D, 0x9E, 0x01}};

static bool SaveWindow(HWND hwnd, const wchar_t* out, bool& anyInk) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC hdc = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(hdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, hb);
    BitBlt(mem, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    ReleaseDC(hwnd, hdc);
    uint32_t* px = (uint32_t*)bits;
    size_t ink = 0;
    for (int i = 0; i < w * h; i++) {
        uint32_t p = px[i] & 0xFFFFFF;
        if (p != 0xFFFFFF) ink++;
    }
    anyInk = ink > (size_t)(w * h / 200); // >0.5% non-white
    fwprintf(stderr, L"window %dx%d ink=%llu\n", w, h, (unsigned long long)ink);
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1;
    ih.biBitCount = 32; ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)w * h * 4;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = nullptr;
    bool ok = false;
    if (_wfopen_s(&f, out, L"wb") == 0 && f) {
        fwrite(&fh, 1, sizeof(fh), f);
        fwrite(&ih, 1, sizeof(ih), f);
        std::vector<uint32_t> row((size_t)w);
        for (int y = h - 1; y >= 0; y--) {
            memcpy(row.data(), px + (size_t)y * w, (size_t)w * 4);
            if (fwrite(row.data(), 4, (size_t)w, f) != (size_t)w) break;
        }
        ok = !ferror(f);
        fclose(f);
    }
    DeleteObject(hb);
    DeleteDC(mem);
    return ok;
}

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { fwprintf(stderr, L"usage: prevtest <model> <out.bmp> [size]\n"); return 2; }
    int size = argc > 3 ? _wtoi(argv[3]) : 400;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    WNDCLASSW wc{};
    wc.lpfnWndProc = HostProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PrevTestHost";
    RegisterClassW(&wc);
    HWND host = CreateWindowExW(0, L"PrevTestHost", L"t", WS_OVERLAPPEDWINDOW,
                                0, 0, size, size, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    int rc = 1;
    IPreviewHandler* pv = nullptr;
    hr = CoCreateInstance(CLSID_Prev, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pv));
    if (FAILED(hr)) { fwprintf(stderr, L"CoCreateInstance failed %08lx\n", hr); }
    else {
        fwprintf(stderr, L"preview COM activated.\n");
        IInitializeWithFile* init = nullptr;
        if (SUCCEEDED(pv->QueryInterface(IID_PPV_ARGS(&init)))) {
            hr = init->Initialize(argv[1], 0);
            init->Release();
        } else hr = E_FAIL;
        if (FAILED(hr)) fwprintf(stderr, L"Initialize failed %08lx\n", hr);
        else {
            RECT r{0, 0, size, size};
            hr = pv->SetWindow(host, &r);
            if (FAILED(hr)) fwprintf(stderr, L"SetWindow failed %08lx\n", hr);
            else {
                DWORD t0 = GetTickCount();
                hr = pv->DoPreview();
                fwprintf(stderr, L"DoPreview hr=%08lx dt=%lums\n", hr, GetTickCount() - t0);
                if (SUCCEEDED(hr)) {
                    MSG msg;
                    DWORD end = GetTickCount() + 1500;
                    while (GetTickCount() < end) {
                        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&msg);
                            DispatchMessageW(&msg);
                        }
                        Sleep(50);
                    }
                    bool ink = false;
                    rc = SaveWindow(host, argv[2], ink) && ink ? 0 : 1;
                    fwprintf(stderr, rc ? L"BLANK/WRITE FAIL\n" : L"WROTE %s (has content)\n", argv[2]);
                }
            }
        }
        pv->Release();
    }
    DestroyWindow(host);
    CoUninitialize();
    return rc;
}
