# Zombie County

**A top-down roguelike car shooter built in one week, made in Unreal Engine 5.8 with C++.**

<!-- Add a gameplay GIF or screenshot here, e.g.:
![Gameplay](Docs/gameplay.gif)
-->

You drive an armed car through five waves of zombies. You pick a special weapon after the first wave, stack power-ups as the run goes on, and try to survive the final boss.

---

## About this project

Zombie County was a personal study project and a challenge to myself: **build a complete game in one week, doing everything myself.** That covers the design, gameplay programming, AI, vehicle physics, weapons, progression and UI.

The only exceptions were the **environment assets used to dress the map** and the **visual effects packs** (explosions, muzzle flashes, particles, toon shader). They come from [Fab](https://www.fab.com) under licenses that don't allow redistribution, so **I removed them from this repository**. The level will open without environment meshes and without VFX, but all gameplay systems are here and work.

> **Yes, it looks pretty rough without them. But it's now completely free**: no licensed content, nothing you're not allowed to use. Every file in this repo is yours to study, modify and reuse.

I'm publishing it for free for two reasons:

- **As a portfolio piece**, to show how I structure gameplay code in Unreal.
- **To help anyone learning Unreal and C++.** Everything is heavily commented and exposed to Blueprints, so you can read it, take it apart and reuse whatever is useful to you.

---

## Gameplay

- **Top-down roguelike shooter** with a car as the player character.
- **5 waves** of increasing difficulty, ending with a **final boss**.
- **3 enemy types** (a runner, a shooter and a dasher), each with its own behaviour and counter-play.
- **After wave 1** you choose a special weapon: the **Shotgun** or the **Grenade Launcher**.
- **After every later wave** you choose **1 of 2 random power-ups**. Power-ups never repeat, and the two cards on screen never come from the same system.
- **Ram dash**: spend your ram charge on a locked-direction boost that kills enemies on contact. Every hit costs you a bit of health.
- The run ends with a stats screen: survival time, kills and the wave reached.

### Controls

| Input | Action |
|---|---|
| `W` / `S` | Throttle / brake & reverse |
| `A` / `D` | Steer |
| `Space` | Handbrake (drift) |
| `Left Shift` | Ram dash |
| `Left Mouse` | Machine gun (always available, heat-based) |
| `Right Mouse` | Special weapon: the Shotgun fires on press; the Grenade Launcher aims while held and fires on release |
| Mouse | Aim the turret |

### Enemies

| Enemy | Class | Behaviour |
|---|---|---|
| **Runner** | `AEnemyBase` | Swarms the player and deals contact damage. Dies instantly to a ram. |
| **Shooter** | `AEnemySniper` | Keeps its distance, backs away if you get close, and lobs explosive bombs. A ground marker shows where each bomb will land before it's thrown. |
| **Dasher** | `AEnemyHunter` | Stalks you, stops to telegraph a charge, then lunges in a straight line and explodes on contact (a kamikaze). |
| **Boss** | `AEnemyBoss` | Chases at high speed with contact attacks. If you get too far away it telegraphs a parabolic leap and lands with an AOE shockwave. It summons runners at 70% HP and becomes enraged at 50% HP: faster windups, harder hits and more frequent leaps. |

### Power-ups

| Power-up | Effect |
|---|---|
| Upgraded Radiator | Machine gun cools down ~40% faster |
| Ammo+ | +2 special ammo and +2 max ammo |
| Reinforced Chassis | −35% self-damage when ramming |
| Turbocharger | +35% ram recharge speed |
| Mobile Workshop | Slow health regeneration |
| Shockwave | Every kill pushes nearby enemies away |

---

## Technical overview

The whole game logic is written in **C++** (about 6,400 lines in the `WhiskyCounty` module). **Blueprints** are used only for visuals: meshes, animation, VFX, sound and UMG widgets. The C++ classes expose data to Blueprints through `BlueprintPure` getters, and hand off visual moments through `BlueprintImplementableEvent` hooks (`OnSpawnFX`, `OnLanded`, `OnEnterRam`, `OnGrenadeAimTick`, …). Designers and artists can then work entirely in BP without touching gameplay code.

### Architecture

```
Source/WhiskyCounty/
├── VehiclePawn               Player car: custom raycast physics, ram dash, FX state
├── VehicleWeaponComponent    Machine gun + special weapon (shotgun / grenade launcher)
├── HealthComponent           Reusable health, regen, i-frames, damage/death delegates
├── EnemyBase                 Shared enemy: NavMesh steering, crowd separation, contact damage
│   ├── EnemySniper           State machine: Idle → Aiming → Firing → Recovery
│   ├── EnemyHunter           State machine: Pursuing → Charging → Lunging → Recovery
│   └── EnemyBoss             State machine: Pursuing → ChargingLeap → Leaping → Recovery
├── GrenadeProjectile         Player grenade (arc, fuse, AOE)
├── SniperProjectile          Enemy bomb (arc, AOE)
├── WaveDefinition            DataAsset: which enemies, how many, timing, spawn tags
├── WaveDirector              Spawns waves, tracks live enemies, broadcasts run events
├── SpawnPoint                Placeable spawn marker (taggable, can be excluded from random)
└── WhiskyCountyPlayerController  Run flow: rewards, power-ups, HUD, tutorial, game over
```

### Vehicle physics (custom, not Chaos Vehicles)

The car is a physics-simulated box driven by **custom raycast suspension** rather than Unreal's Chaos Vehicle plugin. I wanted arcade handling that I fully controlled.

- **Per-wheel line traces** with spring/damper forces, plus an **anti-roll bar** that transfers load between left and right wheels.
- **Throttle, brake and reverse forces** with rolling resistance, and engine RPM that spools up and down over time.
- **Lateral grip** that drops at high speed, and a **handbrake** that cuts rear grip for drifting.
- **Speed-sensitive steering**: less steering angle at high speed.
- The pawn exposes speed, steer angle, RPM, wheel spin, suspension offset and turret yaw, so the AnimBP can animate the car without any extra logic.

### Weapons

- **Machine gun**: hitscan with spread and a **heat / overheat** system. After an overheat it only fires again once the heat has dropped to a set threshold.
- **Shotgun**: multi-pellet hitscan cone, ammo-limited.
- **Grenade launcher**: hold to aim and release to fire. The launch velocity is solved with `UGameplayStatics::SuggestProjectileVelocity_CustomArc` so the grenade lands exactly on the (range-clamped) mouse point. A **live trajectory preview** (sampled path + landing point) is exposed to BP for a Niagara ribbon or ground marker.
- Special ammo is refilled at the start of every wave.

### Enemy AI

Enemies don't use Behavior Trees. Each one is a small **explicit state machine in C++**, which keeps the behaviour easy to read, debug and tune for a project this size.

- **NavMesh path following**: paths are re-queried at a fixed rate (4 Hz by default), and also immediately whenever the target moves too far. Enemies route around walls without an AIController.
- **Ring-slot targeting**: each enemy claims a different angle around the player, so the horde surrounds you instead of stacking on a single point.
- **Boid-style separation**: a soft repulsion between neighbours keeps the crowd readable.
- **Telegraphs for every dangerous attack**: the shooter's bomb landing point, the dasher's charge direction and the boss's leap landing are streamed to BP every frame, so the player always gets a fair warning.
- **Animation-driven damage**: the boss's melee and the shooter's throw fire from **AnimNotifies**, with timeout fallbacks in case a notify is missing.

### Wave & run system

- Waves are **`UWaveDefinition` DataAssets**. Each one is a list of entries (enemy class, count, spawn interval, start delay and an optional spawn-point tag), so you can rebalance waves without touching code.
- `AWaveDirector` spawns the entries on timers and tracks live enemies per class. It broadcasts `OnWaveStarted`, `OnWaveEnded`, `OnEnemyKilled`, `OnEnemyDied`, `OnLiveEnemyCountChanged` and `OnAllWavesCompleted`, and it also keeps the survival timer.
- `AWhiskyCountyPlayerController` listens to those events and drives the roguelike loop: weapon choice after wave 1, power-up rolls afterwards, applying power-up effects, the optional tutorial screen, and the death / victory UI.

### Design principles I followed

- **All tuning values are `UPROPERTY`s** with clamps and a comment explaining what the number means in gameplay terms.
- **Gameplay in C++, presentation in Blueprints**, connected through events rather than hard references.
- **Reusable components**: `UHealthComponent` is shared by the player, every enemy and the boss.
- **Debug toggles** on most systems (NavMesh path draw, telegraph draw, suspension debug, weapon debug).

---

## Getting started

### Requirements

- **Unreal Engine 5.8**
- **Visual Studio 2022** with the "Game development with C++" workload. The `.vsconfig` in the repo lists the required components.

### Build & run

1. Clone the repository.
2. Right-click `ZombieCounty.uproject` and choose **Generate Visual Studio project files**.
3. Open `ZombieCounty.sln`, set the configuration to **Development Editor | Win64**, and build.
   Alternatively, double-click the `.uproject` and let Unreal compile the module for you.
4. The project starts on `Content/Level/Test_Menu`.

> **Note:** the map's environment art and the VFX (Fab assets) are not included, so the level will show missing meshes and the explosions, shots and trails have no particles. Gameplay, enemies, weapons and UI all work. You can drop in any environment you like, as long as it has a **NavMeshBoundsVolume** covering the play area and some `SpawnPoint` actors. To bring the effects back, hook your own Niagara or Cascade systems into the `On...FX` Blueprint events (for example `OnExplode`, `OnPrimaryFired` and `OnEnterRam`).

---

## Credits

- **Design, programming, gameplay, AI, vehicle physics and UI:** me.
- **Map environment assets and VFX packs:** from [Fab](https://www.fab.com). They were used during development and removed from this repository because of their licenses.

If this project helped you learn something, or you reuse part of it, I'd love to hear about it. A mention or a star is always appreciated.
