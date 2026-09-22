// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EnemyBase.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UFloatingPawnMovement;
class UHealthComponent;

UCLASS()
class WHISKYCOUNTY_API AEnemyBase : public APawn
{
	GENERATED_BODY()

public:
	AEnemyBase();

	virtual void Tick(float DeltaTime) override;

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Enemy")
	UHealthComponent* GetHealthComponent() const { return Health; }

	// Current chase target (the player pawn). Useful for BP-driven VFX (telegraph beams).
	UFUNCTION(BlueprintPure, Category = "Enemy")
	AActor* GetCurrentTarget() const { return Target; }

	// Ram damage dealt to this enemy when the player rams it (read by VehiclePawn).
	UFUNCTION(BlueprintPure, Category = "Enemy|Ram")
	float GetRamDamageDealt() const { return RamDamageDealt; }

	// Self-damage to the player for ramming this enemy.
	UFUNCTION(BlueprintPure, Category = "Enemy|Ram")
	float GetRamCostToPlayer() const { return RamCostToPlayer; }

	// Hunter & miniboss are immune to the Onda d'urto knockback (design rule).
	UFUNCTION(BlueprintPure, Category = "Enemy|Ram")
	bool IsImmuneToShockwave() const { return bImmuneToShockwave; }

	// |velocity| in cm/s. For walk blendspace speed.
	UFUNCTION(BlueprintPure, Category = "Enemy|Animation")
	float GetMovementSpeed() const;

	// Signed forward speed (cm/s, dot of velocity with actor forward). Positive = forward,
	// negative = backward. Drive a "retreating" anim transition with this < some threshold.
	UFUNCTION(BlueprintPure, Category = "Enemy|Animation")
	float GetForwardSpeed() const;

	// True when the target is within ContactDamageRange and ContactDamage > 0. Drive a
	// melee/swing animation loop with this. False for snipers/hunters (ContactDamage = 0).
	UFUNCTION(BlueprintPure, Category = "Enemy|Animation")
	bool IsAttacking() const { return bIsInAttackRange; }

	// Fired right after BeginPlay finishes for this enemy. Use this to spawn the spawn-in
	// VFX (smoke puff, ground portal, materialization flash, summon ring, etc.) and the
	// appearance sound. Fires for EVERY spawn path — wave director, boss adds, debug spawns.
	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|FX")
	void OnSpawnFX();

	// Fired when the enemy dies, BEFORE Destroy() is called. Override in BP to spawn the
	// death particle / sound at the actor's location.
	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|FX")
	void OnDeathFX(AActor* Causer);

	// Fired when this enemy takes damage (any source). Override to spawn damage numbers,
	// hit-flash on the material, etc.
	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|FX")
	void OnDamagedFX(float Amount, AActor* Causer);

protected:
	virtual void BeginPlay() override;

	// ----- Subclass hooks (override in Chaser/Sniper/Hunter for stagger, rage, etc.) -----
	virtual void UpdateMovement(float DeltaTime);
	virtual void OnDamaged(float Amount, AActor* Causer);
	virtual void OnDeath(AActor* Causer);

	// Helper: find the player pawn and cache it
	void RefreshTarget();

	// Snaps the actor onto the ground via a downward line trace
	void SnapToGround();

	// ===== Components =====
	UPROPERTY(VisibleAnywhere, Category = "Enemy|Components")
	TObjectPtr<UCapsuleComponent> CollisionCapsule;

	UPROPERTY(VisibleAnywhere, Category = "Enemy|Components")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Enemy|Components")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UPROPERTY(VisibleAnywhere, Category = "Enemy|Components")
	TObjectPtr<UHealthComponent> Health;

	// ===== Stats =====
	UPROPERTY(EditAnywhere, Category = "Enemy|Stats", meta = (ClampMin = "0"))
	float MoveSpeed = 600.f;

	// Distance from target where the enemy stops moving (also melee stop-distance).
	UPROPERTY(EditAnywhere, Category = "Enemy|Stats", meta = (ClampMin = "0"))
	float AcceptanceRadius = 150.f;

	// Yaw rotation speed when turning toward the target (deg/s)
	UPROPERTY(EditAnywhere, Category = "Enemy|Stats", meta = (ClampMin = "0"))
	float TurnRateDeg = 360.f;

	UPROPERTY(EditAnywhere, Category = "Enemy|Stats")
	bool bSnapToGround = true;

	UPROPERTY(EditAnywhere, Category = "Enemy|Stats", meta = (ClampMin = "0"))
	float GroundTraceOriginUpOffset = 100.f;

	UPROPERTY(EditAnywhere, Category = "Enemy|Stats", meta = (ClampMin = "0"))
	float GroundTraceMaxDrop = 500.f;

