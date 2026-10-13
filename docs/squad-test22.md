# Test22: original MP hitmarkers and experimental 256 AI

Version: `0.1.21-squad-test22`.

Install the complete ZIP with MW2 closed: uninstall the previous wrapper, then
run `Install.cmd` and select the game folder. The installer backs up `players`
under `bo1mw2/backups/before-test22-*` before copying the DLL. Original game
executables, fastfiles, IWDs and player files are not patched on disk.

## Hitmarkers

WEAPONS → Hitmarkers uses the original `damage_feedback` image and
`MP_hit_alert` / `misc/mp_hit_indication_3c.wav` sound from the owner's MP files.
The installer extracts `images/damage_feedback.iwi` from the game's local
`main/*.iwd` into `bo1mw2/assets`. The DLL validates the MW2 IWI version, DXT5
format, 36×72 base level and file bounds before creating the texture. There is
no drawn-X replacement. Missing assets/audio keep the option unavailable.

MP's ordinary hit feedback uses a 24×48 HUD rectangle at (-12,-12), centered
in the 480-unit HUD coordinate system, with immediate one-second linear fade.
The crosshair vertical-offset dvar is applied. Confirmed player damage triggers
feedback; blocked/zero damage and recycled targets do not. Lethal hits count.
The native SP damage hook supplies actor damage events rather than an MP
player callback. MP-specific perks, scavenger pickups and split-screen/third-person
camera calculations are not transplanted into SP.

The original PCM click is embedded, with the alias's 0.93 gain, pitch 1 and the
live `snd_volume` setting. Eight prepared waveOut buffers avoid file reads,
allocation or thread creation on each hit. Multiple damage events between
render callbacks coalesce into one displayed/sounded update. Device resets
recreate the real texture; the existing D3D state restoration remains in place.

See [asset evidence](mp-feedback-test22-evidence.json). Complete decrypted
scripts/zones and the supplied game files are excluded from the repository/ZIP.

## Native 256 attempt

This build ships activation code, rather than just a private layout prototype.
The proxy already initializes the exact-build engine profile before forwarding
its first D3D entry point. Test22 activates there only after confirming idle
actor/sentient pools and all 206 original instruction fragments. It allocates a
separate 8 MiB nearby arena, relocates checked references, then publishes capacity.
Synthetic fixture RVAs are never used as live game storage.

The layout has 256 actors, 274 sentients, 64 corpse slots and 336 temporary
animation trees. Actor stride is 0x48c0: original brain fields retain their
offsets, while 274 perception records are appended. Direct pools, traversal
strides/spans, index divisions, perception consumers, reference heads and
animation creation/cleanup are updated together. A shared 17,412-byte snapshot
uses 16-bit entity-to-actor/corpse indices, 320 goal records and all three
client readers. Unrelated vehicle/FX counts remain 32.

Native checkpoint segment 3 receives a versioned population header. Native
MemFile transport handles that header and the expanded records. Expanded actor
records use a complete descriptor/scratch buffer without enlarging the original
save function's fixed stack temporary. Untagged stock checkpoints reopen the
native segment, decode 32 actors/50 sentients with stock descriptors, then migrate
perception into the appended layout. Wrong expanded versions are rejected.

The AI page displays the actual capacity. Successful activation logs:
`AI_CAPACITY_EXPANSION enabled=1 experimental=1 actors=256 sentients=274`.
Validation/allocation/idle failures leave capacity at 32 and log a blocking reason.
The existing squad ownership fix, save adoption, invincibility, friendly fire,
held semi-auto fire, recoil and world-speed controls remain included. Relocated
arena reads use the same checked permanent-copy approach as the performance fix.

Expanded checkpoints require Test22's expanded reader. Returning to stock/older
builds requires restoring the pre-Test22 player/save backup. A manual
`bo1mw2/use-stock-ai.flag` disables startup activation; this build rejects an
expanded checkpoint when running at stock capacity instead of parsing it as a
stock stream. Remove the flag and restart to attempt expansion again.

## Verification and next game test

18 C++ suites and 28 Python tests pass. Original profile and combat bindings
match the supplied SP executable. The Windows AMD64 proxy builds successfully.
The installer/diagnostic PowerShell scripts parse; isolated install/uninstall
checks cover original-asset extraction, receipt/DLL hashes and player backup.

The exact-native Linux MS-ABI fixture executes actor/sentient allocation,
animation startup/cleanup, client snapshots, linked sentient handles and native
G_Save/G_Load actor/sentient loops. It passes both with the synthetic fixture
arena and with the checked runtime plan relocated outside the main image:

- 256 actors / 274 sentients restored; 70,144 perception node references;
- 256 client team lookups and 320 actor/corpse goal pairs;
- 658 animation trees created/cleared and 336 temporary leases;
- all 274 linked sentient handles encoded, restored and released;
- 32-actor legacy population migration.

[Exact native results](expanded-runtime-test22-probe.json) list every isolated
engine checkpoint domain and substituted dependency. Transport uses the actual
native uncompressed MemFile/RLE paths. A booted Windows engine, compressed retail
checkpoint streams, game audio/render appearance and crowded-AI performance have
not been run here. These checks do not establish retail crash-free operation.

For the next game test, first confirm the AI count row shows `/ 256`, then test
32, 64 and 128 actors before approaching 256. Exercise enemy waves, saves,
death/checkpoint reload, mission transitions and Remove Spawned Actors. Use
`CollectLogs.cmd` afterward; activation and checkpoint format logs are included.
