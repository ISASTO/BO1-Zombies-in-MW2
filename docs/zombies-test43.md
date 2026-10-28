# Test43: the zombie skeletons, fixed, and BO1's zombie sounds

Version **0.1.42-zombies-test43**. Everything else is as in Test42.

## The mangled zombies

Test42 was the first test where the BO1 zombie models showed up in game. Their
limbs were folded and stretched.

**What went wrong.** A model stores, for every bone, where it sits relative to
its parent: a rotation and a translation. Black Ops reserves 16 bytes per bone
for the translations. It fills only the first 12 bytes of each bone's 16, all
packed together at the start, and leaves the rest zero.

The importer read them as one bone per 16 bytes, so:

- most bones got another bone's offset;
- a quarter of them got zeros.

MW2 rebuilds each animated bone from these offsets, so the limbs pointed the
wrong way and had the wrong lengths.

**The fix.**

- The translations are read packed, 12 bytes per bone. MW2 reads them the same
  way (checked on MW2's own `defaultactor`).
- Every converted model is now checked before use:
  - the bones are chained parent to child from the stored offsets;
  - the result must land on the model's stored rest pose (`baseMat`), within
    0.05 units.
- Before the fix, the worst bone was 54.8 units off. Now all six Kino models
  match exactly. A model that fails the check is not converted, and zombies
  keep MW2 models.
- The cached model file is rebuilt automatically, because its builder version
  changed.

## BO1's zombie sounds

Zombies now sound like Black Ops zombies.

### What you hear

**Zombies:**

| Event | BO1 sound |
|---|---|
| Rising | the dirt of the rise |
| Walkers | ambient moans, one at a time, every 1-4 s as BO1's `zombie_moan` |
| Runners and sprinters | sprint moans, at the same cadence |
| Each swing | an attack vocal |
| Each hit or miss | the swing whoosh |
| Within 150 units behind you | the "behind" moan |
| Death | a BO1 death vocal |
| Footsteps | BO1 zombie steps for the surface they walk on (dirt, concrete, metal, wood, water...) |

The zombies no longer use MW2 soldier pain and death voices.

**Hellhounds:**

- a lightning bolt and spawn sound when they appear;
- ambient growls, or close growls near you;
- the explosion when they die.

**Powerups:**

- the spawn sound where the powerup drops;
- the grab sound;
- the max ammo, insta-kill and nuke stingers;
- each zombie the nuke kills.

**Rounds:**

- the round start and round over music;
- the dog round start and end music, and the dog round stinger;
- the game over music.

The music depends on finding the music files in your Black Ops install (see
below).

### How it works

Nothing from Black Ops is shipped. On the first start after installing,
`d3d9.dll` builds `bo1mw2\cache\bo1_zombie_sounds.ff` on a background thread.
The build takes a second or two, and the file is reused until a source file
changes.

1. **The sound bank.** It reads BO1's sound bank, `zmb_common.all` in your
   `zone\Common\common_zombie.ff`:
   - 2,287 alias lists;
   - 1,513 sounds stored in the file;
   - 5,211 aliases that share a sound with an earlier one. A shared sound is
     recorded as a pointer into the zone's memory, so the reader replays the
     memory layout to resolve it. All of them resolve.
2. **The music.** The round music aliases are in `zmb_theater.all` (Kino's
   `zombie_theater.ff`). Their files are streamed from your `main\*.iwd`
   archives.
3. **Decoding.** Black Ops stores most sounds as xWMA, which MW2 cannot play.
   The mod decodes them to 16-bit PCM itself:
   - WMA v2 through a C++ port of FFmpeg's decoder (`third_party/wmadec`,
     LGPL 2.1);
   - MS-ADPCM through a small decoder of its own.
4. **What it keeps.** About 50 alias lists and 238 variations (142 sounds,
   about 3 minutes of audio, 19.6 MB). Positional sounds are mixed down to
   mono so MW2 positions them. Each keeps BO1's settings:
   - volume and pitch ranges;
   - distances;
   - start delay.
5. **The MW2 file.** It writes a MW2 fastfile of MW2 sound alias lists, each
   holding its sounds as MW2 loaded sounds. Each alias uses:
   - MW2's own `$default` falloff curve;
   - the default speaker map;
   - the same channel kind as MW2's similar sounds: AI (`auto`), footsteps
     (`body`), effects (`effects1`), or 2D for stingers and music.

**In game.** The sound file loads with each zombies level, before the level
zone, next to the model file. A sound plays exactly as a one-argument
`self playsound(alias)` does:

1. `G_SoundAliasIndex` gives the alias a configstring;
2. `G_AddEvent(EV_SOUND_ALIAS)` sends it to the entity;
3. MW2's own sound system plays it there, with the volume setting, 3D
   position, occlusion and pause behaviour of any other MW2 sound.

**MW2's own animscripts still run on the zombies.** To change what they play,
the mod redirects the `playsound` method:

- footsteps (`step_run_<surface>`) become `fly_step_zombie_<surface>`;
- soldier voices (`generic_pain_*`, `generic_death_*`, ...) are dropped;
- this applies only to zombies, and only to one-argument calls;
- every other call goes to the original method.

**Crash guard.** It works as for the models. If a run ends abnormally while the
sounds are new, the next run skips them once.

## Checks done here

**The decoders.** Compared with FFmpeg 6.1 on your Black Ops files (run
locally, never committed):

- all 1,443 xWMA sounds in `common_zombie.ff` decode within 1 LSB, with
  identical sample counts;
- all 69 MS-ADPCM sounds decode bit for bit.

A committed fixture (a synthetic chirp encoded by FFmpeg) checks the same
decoder in the unit tests. Every loaded sound in both banks (1,513 + 326) also
decodes under AddressSanitizer and UndefinedBehaviorSanitizer.

**The MW2 loader.** The new exact-native probe (`tools/sound_zone_probe.cpp`)
loads the generated file with the retail x64 loader code, unmodified:

- `DB_LoadXFile` and the stream allocator;
- `Load_snd_alias_list_ptr`, the alias, sound file and loaded-sound loaders;
- the curve and speaker map loaders.

Then it checks:

- every alias, sound file, loaded sound, curve reference and speaker map;
- that the samples reached MW2's heap copy;
- that the stream is consumed exactly and the zone blocks are within bounds.

It covers a synthetic zone and the full zombie set.

**The bindings.** `tools/verify_zombie_bindings.py` now also checks:

- the sound asset types;
- `G_SoundAliasIndex` and its 256-slot configstring ring;
- `G_AddEvent`;
- the `playsound` thunk and its one-argument path;
- `Scr_GetString` and `Scr_GetNumParam`;
- entity references.

**Unit tests.** `tests/bo1_sound_tests.cpp` covers:

- the bit reader and VLCs;
- the IMDCT, against its definition;
- the WMA fixture;
- MS-ADPCM, against an encoder;
- a synthetic T5 bank with shared sounds and a streamed one;
- the zone writer.

The Python tests check:

- that every alias the game plays is built;
- that the sound zone loads before the level zone;
- that the `playsound` hook holds nothing a script error could skip past.

## Not covered offline

- **Volumes.** How loud the BO1 sounds are next to MW2's own needs listening
  in game.
- **The round music.** It depends on the music files being in your `.iwd`
  archives. The log line `BO1_SOUNDS state=READY ... music_found=N` reports
  it. If the files are not there, everything else still plays.

**Testing:** no Windows gameplay test of Test43 yet.
