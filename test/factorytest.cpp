// ModelThumbLite - cache-path test: IShellItemImageFactory::GetImage,
// the same API Explorer views use (assoc -> handler -> thumb cache).
// Usage: factorytest.exe <model> <out.bmp> [size]
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <cstdio>
#include <vector>

static bool SaveBmp(HBITMAP hb, const wchar_t* out) {
    BITMAP bm{};
    if (!GetObjectW(hb, sizeof(bm), &bm) || !bm.bmBits) return false;
    int w = bm.bmWidth, h = abs(bm.bmHeight);
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1;
    ih.biBitCount = 32; ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)w * h * 4;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = nullptr;
    if (_wfopen_s(&f, out, L"wb") || !f) return false;
    fwrite(&fh, 1, sizeof(fh), f);
    fwrite(&ih, 1, sizeof(ih), f);
    // GetImage may return top-down or bottom-up; handle both
    bool topDown = bm.bmHeight < 0;
    uint32_t* px = (uint32_t*)bm.bmBits;
    std::vector<uint32_t> row((size_t)w);
    for (int y = 0; y < h; y++) {
        int src = topDown ? y : (h - 1 - y);
        memcpy(row.data(), px + (size_t)src * w, (size_t)w * 4);
        if (fwrite(row.data(), 4, (size_t)w, f) != (size_t)w) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { fwprintf(stderr, L"usage: factorytest <model> <out.bmp> [size]\n"); return 2; }
    int size = argc > 3 ? _wtoi(argv[3]) : 256;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    wchar_t full[MAX_PATH] = {0};
    GetFullPathNameW(argv[1], MAX_PATH, full, nullptr);
    IShellItemImageFactory* fac = nullptr;
    hr = SHCreateItemFromParsingName(full, nullptr, IID_PPV_ARGS(&fac));
    if (FAILED(hr)) { fwprintf(stderr, L"SHCreateItem failed %08lx\n", hr); CoUninitialize(); return 1; }
    SIZE sz{(LONG)size, (LONG)size};
    HBITMAP hb = nullptr;
    DWORD t0 = GetTickCount();
    hr = fac->GetImage(sz, SIIGBF_RESIZETOFIT, &hb);
    fwprintf(stderr, L"GetImage hr=%08lx dt=%lums\n", hr, GetTickCount() - t0);
    int rc = 1;
    if (SUCCEEDED(hr) && hb) {
        BITMAP bm{};
        GetObjectW(hb, sizeof(bm), &bm);
        fwprintf(stderr, L"bitmap %dx%d\n", bm.bmWidth, abs(bm.bmHeight));
        rc = SaveBmp(hb, argv[2]) ? 0 : 1;
        fwprintf(stderr, rc ? L"WRITE FAILED\n" : L"WROTE %s\n", argv[2]);
        DeleteObject(hb);
    }
    fac->Release();
    CoUninitialize();
    return rc;
}
