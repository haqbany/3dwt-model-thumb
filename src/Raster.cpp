// ModelThumbLite - orthographic z-buffer rasterizer (original code).
#include "Raster.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979f;

struct Camera {
    float right[3], up[3], dir[3];
    float cx, cy, cz;   // center
    float scale;        // px per world unit
    int size;
};

// iso view matching our preview generator
void SetupCamera(const float* pts /*xyz*count*/, size_t count, int size, Camera& cam) {
    float el = 20.0f * kPi / 180.0f, az = -60.0f * kPi / 180.0f;
    cam.dir[0] = cosf(el) * cosf(az); cam.dir[1] = cosf(el) * sinf(az); cam.dir[2] = sinf(el);
    // right = normalize(cross(dir, +Z))
    float rx = -cam.dir[1], ry = cam.dir[0], rz = 0;
    float rl = sqrtf(rx*rx + ry*ry);
    if (rl < 1e-6f) { rx = 1; ry = 0; rl = 1; }
    cam.right[0] = rx/rl; cam.right[1] = ry/rl; cam.right[2] = 0;
    // up = cross(right, dir)
    cam.up[0] = cam.right[1]*cam.dir[2] - cam.right[2]*cam.dir[1];
    cam.up[1] = cam.right[2]*cam.dir[0] - cam.right[0]*cam.dir[2];
    cam.up[2] = cam.right[0]*cam.dir[1] - cam.right[1]*cam.dir[0];
    double mx = 0, my = 0, mz = 0;
    for (size_t i = 0; i < count; i++) { mx += pts[i*3]; my += pts[i*3+1]; mz += pts[i*3+2]; }
    cam.cx = (float)(mx / count); cam.cy = (float)(my / count); cam.cz = (float)(mz / count);
    float maxR = 0;
    for (size_t i = 0; i < count; i++) {
        float dx = pts[i*3]-cam.cx, dy = pts[i*3+1]-cam.cy, dz = pts[i*3+2]-cam.cz;
        float sx = dx*cam.right[0] + dy*cam.right[1] + dz*cam.right[2];
        float sy = dx*cam.up[0] + dy*cam.up[1] + dz*cam.up[2];
        float r = sqrtf(sx*sx + sy*sy);
        if (r > maxR) maxR = r;
    }
    if (maxR < 1e-9f) maxR = 1.0f;
    cam.size = size;
    cam.scale = (size / 2.0f - 3.0f) / maxR;
}

inline void Project(const Camera& cam, float x, float y, float z, float& px, float& py, float& depth) {
    float dx = x - cam.cx, dy = y - cam.cy, dz = z - cam.cz;
    float sx = dx*cam.right[0] + dy*cam.right[1] + dz*cam.right[2];
    float sy = dx*cam.up[0] + dy*cam.up[1] + dz*cam.up[2];
    depth = dx*cam.dir[0] + dy*cam.dir[1] + dz*cam.dir[2];
    px = cam.size / 2.0f + sx * cam.scale;
    py = cam.size / 2.0f - sy * cam.scale;
}

HBITMAP AllocBitmap(int size, uint32_t** outBits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hb && bits) {
        *outBits = (uint32_t*)bits;
        // white opaque background
        size_t n = (size_t)size * size;
        for (size_t i = 0; i < n; i++) (*outBits)[i] = 0xFFFFFFFF;
    }
    return hb;
}

// blue->cyan->green->yellow ramp
void RampColor(float t, uint8_t& r, uint8_t& g, uint8_t& b) {    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    // stops: 0:(40,60,180) 0.35:(30,170,170) 0.65:(60,180,90) 1:(240,210,60)
    float stops[4][3] = {{40,60,180},{30,170,170},{60,180,90},{240,210,60}};
    float pos = t * 3.0f;
    int s = (int)pos; if (s > 2) s = 2;
    float f = pos - s;
    r = (uint8_t)(stops[s][0] + (stops[s+1][0]-stops[s][0])*f);
    g = (uint8_t)(stops[s][1] + (stops[s+1][1]-stops[s][1])*f);
    b = (uint8_t)(stops[s][2] + (stops[s+1][2]-stops[s][2])*f);
}
} // namespace

// Extension badge (e.g. "STL") bottom-right: dark pill + bold white text.
// GDI ops on a 32bpp DIB preserve the (opaque) alpha channel.
static void WatermarkBitmap(HBITMAP hb, int size, const wchar_t* label) {
    if (!hb || !label || !*label) return;
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return;
    HGDIOBJ oldBmp = SelectObject(hdc, hb);
    int fontPx = size * 15 / 100; // label ~15% of thumbnail size
    if (fontPx < 9) fontPx = 9;
    HFONT font = CreateFontW(-fontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
    SIZE ts{};
    GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &ts);
    int m = size / 32 + 2;
    int tx = m, ty = m;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    TextOutW(hdc, tx + 1, ty + 1, label, (int)wcslen(label)); // light offset for contrast
    SetTextColor(hdc, RGB(192, 192, 192));
    TextOutW(hdc, tx, ty, label, (int)wcslen(label));
    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    SelectObject(hdc, oldBmp);
    DeleteDC(hdc);
}

