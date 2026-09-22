// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "EnemyBoss.generated.h"

// State machine: the boss has TWO attacks.
//   1) Pursuing  — chases the player at high speed and damages them on contact (chaser-style).
//   2) Leaping   — when the player gets too far away, the boss telegraphs a parabolic jump and
//                  lands with an AOE shockwave. This is the long-range gap closer.
// The melee swing was removed: with a fast vehicle the player could just drive away during
// the windup, so the swing never connected. Continuous contact damage is the close-range answer.
UENUM(BlueprintType)
enum class EBossState : uint8
{
	Pursuing       UMETA(DisplayName = "Pursuing"),       // run toward player + contact damage active
	ChargingLeap   UMETA(DisplayName = "Charging Leap"),  // windup, telegraph the landing reticle
	Leaping        UMETA(DisplayName = "Leaping"),        // mid-air parabolic flight (kinematic)
	Recovery       UMETA(DisplayName = "Recovery"),       // post-leap vulnerability window (no movement, no damage)
};

UCLASS()
class WHISKYCOUNTY_API AEnemyBoss : public AEnemyBase
{
	GENERATED_BODY()

public:
	AEnemyBoss();

	// ===== Animation drivers (read by AnimBP) =====
	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	EBossState GetBossState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	bool IsChargingLeap() const { return State == EBossState::ChargingLeap; }

	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	bool IsLeaping() const { return State == EBossState::Leaping; }

	// True when the boss is actively chasing AND the player is in contact-damage range.
	// Drive a "swinging arms / biting" loop with this in the AnimBP.
	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	bool IsPursuingInContactRange() const { return State == EBossState::Pursuing && bInPursueContactRange; }

	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	bool IsInPhase2() const { return bPhase2; }

	// 0..1 progress through the current leap (read by ribbon trail / motion blur in BP).
	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	float GetLeapProgress() const;

	// Anim-driven pursue damage. Call this from an AnimNotify placed on the "hit frame" of
	// the pursue attack animation (e.g. when the bite/claw connects). The function self-checks
	// state + range + cooldown — safe to call whenever; it'll silently no-op if the conditions
	// aren't met. Replaces the old fixed-timer per-tick damage with anim-driven cadence.
	UFUNCTION(BlueprintCallable, Category = "Boss|Animation")
	void ApplyPursueDamageNow();

	// 0..1 leap recharge: 1 = leap ready, 0 = just used. Drive a glow / aura material parameter
	// in BP (e.g. mesh starts dim and brightens as the leap recharges) for a clear tell.
	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	float GetLeapCooldownNormalized() const;

	// True when the leap can fire right now (cooldown empty). Distance/state checks not included —
	// this is purely for VFX/UI ("ready" indicator).
	UFUNCTION(BlueprintPure, Category = "Boss|Animation")
	bool IsLeapReady() const { return LeapCooldownLeft <= 0.f; }

	// ===== FX hooks (override in BP_Boss to spawn Cascade emitters + sounds) =====
	// Boss starts the leap windup. TargetLocation = where the boss is currently aimed; it may
	// re-aim during the windup, so the LANDING is locked at OnEnterLeaping, not here.
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnEnterChargingLeap(const FVector& TargetLocation);

	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnExitChargingLeap();

	// Fired EVERY FRAME during the leap windup with the current predicted landing point
	// (= player's live position, since leap locks at end of windup). Wire this to a ground
	// marker BP that follows the player so they see "if I stay here I'll get hit". The
	// marker is hidden on OnExitChargingLeap. PredictedLanding is in world space.
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnLeapTelegraphTick(const FVector& PredictedLanding);

	// Boss launches into the air. Apex = highest point of the parabola, Landing = locked target.
	// Spawn the trail VFX here; destroy it on OnLanded.
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnEnterLeaping(const FVector& Apex, const FVector& Landing);

	// Boss touches down. Damage is already applied. Spawn shockwave emitter + camera shake here.
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnLanded(const FVector& LandingLocation);

	// Boss landed a per-tick contact damage hit on the player while pursuing. Use for hit-flash
	// / impact sound / damage numbers. Fires every PursueContactDamageInterval seconds while in range.
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnPursueContactHit(AActor* HitTarget, float DamageDealt);