	// ===== Ram (damage taken when player rams this enemy mid-dash) =====
	// Damage applied to THIS enemy when the player rams it. Default = insta-kill chaser.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ram", meta = (ClampMin = "0"))
	float RamDamageDealt = 200.f;

	// Self-damage applied to the PLAYER for ramming this enemy. Trade-off cost.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ram", meta = (ClampMin = "0"))
	float RamCostToPlayer = 10.f;

	// True for hunter / miniboss — they don't get pushed by the shockwave on-kill.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ram")
	bool bImmuneToShockwave = false;

	// ===== Crowd separation (boid-style soft repulsion) =====
	// Distance at which neighbours start pushing each other apart. ~capsule diameter is a good baseline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Crowd", meta = (ClampMin = "0"))
	float SeparationRadius = 130.f;

	// Strength of the separation input vs the seek input. 0 = no separation, 1 = same magnitude as seek.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Crowd", meta = (ClampMin = "0", ClampMax = "2"))
	float SeparationStrength = 0.6f;

	// ===== Contact attack (chaser-style melee) =====
	// Damage per tick when the target is within ContactDamageRange. Set 0 to disable (e.g. snipers).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat", meta = (ClampMin = "0"))
	float ContactDamage = 10.f;

	// Seconds between consecutive contact hits while staying in range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat", meta = (ClampMin = "0.05"))
	float ContactDamageInterval = 1.0f;

	// Max distance from the enemy origin to the target's collision SURFACE for a contact hit.
	// Use a small value (~enemy capsule radius + small buffer) since this is surface-based, not center-to-center.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat", meta = (ClampMin = "0"))
	float ContactDamageRange = 80.f;

	// ===== Navigation (NavMesh path-following) =====
	// When true, UpdateMovement steers toward the next waypoint of a NavMesh path query
	// (lets enemies route around walls). When false, falls back to direct seek (legacy
	// straight-line movement — leave on for shipping; the toggle is for A/B debug).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Navigation")
	bool bUseNavMeshPathing = true;

	// How often the NavMesh path is re-queried (seconds). 0.25 = 4 Hz, low CPU cost. Lower
	// if the player moves very erratically and you want enemies to react faster, raise if
	// you have many enemies and the queries become a profiler hot spot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Navigation", meta = (ClampMin = "0.05"))
	float NavPathRequeryInterval = 0.25f;

	// If the desired destination (anchor on the ring around the player) moves more than
	// this between requeries, force an immediate re-query (so we don't keep chasing a
	// stale path while the player drives away).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Navigation", meta = (ClampMin = "0"))
	float NavTargetMoveThreshold = 200.f;

	// Distance below which a waypoint is considered reached and we advance to the next one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Navigation", meta = (ClampMin = "10"))
	float NavWaypointReachDist = 100.f;

	// Draw the active path as red line segments. Cheap, useful for debugging missing
	// NavMeshBoundsVolume coverage.
	UPROPERTY(EditAnywhere, Category = "Enemy|Navigation|Debug")
	bool bDrawNavDebug = false;

	// ===== State =====
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Enemy|State")
	TObjectPtr<AActor> Target;

private:
	UFUNCTION()
	void HandleHealthDamaged(UHealthComponent* HealthComp, float Amount, float NewHealth, AActor* Causer);

	UFUNCTION()
	void HandleHealthDeath(UHealthComponent* HealthComp, AActor* Causer);

	void TryContactAttack(float DeltaTime);
	void ApplySeparation();

	float ContactAccum = 0.f;
	bool  bIsInAttackRange = false;

	// Per-instance angular slot around the target. Assigned once in BeginPlay so each enemy
	// claims a different position on the ring around the player instead of all stacking on
	// the same point.
	float AngularOffsetRad = 0.f;

	// ===== NavMesh path cache (transient runtime state) =====
	// PathPoints from the last successful FindPathToLocationSynchronously call. We follow
	// these waypoints in order; stale points get popped as we reach them.
	TArray<FVector> CachedNavPath;

	// Counts down each tick; when <= 0 we re-query the path. Reset in two places: timer expired
	// or target moved more than NavTargetMoveThreshold since the last query.
	float NavRequeryTimer = 0.f;

	// World-space position the path was last queried TO (the anchor). If the player moves
	// far from here, force an immediate re-query.
	FVector LastQueriedDestination = FVector::ZeroVector;

	// Helper called from UpdateMovement: queries the nav system if needed and pops reached
	// waypoints. Returns the world point we should currently steer toward.
	FVector GetNextNavSteeringPoint(const FVector& DesiredDestination, float DeltaTime);
};
