# Test29: crowd rendering budgets and non-finite glass collision

Version: **0.1.28-squad-test29**. Live capacity remains 256 actors / 274
sentients; 64 corpse slots and original MP hitmarker behavior remain included.

## What the returned Test28 run establishes

The installed Test28 DLL matches its delivered hash. At the last completed
spawn there are 99 active actors, 96 owned by the mod. The failure is an invalid
read at `iw4sp.exe+0x1dd323` in a native physics worker. The glass free chain is
consistent: 77 active and 974 free entries account for all 1,051 pieces.

The stack reaches the glass collision callback through a map collision-tree
traversal. Its query already contains NaNs in the origin/rotation and derived
geometry. An unselected feature value of zero underflows through unsigned
arithmetic into vertex index `0x7ffffffc`, despite a three-vertex polygon.
This is a different immediate failure from Test27's allocator free-head read.
It does not establish a different upstream corruption source.

Around the reported flickering range, logged skeleton usage is approximately
301 KiB / 9 MiB client-side and 72 KiB / 8 MiB server-side. Actor counts remain
stable without cleanup cycling. Skeleton exhaustion and deliberate actor
reclamation are therefore weakened explanations. Ten-second samples do not
prove that no transient exhaustion occurs.

The exact renderer contains independent 4.5 MiB skin buffers and a shared
128 KiB surface-record pool. Their overflow branches skip drawing surfaces.
This is a concrete candidate for flickering, not a demonstrated retail cause.

## Rendering changes

The skin cache grows from 4.5 MiB to **72 MiB**, corresponding to 2,359,296
32-byte vertices. The patch coordinates native capacity metadata, both GPU
CreateVertexBuffer requests, their error reporting and both CPU virtual
reservations. Native allocation and reset ownership stay with the engine.
Initialization rejects a late attempt after those resources already exist.
Ordinary startup installs the patch on the first D3D export before device
creation. Device recreation executes the same checked larger requests.

Both skin reservation paths use bounded compare/exchange loops. They reject
vertex-to-byte multiplication overflow, addition wrap and exhausted capacity
without advancing the shared counter. The original failed reservation advanced
that counter even though its drawing work was skipped. CPU commit/decommit
continues to use the native reservation and actual bounded used byte count.

Animated DObj records use a separate **2 MiB guarded buffer per native frame
slot**, with independent atomic counters. The native skinned path stores a full
pointer in sceneDObj+0x48 and carries that pointer into worker commands, but the
draw builders subsequently repack record addresses into 16-bit IDs. Increasing
the original pool alone would wrap those IDs. Test29 retains the 16-bit format:
IDs 0..32767 remain native rigid-record quarter-offsets, while IDs 32768..65535
resolve through a per-frame table to expanded animated records. A guarded
reverse map converts each animated record's full address back to its ID.

Both packed producers and all 22 field/pointer readers use this bridge.
Publication validates the native variable-width record sequence, reserves
handles atomically, and fills mappings before the native worker submission and
function return. Exhaustion or malformed records clear the object's drawable
pointer and follow the native failed-allocation return. Reset happens at the
existing frame-reuse boundary; the two buffers never share live storage.

Rigid-model records keep the unchanged 128 KiB native pool. Those paths encode
quarter-offsets in 16-bit fields, so a blanket larger bound would be unsafe.
Counters will identify that pool if it becomes the next bottleneck.

The renderer's host globals numerically overlap the patch planner's synthetic
arena RVA range. Native host references are explicitly tagged in fixups so
they resolve to the game image, while owned counters/buffers resolve to the
separate arena. The externally relocated native fixture checks this distinction.

Fully exercised skin resources can use approximately 135 MiB more GPU buffer
storage and 135 MiB more CPU address space than the stock pair. CPU pages are
still committed by the native code as needed. The two guarded animated-record,
reverse-map and pointer-table sets add 7 MiB of committed host storage. GPU allocation failures follow
the existing native error path; this build does not silently change graphics
settings or fall back to inconsistent smaller buffers.

## Physics changes and limits

The glass-specific world-entry path checks finite native origin, rotation and
bounds before traversing map geometry. Invalid glass poses return no contacts
for that query, without sending NaNs through the collision tree. Other geometry
types retain their original world-entry path.

A second gate checks the completed glass descriptor before triangle work.
Pointer fields are excluded from numeric checks. The generic polygon collision
path also rejects zero/out-of-range feature selectors before the index
arithmetic responsible for the returned fault.

These checks prevent the reproduced invalid read. They do **not** repair the
unknown physics update that first produced NaNs. An invalid shard query is
skipped; valid actor collisions and normal finite glass queries retain their
native behavior. The first invalid descriptor, source/context pointers and
stage are retained for diagnosis. Stage 1 is before world traversal, stage 2
is the completed narrowphase query. At stage 1, derived fields not yet built
must not be interpreted as completed geometry.

Thirty-two new body relays have standalone unwind descriptions derived from the
exact native frames. They neither change RSP nor call external code. The
warning counter entry is a leaf that preserves the native argument spills.
All 36 non-leaf relay entries, including the four retained relays, are registered before publishing hooks.
Allocation, byte validation, protection or unwind-registration failure leaves
the original native code intact and releases unpublished buffers.

## Diagnostics and retest

AI_RENDER_CROWD reports skin peaks, surface usage, failures and frame resets.
AI_RENDER_OVERFLOW counts 64 native warning categories before their normal
message throttle; additional nonzero categories appear as AI_RENDER_WARNING
in ordinary logs. AI_RENDER_HANDLES reports per-frame handle usage and failures.
AI_PHYSICS_GUARDS reports rejected inputs and capture state;
AI_PHYSICS_FIRST_BAD_GLASS preserves raw origin/axis bits without repeating a
message per rejected contact. Hot paths do no file writes, heap allocation or
whole-pool scans. The existing AI info action prints counters immediately.

Crash-only capture adds both renderer frame counters, GPU metadata, owned
diagnostics, bounded surface records/reverse maps/pointer tables and the first bad glass source/reference. The original per-crash
log and native event ring remain available.

The [testing protocol](test29-testing-protocol.md) separates a no-combat crowd
run, glass/gunfire tests and mission/save/reload checks. Stop adding actors at
the first flicker and collect its count/camera behavior before pursuing a crash.

The [native crowd probe](crowd-test29-probe.json) reproduces the returned read
at the same instruction, the stock budgets and failed-counter overshoot. It
checks matching CPU/GPU creation sizes, 320 actor/corpse allocation loads per
frame, concurrent reservations, exhaustion/wrap, finite and non-finite inputs,
feature-selector bounds, native frame isolation and first-failure retention.
It also passes 8,000 concurrent handle publications and 1,344 comparisons of
both packed producers and all bridged readers against the original native code,
checking every GPR, SIMD register and relevant flag. The executable scan accounts
for all 28 direct surface-representation sites; rigid-only writers remain native.
Recording substitutes stand in for D3D/OS services and world callbacks. Windows
GPU rendering, real actor assets/animation, physics integration and actual
RtlVirtualUnwind are not executed by this fixture. Existing native population,
checkpoint, animation, iterator, history, perception, threat, skeleton and glass
lifetime regressions are also retained.

Implementation references: Microsoft's [CreateVertexBuffer contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createvertexbuffer)
and [x64 exception/unwind specification](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
