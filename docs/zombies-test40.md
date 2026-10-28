# Test40: BO1 zombie models

Version **0.1.39-zombies-test40**. It builds on Test39: Spec Ops maps, the
BO1 animations and BO1's zombie setup are unchanged.

## 1. What changes in game

Zombies now wear the Kino der Toten zombie instead of an MW2 soldier. This is
BO1's `character\c_ger_honorguard_zt`:

| Part | BO1 models | How one is picked |
| --- | --- | --- |
| Body | `c_ger_honorguard_body1`, `c_ger_honorguard_body2` | at random, body1 1 time in 4 and body2 3 times in 4 (BO1's `c_ger_honorguard_zomb_bodyalias`) |
| Head | `c_ger_zombie_head1` … `head4` | one of the four at random |

Each zombie is dressed the moment it spawns, before it starts to rise:

1. The MW2 helmet is first moved off `self.hatModel`. This uses MW2's own
   `animscripts/shared::HandleNoteTrack("swap taghelmet to tagleft")`;
   otherwise MW2's `helmetPop` would later look for a helmet that is gone.
2. `detachall` removes every MW2 attachment (head, helmet, gear).
3. `setmodel` sets the BO1 body.
4. `attach` adds the BO1 head with no tag. This is how BO1 attaches it: the
   head's `j_spine4`, `j_neck` and `j_head` bones then follow the body's bones
   of the same name.

Hellhounds keep MW2's dog model.

## 2. Where the models come from

Nothing from Black Ops is in the mod or the repository. The models are built
on your computer from your own Black Ops install:

- **Models and materials:** `zone\Common\zombie_theater.ff` (Kino der Toten)
- **Textures:** the `main\*.iwd` image archives

When MW2 starts, a background thread:

1. finds Black Ops (the same `bo1mw2\bo1_path.txt` as Test39);
2. reads and inflates `zombie_theater.ff`;
3. carves out the six models;
4. reads their eight textures from the `.iwd` archives (as in BO1, the last
   archive that holds a name wins);
5. writes an MW2 fastfile, `bo1mw2\cache\bo1_zombie_models.ff`, and a small
   manifest `bo1_zombie_models.txt` beside it.

The next start reuses the cached file. It is rebuilt only when the size or
date of `zombie_theater.ff` or any `.iwd` changes, or when a new mod version
changes the converter. The first build takes a few seconds and runs while
the menu loads; a zombies launch waits up to 30 s for it.

## 3. Converting BO1 (T5) models to MW2

BO1 and MW2 store most model data the same way, so most of it is copied
byte for byte:

- vertex positions, normals, colours, texture coordinates and tangents;
- the skinning weights (`vertsBlend`);
- triangles;
- the rigid vertex lists and their collision trees.

These parts are rewritten for MW2:

- **Bone names** become MW2 script strings.
- **Bone translations** shrink from 16 to 12 bytes each.
- **Bone bounds** (`XBoneInfo`) change from BO1's 44 bytes (mins, maxs,
  offset, radius²) to MW2's 28 (midpoint, half size, radius²).
- **Collision surfaces and LOD info** use MW2's layout. Each LOD keeps BO1's
  switch distances.
- **Materials.** BO1's shader sets do not exist in MW2, so each zombie gets
  three MW2 materials: body, head skin and eyes.
  - They use MW2's lit, normal-mapped, specular model shader
    `mc_l_sm_r0c0n0s0` from MW2's `common.ff`.
  - They use the opaque state bits of MW2's own model materials (for example
    `mc/mtl_weapon_m14`).
  - The surface type is flesh, so bullet impacts show blood.
- **Textures.**
  - BO1's `.iwi` (version 13) DXT1/DXT3/DXT5 images become MW2 images; only
    the order of the mip levels changes (BO1 stores the smallest first).
  - MW2 keeps a material's gloss in the alpha channel of its specular map,
    while BO1 keeps the head's gloss in a separate image. The two are merged
    into one DXT5 image.

**Missing textures never stop the models.** If one is missing or unreadable,
a fallback is used and logged:

- a colour map falls back to plain grey;
- a normal map falls back to MW2's flat `$identitynormalmap`;
- a specular map falls back to a dull constant one.

**The bodies' close-up materials.** The zombie bodies' own close-up materials
(`..._zombie_linen`, `_lthr`, `_metal`, `_body1`) are only referenced in
`zombie_theater.ff`. They are defined in another BO1 zone that this build
does not read. Every body surface instead uses the honorguard uniform sheet,
`mc/mtl_char_ger_honorguard_lthr1_1`. BO1 itself uses that sheet for the
bodies' distant LODs, and it is in `zombie_theater.ff`. Up close, the bodies
may look cleaner than in BO1.

## 4. Loading into a level

**Joining the level load.** The level's own zone is loaded by one call,
`DB_LoadXAssets` at `0xf0ac3` inside `Com_LoadLevelFastFiles`. The DLL
redirects that call. On a zombies launch it adds the generated fastfile to
that same load with the same allocation flags (4), so MW2 frees it with the
level. Other levels load exactly as before.

