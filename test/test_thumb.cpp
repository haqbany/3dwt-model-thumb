// ModelThumbLite - offline test: renders a thumbnail and writes a .bmp (no registration needed).
// Usage: test_thumb.exe <model> <out.bmp> [size]
#include "../src/Loaders.h"
#include "../src/Raster.h"
#include <windows.h>
#include <cwctype>
#include <cstdio>
#include <string>
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
    // our DIB is top-down; BMP is bottom-up: flip rows
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
    if (argc < 3) { fwprintf(stderr, L"usage: test_thumb <model> <out.bmp> [size]\n"); return 2; }
    UINT size = argc > 3 ? (UINT)_wtoi(argv[3]) : 256;
    DWORD t0 = GetTickCount();
    LiteMesh mesh; LitePoints cloud;
    if (!LoadModelFile(argv[1], mesh, cloud) || (mesh.empty() && cloud.empty())) {
        fwprintf(stderr, L"LOAD FAILED\n"); return 1;
    }
    fwprintf(stderr, L"loaded: %llu tris / %llu points in %lums\n",
             (unsigned long long)mesh.triCount(), (unsigned long long)cloud.count(), GetTickCount() - t0);
    t0 = GetTickCount();
    wchar_t label[16] = {0};
    {
        std::wstring p = argv[1];
        size_t dot = p.find_last_of(L'.');
        size_t sep = p.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (sep == std::wstring::npos || sep < dot)) {
            std::wstring ext = p.substr(dot + 1, 14);
            for (size_t i = 0; i < ext.size() && i < 14; i++)
                label[i] = (wchar_t)towupper(ext[i]);
        }
    }
    HBITMAP hb = !mesh.empty() ? RenderMeshThumb(mesh, size, label[0] ? label : nullptr)
                               : RenderCloudThumb(cloud, size, label[0] ? label : nullptr);
    if (!hb) { fwprintf(stderr, L"RENDER FAILED\n"); return 1; }
    fwprintf(stderr, L"rendered %ux%u in %lums\n", size, size, GetTickCount() - t0);
    bool ok = SaveBmp(hb, argv[2]);
    DeleteObject(hb);
    fwprintf(stderr, ok ? L"WROTE %s\n" : L"WRITE FAILED\n", argv[2]);
    return ok ? 0 : 1;
}