	// Fired exactly once when health crosses Phase2HealthFraction. Spawn the rage VFX/sound,
	// e.g. red aura material on the mesh + battle-cry audio.
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnEnterPhase2();

	// Fired exactly once when health crosses AddSpawnHealthFraction. Use for "summon" VFX:
	// shockwave at boss location, scream sound, brief screen flash. The adds in the array are
	// already spawned and active. Independent from Phase 2 (different threshold + tunables).
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|FX")
	void OnSpawnAdds(const TArray<AEnemyBase*>& SpawnedAdds);

protected:
	virtual void Tick(float DeltaTime) override;
	virtual void UpdateMovement(float DeltaTime) override;
	virtual void OnDamaged(float Amount, AActor* Causer) override;
	virtual void OnDeath(AActor* Causer) override;

	// ===== Damage tunables (all damage values in one place for easy designer tuning) =====
	// Damage dealt to player when the boss lands a leap (AOE inside LeapImpactRadius).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Damage", meta = (ClampMin = "0"))
	float LeapImpactDamage = 25.f;

	// Damage applied per pursue-attack swing (one swing = one ApplyPursueDamageNow call,
	// triggered by an AnimNotify on the hit frame of the attack animation). Cadence is
	// determined by the animation length, not by a fixed timer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Damage", meta = (ClampMin = "0"))
	float PursueContactDamage = 12.f;

	// Multiplier applied to LeapImpactDamage AND PursueContactDamage when boss enters phase 2
	// (below Phase2HealthFraction). 1.4 = +40% damage. Set to 1.0 for no scaling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Damage", meta = (ClampMin = "1"))
	float Phase2DamageMultiplier = 1.4f;

	// Damage TAKEN by the boss when player rams it. Inherited from EnemyBase. Re-declared here
	// so it appears in the Boss|Damage category alongside the other damage values. The C++
	// constructor sets it to 60 (boss-specific override of the chaser's higher default).
	// NOTE: this is the same field as Enemy|Ram > Ram Damage Dealt — editing one edits the other.
	// (Listed here for clarity — actual storage is the inherited UPROPERTY.)

	// ===== Leap tunables =====
	// Player must be at least this far for the boss to consider leaping (closer = stay in pursue).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0"))
	float LeapTriggerDistance = 1100.f;

	// Max horizontal distance the boss will leap. Beyond this it stays in pursue (and walks closer).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0"))
	float LeapMaxRange = 4000.f;

	// Telegraph time before lift-off. Player needs this window to dodge the predicted landing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0.05"))
	float LeapWindupDuration = 1.0f;

	// Total airborne time during the parabolic flight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0.1"))
	float LeapTravelDuration = 0.9f;

	// Apex height (cm) above the higher endpoint. Affects how dramatic the arc looks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0"))
	float LeapApexHeight = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0"))
	float LeapImpactRadius = 350.f;

	// Cooldown between consecutive leaps. The countdown starts the instant the boss lands and
	// ticks during Recovery + Pursuing (NOT during the next leap's windup or travel). Higher
	// values give the player a real window to fight back in pursue mode before the next leap.
	// Total leap-to-leap interval is roughly: Recovery + LeapCooldown + LeapWindupDuration + LeapTravelDuration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Leap", meta = (ClampMin = "0"))
	float LeapCooldown = 4.5f;

	// ===== Pursue (anim-driven contact behavior) =====
	// Defensive minimum interval between two ApplyPursueDamageNow successful hits. Prevents
	// double-hits if an anim glitches or is too short. The "real" cadence is set by the
	// animation length + notify position; this is just a safety floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Pursue", meta = (ClampMin = "0"))
	float PursueDamageMinInterval = 0.4f;

	// Distance at which the boss enters "in contact range" (drives IsPursuingInContactRange,
	// which the AnimBP uses to enter the attack state). Should be >= AcceptanceRadius + a bit
	// so the boss starts swinging the moment it reaches its anchor on the ring around the player.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Pursue", meta = (ClampMin = "0"))
	float PursueContactDamageRange = 280.f;

	// ===== Recovery tunables (post-leap vulnerability) =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Recovery", meta = (ClampMin = "0"))
	float LandingRecoveryDuration = 0.7f;

