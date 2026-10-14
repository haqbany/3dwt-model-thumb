# Attribution

## Method

The COM thumbnail-provider architecture used here
(in-proc DLL implementing `IThumbnailProvider` + `IInitializeWithFile` /
`IInitializeWithStream` / `IInitializeWithItem`, per-extension `shellex`
thumbnail-handler registration under HKCU, content sniffing for stream init)
follows the method of **NCrewsShellExtensions** by Nathan Crews,
https://github.com/nathancrews/NCrewsShellExtensions,
used under its BSD-style license (redistribution in source and binary forms
permitted with retained copyright notice; see `NCrewsShellExtensions-main/LICENSE.txt`
in the parent folder). All ModelThumbLite source code is written from scratch.

## Bundled third-party libraries (in `thirdparty/`)

- cgltf (MIT) — https://github.com/jkuhlmann/cgltf — glTF/GLB parsing
- tinyobjloader (MIT) — https://github.com/tinyobjloader/tinyobjloader — OBJ parsing
- ufbx (MIT) — https://github.com/ufbx/ufbx — FBX parsing
- miniz (MIT/Unlicense) — https://github.com/richgel999/miniz — ZIP container for 3MF
- LASzip (Apache-2.0) — https://github.com/LASzip/LASzip — LAZ decompression
