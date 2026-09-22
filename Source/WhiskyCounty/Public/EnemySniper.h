// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "EnemySniper.generated.h"

class ASniperProjectile;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ESniperState : uint8
{
	Idle      UMETA(DisplayName = "Idle"),     // waiting for target to enter EngagementRange
	Aiming    UMETA(DisplayName = "Aiming"),   // telegraph windup, locked to current target band
	Firing    UMETA(DisplayName = "Firing"),   // one-frame transition: spawn projectile
	Recovery  UMETA(DisplayName = "Recovery"), // post-shot vulnerability window
};

UCLASS()
class WHISKYCOUNTY_API AEnemySniper : public AEnemyBase
{
	GENERATED_BODY()

public:
	AEnemySniper();

	// ===== Animation drivers (read by AnimBP) =====
	UFUNCTION(BlueprintPure, Category = "Sniper|Animation")
	ESniperState GetSniperState() const { return State; }

	// True during the windup (Aiming) — plays the telegraph / wind-up animation.
	UFUNCTION(BlueprintPure, Category = "Sniper|Animation")
	bool IsAiming() const { return State == ESniperState::Aiming; }

	// True during Firing + Recovery — covers the throw + follow-through animation.
	UFUNCTION(BlueprintPure, Category = "Sniper|Animation")
	bool IsThrowing() const { return State == ESniperState::Firing || State == ESniperState::Recovery; }

	// True when in Idle and the velocity is pointing AWAY from the target (player too close).
	UFUNCTION(BlueprintPure, Category = "Sniper|Animation")
	bool IsRetreating() const;

	// World-space muzzle location, derived from MuzzleOffset on the actor transform. BP uses
	// this as the start point of the telegraph beam.
	UFUNCTION(BlueprintPure, Category = "Sniper|FX")
	FVector GetMuzzleWorldLocation() const { return GetActorTransform().TransformPosition(MuzzleOffset); }

	// Call from an AnimNotify in the throw animation at the exact frame the bomb leaves the
	// hand. Spawns the projectile, hides the held bomb mesh, and transitions to Recovery.
	UFUNCTION(BlueprintCallable, Category = "Sniper|Animation")
	void LaunchProjectileNow();

	// ===== FX hooks (override in BP to spawn VFX/sound for the aim windup) =====
	// Fired when the sniper enters the Aiming state. Spawn the laser/glow/charging VFX here.
	UFUNCTION(BlueprintImplementableEvent, Category = "Sniper|FX")
	void OnEnterAiming();

	// Fired when the sniper leaves the Aiming state (either fired the throw or bailed out).
	// Stop the VFX you started in OnEnterAiming.
	UFUNCTION(BlueprintImplementableEvent, Category = "Sniper|FX")
	void OnExitAiming();

	// Fired EVERY FRAME during the Aiming state with the live target position (where the
	// bomb will land if released now). Wire to a ground marker BP. Hidden on OnExitAiming.
	UFUNCTION(BlueprintImplementableEvent, Category = "Sniper|FX")
	void OnAimTelegraphTick(const FVector& PredictedLanding);

	// ===== Held bomb mesh (visible while idle/aiming, hidden during recovery) =====
	UPROPERTY(VisibleAnywhere, Category = "Sniper|Components")
	TObjectPtr<UStaticMeshComponent> HeldMesh;

	// Socket on the skeletal mesh where the held bomb attaches (typically a hand bone).
	UPROPERTY(EditAnywhere, Category = "Sniper|Components")
	FName HeldMeshSocketName = TEXT("hand_r");

protected:
	virtual void BeginPlay() override;
	virtual void UpdateMovement(float DeltaTime) override;

	// ===== Tunables =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sniper", meta = (ClampMin = "0"))
	float EngagementRange = 1500.f;

	// Below this distance the sniper retreats (the "fragile up close" trade-off).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sniper", meta = (ClampMin = "0"))
	float RetreatRange = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sniper", meta = (ClampMin = "0.05"))
	float TelegraphDuration = 0.7f;

	// Max seconds the Firing state waits for the AnimNotify "fire" before firing automatically
	// as a fallback. Should be longer than the time from throw-anim start to release notify.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sniper", meta = (ClampMin = "0.05"))
	float FiringTimeout = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sniper", meta = (ClampMin = "0"))
	float RecoveryDuration = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Sniper")
	TSubclassOf<ASniperProjectile> ProjectileClass;

	// Local-space muzzle offset (forward of the body)
	UPROPERTY(EditAnywhere, Category = "Sniper")
	FVector MuzzleOffset = FVector(80.f, 0.f, 50.f);

	// Arc shape of the parabolic toss. 0 = flattest possible trajectory that still reaches
	// the target, 1 = highest peak. ~0.5 reads as a clear lobbed shot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sniper", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float TossArcParam = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Sniper|Debug")
	bool bDrawDebugTelegraph = true;

private:
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Sniper|State")
	ESniperState State = ESniperState::Idle;

	float StateTimer = 0.f;
	FVector AimedDirection = FVector::ForwardVector;

	void EnterIdle();
	void EnterAiming();
	void EnterFiring();
	void EnterRecovery();

	void TickAiming(float DeltaTime);
	void TickRecovery(float DeltaTime);

	void FireProjectile();
};