	// ===== Phase 2 tunables =====
	// Below this fraction of MaxHealth, boss enters phase 2 (faster windups, harder hits).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase2", meta = (ClampMin = "0", ClampMax = "1"))
	float Phase2HealthFraction = 0.5f;

	// Multiplier applied to all windup durations in phase 2. 0.65 = 35% faster.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase2", meta = (ClampMin = "0.1", ClampMax = "1"))
	float Phase2WindupMultiplier = 0.65f;

	// Phase2DamageMultiplier lives in Boss|Damage so all damage values are tunable in one place.

	// Multiplier applied to LeapCooldown in phase 2. 0.6 = 40% shorter recharge → boss leaps
	// more often when enraged. Pure tuning lever; set to 1.0 to keep the same cadence.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase2", meta = (ClampMin = "0.1", ClampMax = "1"))
	float Phase2LeapCooldownMultiplier = 0.6f;

	// ===== Adds (summon chasers below an HP threshold) =====
	// Below this fraction of MaxHealth, the boss spawns AddSpawnCount enemies of AddSpawnClass
	// in a ring around itself. Set above Phase2HealthFraction for a "70% adds → 50% rage" arc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Adds", meta = (ClampMin = "0", ClampMax = "1"))
	float AddSpawnHealthFraction = 0.7f;

	// How many adds to spawn when the threshold is crossed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Adds", meta = (ClampMin = "0", ClampMax = "20"))
	int32 AddSpawnCount = 5;

	// Class to spawn (typically BP_Chaser). Assign in BP_Boss defaults.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Adds")
	TSubclassOf<AEnemyBase> AddSpawnClass;

	// Distance (cm) from the boss at which adds appear, evenly distributed on a circle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Adds", meta = (ClampMin = "100"))
	float AddSpawnRadius = 600.f;

	UPROPERTY(EditAnywhere, Category = "Boss|Debug")
	bool bDrawDebugTelegraph = true;

private:
	// ===== Runtime state =====
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Boss|State")
	EBossState State = EBossState::Pursuing;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Boss|State")
	bool bPhase2 = false;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Boss|State")
	bool bAddsSpawned = false;

	float StateTimer       = 0.f;
	float LeapCooldownLeft = 0.f;

	// Time of the last successful ApplyPursueDamageNow call (world-time seconds). Used to
	// enforce PursueDamageMinInterval as a safety against spam-hits from short anims.
	float LastPursueHitTime    = -1000.f;
	bool  bInPursueContactRange = false;

	// Snapshots taken at lift-off, frozen for the whole flight.
	FVector LeapStartLoc  = FVector::ZeroVector;
	FVector LeapTargetLoc = FVector::ZeroVector;

	// Weak refs to adds spawned by SpawnAdds. Used by OnDeath to wipe surviving adds when
	// the boss dies (the adds are the boss's "summons" — losing the master ends them too).
	// Weak ptrs auto-null when an add dies naturally, so iteration always sees only live ones.
	TArray<TWeakObjectPtr<AEnemyBase>> TrackedAdds;

	// ===== State transitions =====
	void EnterPursuing();
	void EnterChargingLeap();
	void EnterLeaping();
	void EnterRecovery(float Duration);

	// ===== Per-state ticks =====
	void TickPursuing(float DeltaTime);
	void TickChargingLeap(float DeltaTime);
	void TickLeaping(float DeltaTime);
	void TickRecovery(float DeltaTime);

	// True if the boss should leap right now (player far enough + cooldown ready).
	bool ShouldLeapNow() const;

	// Updates bInPursueContactRange every tick (drives IsPursuingInContactRange for AnimBP).
	// Damage is NOT applied here — it's anim-driven via ApplyPursueDamageNow.
	void UpdatePursueRangeState();

	// AOE damage centered on landing point.
	void ApplyLandingDamage(const FVector& LandingLoc);

	// Spawns AddSpawnCount enemies in a ring around the boss. Called from OnDamaged once.
	void SpawnAdds();

	// Phase-2 helpers.
	float ScaledWindup(float Base) const { return bPhase2 ? Base * Phase2WindupMultiplier : Base; }
	float ScaledDamage(float Base) const { return bPhase2 ? Base * Phase2DamageMultiplier : Base; }

	// True when the boss should be considered "in air" (leap suspends ground snap).
	bool IsAirborne() const { return State == EBossState::Leaping; }
};
