// ModelThumbLite - in-proc COM test (STA, like Explorer): CoCreateInstance ->
// IInitializeWithFile -> IThumbnailProvider::GetThumbnail -> save .bmp
// Usage: comtest.exe <model> <out.bmp> [size]
#include <windows.h>
#include <thumbcache.h>
#include <shlobj.h>
#include <cstdio>
#include <vector>
// {E8A4B2C1-5D3F-4A7E-9C1B-6F2A4D8C0E13}
static const CLSID CLSID_Test =
    {0xE8A4B2C1, 0x5D3F, 0x4A7E, {0x9C, 0x1B, 0x6F, 0x2A, 0x4D, 0x8C, 0x0E, 0x13}};

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
    uint32_t* px = (uint32_t*)bm.bmBits;
    std::vector<uint32_t> row((size_t)w);
    for (int y = h - 1; y >= 0; y--) {
        memcpy(row.data(), px + (size_t)y * w, (size_t)w * 4);
        if (fwrite(row.data(), 4, (size_t)w, f) != (size_t)w) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { fwprintf(stderr, L"usage: comtest <model> <out.bmp> [size]\n"); return 2; }
    UINT size = argc > 3 ? (UINT)_wtoi(argv[3]) : 256;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { fwprintf(stderr, L"CoInitializeEx failed %08lx\n", hr); return 1; }
    int rc = 1;
    IThumbnailProvider* prov = nullptr;
    hr = CoCreateInstance(CLSID_Test, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&prov));
    if (FAILED(hr)) { fwprintf(stderr, L"CoCreateInstance failed %08lx\n", hr); CoUninitialize(); return 1; }
    fwprintf(stderr, L"COM activated.\n");
    IInitializeWithFile* init = nullptr;
    hr = prov->QueryInterface(IID_PPV_ARGS(&init));
    if (SUCCEEDED(hr)) {
        hr = init->Initialize(argv[1], 0);
        init->Release();
    }
    if (FAILED(hr)) { fwprintf(stderr, L"Initialize failed %08lx\n", hr); }
    else {
        DWORD t0 = GetTickCount();
        HBITMAP hb = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        hr = prov->GetThumbnail(size, &hb, &alpha);
        fwprintf(stderr, L"GetThumbnail hr=%08lx alpha=%d dt=%lums\n", hr, (int)alpha, GetTickCount() - t0);
        if (SUCCEEDED(hr) && hb) {
            rc = SaveBmp(hb, argv[2]) ? 0 : 1;
            fwprintf(stderr, rc ? L"WRITE FAILED\n" : L"WROTE %s\n", argv[2]);
            DeleteObject(hb);
        }
    }
    prov->Release();
    CoUninitialize();
    return rc;
}
