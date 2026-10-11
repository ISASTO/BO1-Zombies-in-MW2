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
| [ZoneTool](https://github.com/ZoneTool/zonetool) | IW-family linking; IW4 XAnim writer inspected at develop `aa4475b185de5ef6f5a63360d3d5e9c3321ca7ff` | Different target binaries/branches and IW5 reuse; no direct ABI or engine-address reuse |
| [IW4x client](https://github.com/iw4x/iw4x-client) | IW4 structures, assets and renderer concepts; main `d1b175da1a25d31043246de103a702b206269e5a` | Multiplayer/historical assumptions; inspected Structs.hpp is not a verified SP actor ABI for this AMD64 engine |
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

## Test8 native weapon access reference

- [IW4x ClientCommand implementation](https://github.com/iw4x/iw4x-client/blob/main/src/Components/Modules/ClientCommand.cpp):
  current primary upstream reference inspected for native g_giveAll semantics.
  Its stock global access behavior was a lead; exact retail AMD64 registration,
  inventory consumer, command branch and loaded catalogs were independently
  checked. No GPL implementation was copied and no upstream x86 address/layout
  was used. The current unpinned link is context, not executable evidence.

## Test9 inventory and dual-wield references

- The same IW4x ClientCommand source was inspected for the distinction between
  its global access bypass and its separate ammo compatibility hooks. That
  distinction was a research lead, not an imported implementation.
- [OAT IW4 asset definitions](https://github.com/Laupetin/OpenAssetTools/blob/main/src/Common/Game/IW4/IW4_Assets.h)
  supplied field-name/format leads for player gun models, clip size, alternate
  weapon chains and dual eligibility. The exact AMD64 native grant/getter/PM
  instructions independently establish every offset used here. No complete
  upstream structure, x86 address, T5 structure, or GPL implementation is copied.
  The current link is context; the supplied executable hash is the authority.

## Test10 animation references

The same primary OAT IW4 header supplies naming/format leads for handed arrays
and animation slots. Every used offset/count is corroborated by this executable:
86740/86e80 select arrays, c2c70 processes37 names, c4b38 groups three cache
slots, a6392/a93f2 clear200 groups, and a9730/2689a0 establish arena allocation.
No OAT/GPL structure or implementation is copied. T5 asset/actor layouts remain
separate; this generic weapon animation fallback does not implement Zombies
animation conversion or reproduce authentic akimbo asset authoring.
