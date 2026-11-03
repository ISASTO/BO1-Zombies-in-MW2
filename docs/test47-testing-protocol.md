# Test47 install and test

Build **0.1.46-zombies-test47**. It fixes the powerup glow and the hellhounds, and adds Spawn Zombie and allies in zombie mode.

## Install

1. Close MW2 and extract the complete Test47 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test47 folder. This removes Test46.
3. Run **Install.cmd** from the Test47 folder.

The first start rebuilds the BO1 model file (the powerup bones changed).

## Run A: Favela, powerups

Start zombies on **Favela** (F4 → ZOMBIE MODE → Playable zombies) and play until a few powerups drop.

- Every powerup glows and hums: max ammo, double points, kaboom and insta-kill.
- The F4 BO1 row should read `A94/94 ... H1 F10`.

## Run B: a dog round

Start at round **4** on Favela or Gulag showers and play the dog round.

- **Arrival:** lightning, then the hound. The eye glow should be on its eyes.
- **Close to you,** it should bite (a lunge), not freeze in a stiff pose.
- **When it dies,** it bursts. The body and its fire disappear.

## Run C: the AI page

While zombies run, open F4 → **AI**:

- **Spawn Zombie** (row 3), pressed a few times: each press adds a zombie that rises near you, and the round lasts until it is killed. During a dog round it adds a hellhound.
- **Spawn Friendly:** an ally appears and fights the zombies. It moves with the zombie animations; that is expected for now.

## Send back

Run **CollectLogs.cmd** at the end. Also send:

- **Screenshots or a short clip:** a glowing powerup other than insta-kill, and a hound biting.
- **Say:**
  - whether hounds still go stiff or use zombie animations, and when (climbing, close to you, dying);
  - where the eye glow sits.

## Useful log lines

- `BO1_MODELS state=READY ... powerups=1 hellhound=1 effects=10`
- `BO1_ANIM swap=94/94`
- `ZOMBIE_FX ... on_tag=N refused=0`
- `ZOMBIE_HOUNDS ... deaths=N removed=N menu_zombies=N`
- `ZOMBIE_MENU_SPAWN added=N ...` and `AI_SPAWN_FRIENDLY zombies=1 gate=BYPASS`
