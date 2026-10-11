# Primary technical references

Reviewed 2026-10-11. External code is research material; current runtime code is
original and targets only the supplied retail executable. BO1 behavior claims
are grounded first in the extracted local scripts, not forum memories.

| Reference | Useful material | Limits |
| --- | --- | --- |
| [MisaDev4 BO1 decompilation](https://github.com/MisaDev4/bo1-zombies-decompiled) | Native actor state/navigation/senses/threat, exposed zombie thinker, animation internals; pin `102f6992be332610aff587e681e3180b15f395bb` | Validate against uploaded x86 BlackOps.exe; reimplementation changes and provenance need auditing; no top-level license located in inspected tree, so no source copied |
| [OpenAssetTools](https://github.com/Laupetin/OpenAssetTools) | Game-specific fastfile loaders, T5/IW4 asset schemas, GSC/CSC RawFile decompression; pin `068d4cd9f513ff0509b757498712b2ec3dee9463` | GPL-3.0 project; incomplete export/import coverage; historical format support does not prove 2026 runtime compatibility |
| [OAT supported assets](https://github.com/Laupetin/OpenAssetTools/blob/main/docs/SupportedAssetTypes.md) | Per-game supported asset operations | Check current per-asset coverage before choosing a conversion path |
| [OAT documentation](https://openassettools.dev/guide/using-the-tools.html) | Standalone zone extraction/linking workflow | A full zone tool, not our DLL loader |
| [ZoneTool](https://github.com/ZoneTool/zonetool) | IW-family linking and XModel/XAnim/material handling | Different target binaries/branches; no direct ABI or engine-address reuse |
| [IW4x client](https://github.com/iw4x/iw4x-client) | IW4 structures, asset and renderer concepts, mod-loading architecture | Multiplayer client and historical executable assumptions; not the supplied retail SP engine |
| [GEEKiDoS MW2 SP branch](https://github.com/GEEKiDoS/mw2-mod/tree/iw4sp) | Historical SP client context | Very experimental older client; no requirement to install it and no compatible-address assumption |
| [Nukem9 LinkerMod](https://github.com/Nukem9/LinkerMod) | BO1 mod-tools and script/zone-loading research | T5-specific, not an IW4 compatibility layer |
| [Microsoft DLL search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order) | Application-directory proxy and absolute-path system load | Runtime environment, existing loaded modules, and DLL redirection still need the first local test |
| [Microsoft IDirect3D9 interface](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nn-d3d9-idirect3d9) | Documented COM factory/device creation | Standard interface; game-specific render flow still observed separately |
| [Microsoft IDirect3DDevice9 interface](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nn-d3d9-idirect3ddevice9) | EndScene/Present/Reset/state blocks | Does not establish engine threading or level state |
| [Microsoft minidumps](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump) | Local crash diagnostics | Exception filters and game handlers can affect capture |

Our T5 script extractor implements the observed data format independently; it
does not include OAT source. External implementation code, generated BO1 source,
and game assets are not vendored. If future integration adopts licensed external
code, add its precise revision, license, notices, and distribution obligations
before committing that dependency.

## Test5 numeric and StringTable references

- [Pinned T5 VM operator implementation](https://github.com/MisaDev4/bo1-zombies-decompiled/blob/102f6992be332610aff587e681e3180b15f395bb/src/clientscript/cscr_variable.cpp):
  `Scr_EvalDivide` was a lead for float division. Independently matched its
  semantics in the owner's exact executable at RVA `0x1bde60`; no source copied.
- [Pinned VM value definitions](https://github.com/MisaDev4/bo1-zombies-decompiled/blob/102f6992be332610aff587e681e3180b15f395bb/src/clientscript/cscr_variable.h):
  float/integer tags 5/6. Native getter/divide code independently corroborates
  the tags and partial layout; not an IW4 structure definition.
- [Pinned OAT T5 asset definitions](https://github.com/Laupetin/OpenAssetTools/blob/068d4cd9f513ff0509b757498712b2ec3dee9463/src/Common/Game/T5/T5_Assets.h)
  and [StringTable zone schema](https://github.com/Laupetin/OpenAssetTools/blob/068d4cd9f513ff0509b757498712b2ec3dee9463/src/ZoneCode/Game/T5/XAssets/StringTable.txt):
  format/dependency references for the tuning asset. The independent scoped
  reader now recovers selected normal tuning; no OAT/GPL code is copied.

## Test6 packed-pointer references

- [Pinned OAT zone input stream](https://github.com/Laupetin/OpenAssetTools/blob/068d4cd9f513ff0509b757498712b2ec3dee9463/src/ZoneLoading/Zone/Stream/ZoneInputStream.cpp)
  and [T5 constants](https://github.com/Laupetin/OpenAssetTools/blob/068d4cd9f513ff0509b757498712b2ec3dee9463/src/ZoneCommon/Game/T5/ZoneConstantsT5.h):
  reference leads for subtract-one packed references, three block bits and zone
  version 473. The supplied records independently corroborate selected mappings.
- [Pinned T5 StringTable loader](https://github.com/Laupetin/OpenAssetTools/blob/068d4cd9f513ff0509b757498712b2ec3dee9463/src/ObjLoading/Game/T5/StringTable/LoaderStringTableT5.cpp):
  string/hash and cell-index concepts. Our reader validates hashes/permutations,
  but does not claim to reproduce all native sorting/fixup behavior.
