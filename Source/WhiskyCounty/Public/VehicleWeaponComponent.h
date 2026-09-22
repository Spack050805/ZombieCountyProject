// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VehicleWeaponComponent.generated.h"

class AGrenadeProjectile;

UENUM(BlueprintType)
enum class ESpecialWeaponType : uint8
{
	Shotgun          UMETA(DisplayName = "Shotgun"),
	GrenadeLauncher  UMETA(DisplayName = "Grenade Launcher")
};

UCLASS(ClassGroup = (Vehicle), meta = (BlueprintSpawnableComponent))
class WHISKYCOUNTY_API UVehicleWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVehicleWeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ===== Inputs (called by the owning pawn) =====
	void StartFirePrimary();
	void StopFirePrimary();
	// Press: shotgun fires immediately; grenade enters aiming mode (preview arc).
	void FireSpecialPressed();
	// Release: grenade fires if it was aiming; shotgun ignores.
	void FireSpecialReleased();

	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	bool IsAimingSpecial() const { return bAimingSpecial; }

	// True after the player picks a special weapon at end of wave 1. Until then, special
	// fire is disabled and the HUD should hide the special slot.
	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	bool IsSpecialEquipped() const { return bSpecialEquipped; }

	// Sampled trajectory points predicted for the current aim. Updated each tick while aiming.
	// BP feeds these into a Niagara ribbon or spline mesh to draw the parabola.
	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	const TArray<FVector>& GetGrenadePreviewPath() const { return LatestGrenadePreviewPath; }

	// World-space landing point of the grenade for the current aim (last path point or hit).
	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	FVector GetGrenadePreviewLanding() const { return LatestGrenadePreviewLanding; }

	// Refill the special weapon ammo. Called by future ammo-pickup actors when the
	// player drives over a drop from a killed enemy.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Special")
	void AddSpecialAmmo(int32 Amount);

	// ===== Read-only state for HUD / VFX =====
	UFUNCTION(BlueprintPure, Category = "Weapon|Primary")
	float GetHeatNormalized() const { return FMath::Clamp(Heat / FMath::Max(1.f, MaxHeat), 0.f, 1.f); }

	UFUNCTION(BlueprintPure, Category = "Weapon|Primary")
	bool IsOverheated() const { return bOverheated; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	int32 GetCurrentSpecialAmmo() const { return CurrentSpecialAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	int32 GetMaxSpecialAmmo() const { return MaxSpecialAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Special")
	ESpecialWeaponType GetSelectedSpecialWeaponType() const { return SpecialWeaponType; }

	// Switch the equipped special. Resets ammo to StartingSpecialAmmo. Called by the reward
	// flow at end of wave 1.
	UFUNCTION(BlueprintCallable, Category = "Weapon|Special")
	void SetSelectedSpecialWeaponType(ESpecialWeaponType NewType);

	// ===== Common =====
	UPROPERTY(EditAnywhere, Category = "Weapon")
	bool bDrawWeaponDebug = false;

	// ===== Primary — Machinegun (always active, heat-based, infinite "ammo") =====
	UPROPERTY(EditAnywhere, Category = "Weapon|Primary", meta = (ClampMin = "0.1"))
	float MachinegunFireRate = 10.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Primary", meta = (ClampMin = "0"))
	float MachinegunDamage = 10.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Primary", meta = (ClampMin = "0"))
	float MachinegunRange = 5000.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Primary", meta = (ClampMin = "0"))
	float MachinegunSpreadDegrees = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Primary|Heat", meta = (ClampMin = "1"))
	float MaxHeat = 100.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Primary|Heat", meta = (ClampMin = "0"))
	float HeatPerShot = 8.f;

	// Heat dissipated per second (always, even while firing — net heat per shot = HeatPerShot - CoolingRate*FireInterval)
	UPROPERTY(EditAnywhere, Category = "Weapon|Primary|Heat", meta = (ClampMin = "0"))
	float CoolingRate = 40.f;

	// After overheat lockout, can fire again only when Heat drops to this fraction of MaxHeat
	UPROPERTY(EditAnywhere, Category = "Weapon|Primary|Heat", meta = (ClampMin = "0", ClampMax = "1"))
	float OverheatRecoveryFraction = 0.4f;

	// ===== Special — slot weapon (Shotgun OR Grenade Launcher), ammo-limited =====
	UPROPERTY(EditAnywhere, Category = "Weapon|Special")
	ESpecialWeaponType SpecialWeaponType = ESpecialWeaponType::Shotgun;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special", meta = (ClampMin = "0"))
	int32 MaxSpecialAmmo = 8;

	// Ammo at run start (clamped to MaxSpecialAmmo). Future: overridden by GameInstance / SaveGame.
	UPROPERTY(EditAnywhere, Category = "Weapon|Special", meta = (ClampMin = "0"))
	int32 StartingSpecialAmmo = 4;

	// ----- Shotgun -----
	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Shotgun", meta = (ClampMin = "0.1"))
	float ShotgunFireRate = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Shotgun", meta = (ClampMin = "1"))
	int32 ShotgunPellets = 3;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Shotgun", meta = (ClampMin = "0"))
	float ShotgunDamagePerPellet = 25.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Shotgun", meta = (ClampMin = "0"))
	float ShotgunRange = 2500.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Shotgun", meta = (ClampMin = "0"))
	float ShotgunSpreadDegrees = 8.f;

	// ----- Grenade Launcher -----
	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Grenade", meta = (ClampMin = "0.1"))
	float GrenadeFireRate = 1.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Grenade")
	TSubclassOf<AGrenadeProjectile> GrenadeProjectileClass;

	// Maximum horizontal distance the grenade can land from the muzzle. The mouse aim point
	// is clamped to this distance, so aiming beyond just throws to the cap.
	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Grenade", meta = (ClampMin = "0"))
	float MaxGrenadeRange = 3000.f;

	// Arc shape for the toss. 0.05 ~ flat throw (fast), 0.95 ~ high lob (slow), 0.3 ~ snappy.
	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Grenade", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float GrenadeArcParam = 0.3f;

	// Legacy fallback values, used only if SuggestProjectileVelocity_CustomArc fails to solve.
	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Grenade|Fallback", meta = (ClampMin = "0"))
	float GrenadeMuzzleSpeed = 2500.f;

	UPROPERTY(EditAnywhere, Category = "Weapon|Special|Grenade|Fallback", meta = (ClampMin = "0", ClampMax = "2"))
	float GrenadeLaunchArc = 0.5f;

private:
	// ----- Runtime state -----
	bool  bFiringPrimary = false;
	bool  bAimingSpecial = false; // RMB held with grenade equipped
	bool  bSpecialEquipped = false; // false until the player picks at end of wave 1
	float PrimaryFireCooldown = 0.f;
	float SpecialFireCooldown = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Weapon|State")
	float Heat = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Weapon|State")
	bool bOverheated = false;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Weapon|State")
	int32 CurrentSpecialAmmo = 0;

	// ----- Internals -----
	void TryFirePrimary();
	void TryFireSpecial();

	bool ComputeAim(FVector& OutMuzzleWorld, FVector& OutAimDirXY, bool bUseGrenadeMuzzle = false) const;

	void Fire_Machinegun(const FVector& MuzzleWorld, const FVector& AimDir);
	void Fire_Shotgun   (const FVector& MuzzleWorld, const FVector& AimDir);
	void Fire_Grenade   (const FVector& MuzzleWorld, const FVector& AimDir);

	// Computes the parabolic launch velocity that lands the grenade at the (clamped) aim point.
	// Returns false if the math couldn't solve. Used by both Fire_Grenade and the preview.
	bool ComputeGrenadeLaunchVelocity(const FVector& MuzzleWorld, const FVector& AimDirXY,
	                                  const FVector& AimPoint, FVector& OutVelocity) const;

	// Updates the predicted trajectory cache + draws debug visualisation each frame while aiming.
	void UpdateGrenadePreview();

	TArray<FVector> LatestGrenadePreviewPath;
	FVector         LatestGrenadePreviewLanding = FVector::ZeroVector;

	float GetCurrentSpecialFireInterval() const;

	AActor* PerformHitscan(const FVector& Origin, const FVector& Direction,
	                       float Range, float Damage, float SpreadDegrees,
	                       FVector& OutShotEnd);
};