HBITMAP RenderMeshThumb(const LiteMesh& mesh, UINT requested, const wchar_t* label) {
    size_t nv = mesh.vertCount(), nt = mesh.triCount();
    if (!nv || !nt) return nullptr;
    int size = (int)(requested ? requested : 256);
    if (size > 512) size = 512;
    if (size < 32) size = 32;
    // stride-cap absurd meshes (even sampling keeps coverage)
    const size_t kCap = 500000;
    size_t step = (nt + kCap - 1) / kCap;
    if (!step) step = 1;

    Camera cam;
    SetupCamera(mesh.verts.data(), nv, size, cam);

    uint32_t* bits = nullptr;
    HBITMAP hb = AllocBitmap(size, &bits);
    if (!hb) return nullptr;
    std::vector<float> zbuf((size_t)size * size, -1e30f);

    float light[3] = {0.4f, 0.5f, 0.75f};
    float ll = sqrtf(light[0]*light[0]+light[1]*light[1]+light[2]*light[2]);
    light[0]/=ll; light[1]/=ll; light[2]/=ll;

    const float* V = mesh.verts.data();
    for (size_t t = 0; t < nt; t += step) {
        uint32_t i0 = mesh.tris[t*3], i1 = mesh.tris[t*3+1], i2 = mesh.tris[t*3+2];
        if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
        float ax=V[i0*3], ay=V[i0*3+1], az=V[i0*3+2];
        float bx=V[i1*3], by=V[i1*3+1], bz=V[i1*3+2];
        float cx=V[i2*3], cy=V[i2*3+1], cz=V[i2*3+2];
        // face normal (world)
        float e1x=bx-ax, e1y=by-ay, e1z=bz-az;
        float e2x=cx-ax, e2y=cy-ay, e2z=cz-az;
        float nx=e1y*e2z-e1z*e2y, ny=e1z*e2x-e1x*e2z, nz=e1x*e2y-e1y*e2x;
        float nl = sqrtf(nx*nx+ny*ny+nz*nz);
        if (nl < 1e-12f) continue;
        nx/=nl; ny/=nl; nz/=nl;
        float shade = fabsf(nx*light[0]+ny*light[1]+nz*light[2])*0.55f + 0.45f;
        uint8_t R = (uint8_t)(77*shade), G = (uint8_t)(143*shade), B = (uint8_t)(235*shade);

        float pax, pay, da, pbx, pby, db, pcx, pcy, dc;
        Project(cam, ax, ay, az, pax, pay, da);
        Project(cam, bx, by, bz, pbx, pby, db);
        Project(cam, cx, cy, cz, pcx, pcy, dc);
        int x0 = (int)floorf(fminf(fminf(pax,pbx),pcx)), x1 = (int)ceilf(fmaxf(fmaxf(pax,pbx),pcx));
        int y0 = (int)floorf(fminf(fminf(pay,pby),pcy)), y1 = (int)ceilf(fmaxf(fmaxf(pay,pby),pcy));
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 >= size) x1 = size-1; if (y1 >= size) y1 = size-1;
        if (x0 > x1 || y0 > y1) continue;
        float denom = (pby-pcy)*(pax-pcx)+(pcx-pbx)*(pay-pcy);
        if (fabsf(denom) < 1e-9f) continue;
        uint32_t rgb = 0xFF000000 | (R<<16) | (G<<8) | B;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float w1 = ((pby-pcy)*(x-pcx)+(pcx-pbx)*(y-pcy))/denom;
                float w2 = ((pcy-pay)*(x-pcx)+(pax-pcx)*(y-pcy))/denom;
                float w0 = 1.0f - w1 - w2;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float depth = w0*da + w1*db + w2*dc;
                size_t idx = (size_t)y * size + x;
                if (depth > zbuf[idx]) { zbuf[idx] = depth; bits[idx] = rgb; }
            }
        }
    }
    WatermarkBitmap(hb, size, label);
    return hb;
}

HBITMAP RenderCloudThumb(const LitePoints& cloud, UINT requested, const wchar_t* label) {
    size_t n = cloud.count();
    if (!n) return nullptr;
    int size = (int)(requested ? requested : 256);
    if (size > 512) size = 512;
    if (size < 32) size = 32;

    Camera cam;
    SetupCamera(cloud.xyz.data(), n, size, cam);

    uint32_t* bits = nullptr;
    HBITMAP hb = AllocBitmap(size, &bits);
    if (!hb) return nullptr;
    std::vector<float> zbuf((size_t)size * size, -1e30f);
    int dot = size >= 256 ? 2 : 1;

    // color by height (linear): always meaningful, unlike intensity which is
    // often near-constant (e.g. dc.laz renders as a flat slab otherwise)
    float zmin = cloud.xyz[2], zmax = cloud.xyz[2];
    for (size_t i = 1; i < n; i++) {
        float z = cloud.xyz[i*3+2];
        if (z < zmin) zmin = z;
        if (z > zmax) zmax = z;
    }
    float zspan = zmax - zmin;
    if (zspan < 1e-9f) zspan = 1.0f;

    for (size_t i = 0; i < n; i++) {
        float px, py, depth;
        Project(cam, cloud.xyz[i*3], cloud.xyz[i*3+1], cloud.xyz[i*3+2], px, py, depth);
        int x = (int)(px + 0.5f), y = (int)(py + 0.5f);
        uint8_t R, G, B;
        RampColor((cloud.xyz[i*3+2] - zmin) / zspan, R, G, B);
        uint32_t rgb = 0xFF000000 | (R<<16) | (G<<8) | B;
        for (int dy = 0; dy < dot; dy++) for (int dx = 0; dx < dot; dx++) {
            int xx = x+dx, yy = y+dy;
            if (xx < 0 || yy < 0 || xx >= size || yy >= size) continue;
            size_t idx = (size_t)yy * size + xx;
            if (depth > zbuf[idx]) { zbuf[idx] = depth; bits[idx] = rgb; }
        }
    }
    WatermarkBitmap(hb, size, label);
    return hb;
}
