# ModelThumbLite — live Explorer thumbnails + one-click quick view

`.stl .obj .3mf .glb .gltf .fbx .ply .las .laz`:

- **Thumbnails** in Explorer (Large icons / Tiles), rendered on demand.
- **Quick view**: turn on the preview pane once
  (`View > Show > Preview pane`), then a single click on any model file
  shows a large render — no app launch.

COM architecture follows the method of NCrewsShellExtensions (see ATTRIBUTION.md);
all provider/rendering code is original and needs no Open3D, OpenCV, or vcpkg.

## Build

Requires VS 2022 Build Tools (C++ workload + Windows SDK) — `build.ps1`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
```

Output: `bin\Release\ModelThumbLite.dll`, `bin\Release\test_thumb.exe`
(single self-contained DLL; only the VC++ runtime is needed).

## Validate without touching Explorer

```powershell
.\bin\Release\test_thumb.exe <model> out.bmp 256
```

## Install / uninstall (per-user, no admin)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1     # registers + clears thumb cache (restarts Explorer)
powershell -NoProfile -ExecutionPolicy Bypass -File uninstall.ps1   # removes; .stl/.obj/.3mf fall back to Windows' own provider
```

## Notes

- Meshes render flat-shaded steel blue; point clouds height/intensity ramp.
  Every thumbnail carries a small gray extension label (STL/OBJ/3MF/GLB/GLTF/FBX/PLY/LAS/LAZ).
  Textures/materials are intentionally not applied (keeps it fast and dependency-free).
- `.gltf` with external `.bin` works via file path; stream-only init sniffs content like NCrews.
- Vertex-only `.obj`/`.ply` (scan exports with no faces) render as point clouds.
- Failures log one line to `%TEMP%\ModelThumbLite.log`, never a dialog.
- Runtime cost: Benchy-class meshes (~225k tris) render in ~1–3 s at 256 px;
  1M-tri binary STL ~60 ms; 35 MB ASCII STL ~50 ms (no `sscanf`, custom float parser).

## Validated (test_thumb.exe + live IShellItemImageFactory)

STL binary + ASCII, OBJ mesh + vertex-only, 3MF, GLB (+WebP), GLTF,
FBX binary + ASCII, PLY binary + CRLF + vertex-only, LAS, LAZ — all render;
live `GetImage` returns S_OK/ARGB through the registered handler.
