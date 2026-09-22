// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "EnemyHunter.generated.h"

UENUM(BlueprintType)
enum class EHunterState : uint8
{
	Pursuing  UMETA(DisplayName = "Pursuing"), // standard seek behavior, looking for a chance to charge
	Charging  UMETA(DisplayName = "Charging"), // standing still, telegraphing the lunge direction
	Lunging   UMETA(DisplayName = "Lunging"),  // dashing along a locked direction at LungeSpeed
	Recovery  UMETA(DisplayName = "Recovery"), // post-lunge vulnerability window
};

UCLASS()
class WHISKYCOUNTY_API AEnemyHunter : public AEnemyBase
{
	GENERATED_BODY()

public:
	AEnemyHunter();

	// ===== Animation drivers (read by AnimBP) =====
	UFUNCTION(BlueprintPure, Category = "Hunter|Animation")
	EHunterState GetHunterState() const { return State; }

	// True during the windup — plays the charge / telegraph anim (e.g. growl + paw-stomp).
	UFUNCTION(BlueprintPure, Category = "Hunter|Animation")
	bool IsCharging() const { return State == EHunterState::Charging; }

	// True during the actual dash — plays the sprint animation.
	UFUNCTION(BlueprintPure, Category = "Hunter|Animation")
	bool IsLunging() const { return State == EHunterState::Lunging; }

	// ===== FX hooks (override in BP to spawn VFX/sound) =====
	// Fired when the hunter enters Charging — windup VFX (red aura, growl sound).
	UFUNCTION(BlueprintImplementableEvent, Category = "Hunter|FX")
	void OnEnterCharging();

	// Fired when the hunter leaves Charging (transitioning to Lunging). Stop the windup VFX.
	UFUNCTION(BlueprintImplementableEvent, Category = "Hunter|FX")
	void OnExitCharging();

	// Fired when the hunter starts the dash — trail/dust VFX, roar sound. Direction is the
	// locked lunge direction (horizontal unit vector).
	UFUNCTION(BlueprintImplementableEvent, Category = "Hunter|FX")
	void OnEnterLunging(const FVector& LungeDir);

	// Fired when the lunge ends (hit something or duration expired). Stop the dash VFX.
	UFUNCTION(BlueprintImplementableEvent, Category = "Hunter|FX")
	void OnExitLunging();

	// Fired RIGHT BEFORE the hunter is destroyed (kamikaze explosion). AOE damage already
	// applied at this point. Spawn the explosion Cascade + sound + camera shake here.
	// Origin = explosion center, AffectedCount = pawns hit by AOE (drive shake intensity).
	UFUNCTION(BlueprintImplementableEvent, Category = "Hunter|FX")
	void OnHunterExploded(const FVector& Origin, int32 AffectedCount);

protected:
	virtual void BeginPlay() override;
	virtual void UpdateMovement(float DeltaTime) override;

	// ===== Tunables =====
	// Distance at which the hunter stops pursuing and starts charging.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter", meta = (ClampMin = "0"))
	float LungeRange = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter", meta = (ClampMin = "0.05"))
	float ChargeDuration = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter", meta = (ClampMin = "0.05"))
	float LungeDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter", meta = (ClampMin = "0"))
	float LungeSpeed = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter", meta = (ClampMin = "0"))
	float RecoveryDuration = 2.0f;

	// Damage applied to each pawn inside ExplosionRadius when the hunter detonates. Reusing
	// the old "LungeImpactDamage" name preserves any tuned BP defaults — semantically it's
	// now the explosion damage per target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter|Explosion", meta = (ClampMin = "0"))
	float LungeImpactDamage = 30.f;

	// AOE radius (cm) of the kamikaze explosion. "Piccolo" means small — 250 = 2.5m blast.
	// Bumps it to 350+ for arena-clearing chaos, drops it to 150 for "must touch" precision.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter|Explosion", meta = (ClampMin = "0"))
	float ExplosionRadius = 250.f;

	// If true, the hunter ALSO explodes when its lunge whiffs (timer expires without contact).
	// Pure kamikaze theme: they commit fully to every charge. False = retry mechanic (whiff
	// goes back to Recovery → Pursuing). Default false to give the player breathing room.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter|Explosion")
	bool bExplodeOnLungeWhiff = false;

	UPROPERTY(EditAnywhere, Category = "Hunter|Debug")
	bool bDrawDebugTelegraph = true;

private:
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Hunter|State")
	EHunterState State = EHunterState::Pursuing;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Hunter|State")
	bool bExploded = false;

	float StateTimer = 0.f;
	FVector LungeDirection = FVector::ForwardVector;

	void EnterPursuing();
	void EnterCharging();
	void EnterLunging();
	void EnterRecovery();

	void TickCharging(float DeltaTime);
	void TickLunging(float DeltaTime);
	void TickRecovery(float DeltaTime);

	// Sphere-overlap AOE damage at the hunter's current location, fires OnHunterExploded BIE,
	// then kills self via Health->Kill. Idempotent (bExploded guard).
	void Explode();

	// Capsule overlap handler. If the overlapping actor is the player target, triggers Explode.
	UFUNCTION()
	void HandleCapsuleOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
