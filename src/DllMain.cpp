// ModelThumbLite - DLL entry + self-registration (HKCU, no admin needed).
// Registration layout mirrors NCrewsShellExtensions:
// CLSID\InprocServer32, per-extension shellex thumbnail handler entries,
// Shell Extensions\Approved, SHChangeNotify. All code below is original.
#include <initguid.h>
#include "Provider.h"
#include "ThumbGuid.h"
#include <shlobj.h>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

HINSTANCE g_hInst = nullptr;
LONG g_dllRefs = 0;
void DllAddRef() { InterlockedIncrement(&g_dllRefs); }
void DllRelease() { InterlockedDecrement(&g_dllRefs); }

BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() { return g_dllRefs <= 0 ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, CLSID_ModelThumbLite) && !IsEqualCLSID(clsid, CLSID_ModelPreviewLite))
        return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* f = new (std::nothrow) CClassFactory(clsid);
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

namespace {
const wchar_t* kExts[] = {L".stl", L".obj", L".3mf", L".glb", L".gltf", L".fbx", L".ply", L".las", L".laz"};
const wchar_t* kThumbHandlerKey = L"shellex\\{E357FCCD-A995-4576-B01F-234630154E96}"; // thumbnail handler association
const wchar_t* kPreviewHandlerKey = L"shellex\\{8895b1c6-b41f-4c1c-a562-0d564250836f}"; // preview handler association
const wchar_t* kLegacyImageKey = L"ShellEx\\{BB2E617C-0920-11D1-9A0B-00C04FC2D6C1}"; // legacy image handler (Win11 compat)

bool WriteDefault(HKEY root, const std::wstring& sub, const std::wstring& value) {
    HKEY h = nullptr; DWORD d = 0;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h, &d) != ERROR_SUCCESS)
        return false;
    LSTATUS s = RegSetValueExW(h, nullptr, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return s == ERROR_SUCCESS;
}
bool WriteNamed(HKEY root, const std::wstring& sub, const wchar_t* name, const std::wstring& value) {
    HKEY h = nullptr; DWORD d = 0;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h, &d) != ERROR_SUCCESS)
        return false;
    LSTATUS s = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return s == ERROR_SUCCESS;
}
void DllPath(std::wstring& out) {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, buf, MAX_PATH);
    out = buf;
}
} // namespace

STDAPI DllRegisterServer() {
    std::wstring dll;
    DllPath(dll);
    if (dll.empty()) return E_UNEXPECTED;

    std::wstring clsid = std::wstring(L"Software\\Classes\\CLSID\\") + MODELTHUMBLITE_CLSID_STR;
    if (!WriteDefault(HKEY_CURRENT_USER, clsid, MODELTHUMBLITE_NAME)) return E_UNEXPECTED;
    if (!WriteDefault(HKEY_CURRENT_USER, clsid + L"\\InprocServer32", dll)) return E_UNEXPECTED;
    if (!WriteNamed(HKEY_CURRENT_USER, clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment")) return E_UNEXPECTED;

    std::wstring pclsid = std::wstring(L"Software\\Classes\\CLSID\\") + MODELPREVIEWLITE_CLSID_STR;
    if (!WriteDefault(HKEY_CURRENT_USER, pclsid, MODELPREVIEWLITE_NAME)) return E_UNEXPECTED;
    if (!WriteDefault(HKEY_CURRENT_USER, pclsid + L"\\InprocServer32", dll)) return E_UNEXPECTED;
    if (!WriteNamed(HKEY_CURRENT_USER, pclsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment")) return E_UNEXPECTED;

    for (auto ext : kExts) {
        std::wstring base = std::wstring(L"Software\\Classes\\") + ext + L"\\";
        if (!WriteDefault(HKEY_CURRENT_USER, base + kThumbHandlerKey, MODELTHUMBLITE_CLSID_STR)) return E_UNEXPECTED;
        if (!WriteDefault(HKEY_CURRENT_USER, base + kPreviewHandlerKey, MODELPREVIEWLITE_CLSID_STR)) return E_UNEXPECTED;
        // legacy key: best effort only (may already belong to another provider)
        WriteDefault(HKEY_CURRENT_USER, base + kLegacyImageKey, MODELTHUMBLITE_CLSID_STR);
    }
    WriteNamed(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
               MODELTHUMBLITE_CLSID_STR, MODELTHUMBLITE_NAME);
    WriteNamed(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
               MODELPREVIEWLITE_CLSID_STR, MODELPREVIEWLITE_NAME);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    for (auto ext : kExts) {
        std::wstring base = std::wstring(L"Software\\Classes\\") + ext + L"\\";
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + kThumbHandlerKey).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + kPreviewHandlerKey).c_str());
        // only remove legacy key if it points at us
        HKEY h = nullptr;
        std::wstring legacy = base + kLegacyImageKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, legacy.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
            wchar_t cur[128] = {0}; DWORD cb = sizeof(cur), t = 0;
            if (RegQueryValueExW(h, nullptr, nullptr, &t, (BYTE*)cur, &cb) == ERROR_SUCCESS &&
                _wcsicmp(cur, MODELTHUMBLITE_CLSID_STR) == 0) {
                RegCloseKey(h);
                RegDeleteTreeW(HKEY_CURRENT_USER, legacy.c_str());
            } else RegCloseKey(h);
        }
    }
    std::wstring clsid = std::wstring(L"Software\\Classes\\CLSID\\") + MODELTHUMBLITE_CLSID_STR;
    RegDeleteTreeW(HKEY_CURRENT_USER, clsid.c_str());
    std::wstring pclsid = std::wstring(L"Software\\Classes\\CLSID\\") + MODELPREVIEWLITE_CLSID_STR;
    RegDeleteTreeW(HKEY_CURRENT_USER, pclsid.c_str());
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_WRITE, &h) == ERROR_SUCCESS) {
        RegDeleteValueW(h, MODELTHUMBLITE_CLSID_STR);
        RegDeleteValueW(h, MODELPREVIEWLITE_CLSID_STR);
        RegCloseKey(h);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