**Finding the file.** MW2 looks for zones in `zone\<language>\`, so the
fastfile is named `..\..\bo1mw2\cache\bo1_zombie_models`, which leads back to
the MW2 folder.

**Precaching.** While the level's scripts load (`level.initializing`), each
model is precached with `G_ModelIndex`. Before that, the DLL checks:

- each model's asset entry is the converted model (its name and bone count);
- at least 16 model slots are free.

If anything is missing, zombies keep the MW2 soldier model.

## 5. Crash guard

These models have not yet been seen in game, so the mod protects against a
crash loop:

- A marker file, `bo1mw2\cache\bo1_zombie_models.loading`, is written when
  the models join a level load.
- It is deleted after zombies have worn the models for 45 s, or when MW2
  exits normally.
- **If MW2 crashed** while the models were new, the next start finds the
  marker. The models are skipped, so zombies wear MW2 soldiers and play
  continues. The menu shows `M-SKIPPED` and the log has
  `BO1_MODELS guard=STALE_MARKER`.
- **If a level load ended in an error dialog**, the models are skipped for
  the rest of that run.
- **To try the models again,** delete the marker or run Install.cmd again.

## 6. Menu row and logs

The **BO1 anims / models** row in F4 → ZOMBIE MODE → Playable zombies shows:

- **Before a map:** for example `READY 27/27 M READY`. That is the animation
  read, then the model build: `BUILDING`, `READY`, `MISSING` (no
  `zombie_theater.ff`), `FAILED` or `SKIPPED`.
- **On a zombies map:** `A68/68 M6 SO3`. That is 68 animations applied, 6 BO1
  models precached, and 3 Spec Ops end paths disabled. Without models it
  shows `M-` and the reason, for example `M-SKIPPED`.

Log lines (in `bo1mw2\logs`):

- **Startup:**
  - `BO1_MODELS state=READY cached=0 textures=8 fallbacks=0 bytes=… detail=built models=6 images=…`
  - `cached=1` on later starts.
- **Map load:**
  - `BO1_MODELS level_zone=<zone> append=1 …`
  - `BO1_MODELS precache=1 bodies=4 heads=4 …` (the body list holds body1
    once and body2 three times)
  - `LEVEL_PREP … bo1_models=8 zone_loaded=1`
- **In play:**
  - `BO1_MODELS confirmed=1` after 45 s.
  - **Zombies info to log** adds `ZOMBIE_MODELS … dressed=… dress_failures=0 helmet_swaps=…`.

## Uninstalling

Uninstall.cmd also removes the generated `bo1mw2\cache` files.
CollectLogs.cmd includes the cache manifest (source file paths, sizes and
dates), not the generated models.

## Not in this build

- **Gibs.** BO1 swaps in damaged torso and leg models and drops limbs; here
  zombies stay whole.
- **Glowing eyes.** BO1 adds an effect on `tag_eye`; not yet.
- **Kino only.** These are the only zombie characters for now.
- **Body close-up materials:** see section 3.
- **Effects and sounds.** There are no rise dust effects and no BO1 zombie
  sounds.
- **Testing.**
  - No Windows gameplay test has been done yet.
  - The texture reader was checked against the IWI v13 layout and synthetic
    files, but not against your real `.iwd` images; your computer could not
    be reached during this build.
  - The `BO1_MODELS` line reports how many textures were found and any
    fallbacks.

## Validation

**`bo1_model_tests` (native)**

It checks:

- IWI v13 parsing and mip reordering;
- the specular and gloss merge;
- the zlib writer;
- the `.iwd` (zip) reader, for stored and deflated entries and the CRC check;
- the zone writer, on a synthetic two-model scene (skinned and rigid, a
  collision tree, shared materials and images, the identity normal map);
- the complete build pipeline, with in-memory archives and every fallback
  case.

With the owner's `zombie_theater.ff` (local only, never committed), it also
carves the real Kino character and builds the full fastfile from it.

**Exact-native probe**

[model-zone-test40-probe.json](model-zone-test40-probe.json) comes from
`tools/model_zone_probe.cpp`. It maps the retail x64 `iw4sp.exe` and runs its
own fastfile loader on the generated file:

- `DB_LoadXFile`;
- the stream allocator;
- the XModel, XModelSurfs, Material, technique set and GfxImage loaders.

Only file I/O, memory allocation, asset registration and texture creation
are replaced. The probe then checks every loaded field against the source
data, including:

- block sizes and guard pages;
- vertex and index buffer copies;
- LOD indices, samplers and references.

The committed report covers the synthetic scene. Run locally on the real
Kino character, the retail loader accepted:

- 6 models;
- 52 surfaces;
- 20,774 vertices and 27,972 triangles;
- 16 collision trees;
- 53 resolved references.

**Binding verification**

[zombie-bindings-test40.json](zombie-bindings-test40.json) adds:

- the level-zone call site and the `XZoneInfo` it passes;
- `DB_LoadXAssets`;
- `G_ModelIndex`, the `level.initializing` window and the model configstrings;
- the `setmodel`, `attach` and `detachall` builtins;
- the 192-bone DObj limit;
- the helmet note.

**Earlier suites**

All earlier C++, Python and exact-native suites still pass. The DllMain
patches and the generated capacity tables are unchanged.
