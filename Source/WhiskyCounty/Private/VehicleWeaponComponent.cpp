// Fill out your copyright notice in the Description page of Project Settings.

#include "VehicleWeaponComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GrenadeProjectile.h"
#include "Kismet/GameplayStatics.h"
#include "VehiclePawn.h"

UVehicleWeaponComponent::UVehicleWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVehicleWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	// No special equipped until the player picks one (end of wave 1 reward). Until then,
	// CurrentSpecialAmmo stays 0 and TryFireSpecial / FireSpecialPressed are no-ops.
	bSpecialEquipped = false;
	CurrentSpecialAmmo = 0;
	Heat = 0.f;
	bOverheated = false;
}

void UVehicleWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Heat cools down continuously
	if (Heat > 0.f)
	{
		Heat = FMath::Max(0.f, Heat - CoolingRate * DeltaTime);
	}
	// Recover from overheat lockout when heat drops below threshold
	if (bOverheated && Heat <= MaxHeat * OverheatRecoveryFraction)
	{
		bOverheated = false;
	}

	// Fire-rate cooldowns
	if (PrimaryFireCooldown > 0.f) PrimaryFireCooldown = FMath::Max(0.f, PrimaryFireCooldown - DeltaTime);
	if (SpecialFireCooldown > 0.f) SpecialFireCooldown = FMath::Max(0.f, SpecialFireCooldown - DeltaTime);

	// Auto-fire primary while LMB held, gated by overheat + fire rate
	if (bFiringPrimary && !bOverheated && PrimaryFireCooldown <= 0.f)
	{
		TryFirePrimary();
	}

	// While aiming the grenade launcher (RMB held), update the predicted-arc cache + debug draw.
	if (bAimingSpecial && SpecialWeaponType == ESpecialWeaponType::GrenadeLauncher && CurrentSpecialAmmo > 0)
	{
		UpdateGrenadePreview();
	}
}

// ===== Public input API =====

void UVehicleWeaponComponent::StartFirePrimary()
{
	bFiringPrimary = true;
	if (!bOverheated && PrimaryFireCooldown <= 0.f)
	{
		TryFirePrimary();
	}
}

void UVehicleWeaponComponent::StopFirePrimary()
{
	bFiringPrimary = false;
}

void UVehicleWeaponComponent::FireSpecialPressed()
{
	if (!bSpecialEquipped)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Weapon] FireSpecialPressed ignored — no special equipped (pick at end of wave 1)"));
		return;
	}

	if (SpecialWeaponType == ESpecialWeaponType::GrenadeLauncher)
	{
		// Hold-to-aim, release-to-fire. Just enter aiming mode here.
		if (!bAimingSpecial)
		{
			bAimingSpecial = true;
			if (AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner()))
			{
				OwnerPawn->OnGrenadeAimStarted();
			}
		}
	}
	else
	{
		// Shotgun: semi-auto, fire on press.
		TryFireSpecial();
	}
}

void UVehicleWeaponComponent::FireSpecialReleased()
{
	if (!bSpecialEquipped) return;

	if (SpecialWeaponType == ESpecialWeaponType::GrenadeLauncher && bAimingSpecial)
	{
		bAimingSpecial = false;
		if (AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner()))
		{
			OwnerPawn->OnGrenadeAimEnded();
		}
		LatestGrenadePreviewPath.Reset();
		LatestGrenadePreviewLanding = FVector::ZeroVector;
		TryFireSpecial();
	}
}

void UVehicleWeaponComponent::AddSpecialAmmo(int32 Amount)
{
	CurrentSpecialAmmo = FMath::Clamp(CurrentSpecialAmmo + Amount, 0, MaxSpecialAmmo);
}

void UVehicleWeaponComponent::SetSelectedSpecialWeaponType(ESpecialWeaponType NewType)
{
	SpecialWeaponType  = NewType;
	bSpecialEquipped   = true;
	CurrentSpecialAmmo = FMath::Clamp(StartingSpecialAmmo, 0, MaxSpecialAmmo);

	UE_LOG(LogTemp, Log, TEXT("[Weapon] Special equipped: %d (ammo %d/%d)"),
		(int32)NewType, CurrentSpecialAmmo, MaxSpecialAmmo);
}

// ===== Internals =====

bool UVehicleWeaponComponent::ComputeAim(FVector& OutMuzzleWorld, FVector& OutAimDirXY, bool bUseGrenadeMuzzle) const
{
	AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner());
	if (!OwnerPawn) return false;

	FVector AimPoint;
	if (!OwnerPawn->GetAimWorldPoint(AimPoint)) return false;

	OutMuzzleWorld = bUseGrenadeMuzzle
		? OwnerPawn->GetGrenadeMuzzleWorldLocation()
		: OwnerPawn->GetPrimaryMuzzleWorldLocation();
	FVector AimDir = AimPoint - OutMuzzleWorld;
	AimDir.Z = 0.f;
	AimDir = AimDir.GetSafeNormal();
	if (AimDir.IsNearlyZero()) return false;

	OutAimDirXY = AimDir;

	if (bDrawWeaponDebug)
	{
		if (UWorld* World = GetWorld())
		{
			DrawDebugSphere(World, AimPoint, 20.f, 12, FColor::Cyan, false, 0.1f, 0, 1.f);
			DrawDebugSphere(World, OutMuzzleWorld, 8.f, 8, FColor::Magenta, false, 0.1f, 0, 1.f);
		}
	}
	return true;
}

void UVehicleWeaponComponent::TryFirePrimary()
{
	FVector MuzzleWorld, AimDir;
	if (!ComputeAim(MuzzleWorld, AimDir)) return;

	Fire_Machinegun(MuzzleWorld, AimDir);

	// Heat accumulation + overheat lockout
	Heat = FMath::Min(MaxHeat, Heat + HeatPerShot);
	if (Heat >= MaxHeat)
	{
		Heat = MaxHeat;
		bOverheated = true;
	}

	PrimaryFireCooldown = 1.f / FMath::Max(0.1f, MachinegunFireRate);
}

void UVehicleWeaponComponent::TryFireSpecial()
{
	if (CurrentSpecialAmmo <= 0)    return;
	if (SpecialFireCooldown > 0.f)  return;

	const bool bUseGrenadeMuzzle = (SpecialWeaponType == ESpecialWeaponType::GrenadeLauncher);

	FVector MuzzleWorld, AimDir;
	if (!ComputeAim(MuzzleWorld, AimDir, bUseGrenadeMuzzle)) return;

	switch (SpecialWeaponType)
	{
		case ESpecialWeaponType::Shotgun:         Fire_Shotgun(MuzzleWorld, AimDir); break;
		case ESpecialWeaponType::GrenadeLauncher: Fire_Grenade(MuzzleWorld, AimDir); break;
	}

	--CurrentSpecialAmmo;
	SpecialFireCooldown = GetCurrentSpecialFireInterval();
}

float UVehicleWeaponComponent::GetCurrentSpecialFireInterval() const
{
	switch (SpecialWeaponType)
	{
		case ESpecialWeaponType::Shotgun:         return 1.f / FMath::Max(0.1f, ShotgunFireRate);
		case ESpecialWeaponType::GrenadeLauncher: return 1.f / FMath::Max(0.1f, GrenadeFireRate);
	}
	return 0.5f;
}

void UVehicleWeaponComponent::Fire_Machinegun(const FVector& MuzzleWorld, const FVector& AimDir)
{
	FVector ShotEnd;
	PerformHitscan(MuzzleWorld, AimDir, MachinegunRange, MachinegunDamage, MachinegunSpreadDegrees, ShotEnd);
	if (AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner()))
	{
		OwnerPawn->OnPrimaryFired(MuzzleWorld, AimDir, ShotEnd);
	}
}

void UVehicleWeaponComponent::Fire_Shotgun(const FVector& MuzzleWorld, const FVector& AimDir)
{
	AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner());

	for (int32 i = 0; i < ShotgunPellets; ++i)
	{
		const float Pct = (ShotgunPellets > 1)
			? (((float)i / (float)(ShotgunPellets - 1)) - 0.5f) * 2.f
			: 0.f;
		const float DegOffset = Pct * ShotgunSpreadDegrees;
		const FVector PelletDir = AimDir.RotateAngleAxis(DegOffset, FVector::UpVector);

		FVector PelletEnd;
		PerformHitscan(MuzzleWorld, PelletDir, ShotgunRange, ShotgunDamagePerPellet, 0.f, PelletEnd);

		if (OwnerPawn)
		{
			OwnerPawn->OnShotgunPelletFired(MuzzleWorld, PelletEnd);
		}
	}

	if (OwnerPawn)
	{
		OwnerPawn->OnShotgunFired(MuzzleWorld, AimDir);
	}
}

bool UVehicleWeaponComponent::ComputeGrenadeLaunchVelocity(const FVector& MuzzleWorld, const FVector& AimDirXY,
                                                          const FVector& AimPoint, FVector& OutVelocity) const
{
	UWorld* World = GetWorld();
	if (!World || !GrenadeProjectileClass) return false;

	// Read the projectile's gravity scale from the CDO so the math accounts for whatever
	// scale the BP set (default 1.5x world gravity).
	float GrenadeGravityScale = 1.f;
	if (const AGrenadeProjectile* CDO = Cast<AGrenadeProjectile>(GrenadeProjectileClass->GetDefaultObject()))
	{
		if (CDO->ProjectileMovement)
		{
			GrenadeGravityScale = CDO->ProjectileMovement->ProjectileGravityScale;
		}
	}

	// Clamp horizontal distance to MaxGrenadeRange. Grenade lands at mouse OR at cap.
	FVector ToAimPlanar = AimPoint - MuzzleWorld;
	ToAimPlanar.Z = 0.f;
	const float DistToAim   = ToAimPlanar.Size();
	const float ClampedDist = FMath::Min(DistToAim, MaxGrenadeRange);

	FVector TargetPoint = MuzzleWorld + AimDirXY * ClampedDist;
	TargetPoint.Z = AimPoint.Z;

	const float GravityZ = World->GetGravityZ() * GrenadeGravityScale;
	return UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		World, OutVelocity, MuzzleWorld, TargetPoint,
		/*OverrideGravityZ=*/ GravityZ, GrenadeArcParam);
}

void UVehicleWeaponComponent::UpdateGrenadePreview()
{
	UWorld* World = GetWorld();
	if (!World || !GrenadeProjectileClass) return;

	AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner());
	if (!OwnerPawn) return;

	FVector AimPoint;
	if (!OwnerPawn->GetAimWorldPoint(AimPoint)) return;

	const FVector MuzzleWorld = OwnerPawn->GetGrenadeMuzzleWorldLocation();

	FVector AimDirXY = AimPoint - MuzzleWorld;
	AimDirXY.Z = 0.f;
	AimDirXY = AimDirXY.GetSafeNormal();
	if (AimDirXY.IsNearlyZero()) return;

	FVector LaunchVelocity;
	if (!ComputeGrenadeLaunchVelocity(MuzzleWorld, AimDirXY, AimPoint, LaunchVelocity)) return;

	// Read gravity scale again for predict (cheap; could cache)
	float GravityScale = 1.f;
	if (const AGrenadeProjectile* CDO = Cast<AGrenadeProjectile>(GrenadeProjectileClass->GetDefaultObject()))
	{
		if (CDO->ProjectileMovement) GravityScale = CDO->ProjectileMovement->ProjectileGravityScale;
	}

	FPredictProjectilePathParams PredictParams;
	PredictParams.StartLocation       = MuzzleWorld;
	PredictParams.LaunchVelocity      = LaunchVelocity;
	PredictParams.bTraceWithCollision = true;
	PredictParams.ProjectileRadius    = 15.f;
	PredictParams.MaxSimTime          = 5.f;
	PredictParams.SimFrequency        = 30.f;
	PredictParams.OverrideGravityZ    = World->GetGravityZ() * GravityScale;
	PredictParams.ActorsToIgnore.Add(GetOwner());

	FPredictProjectilePathResult Result;
	UGameplayStatics::PredictProjectilePath(this, PredictParams, Result);

	// Cache for BP consumption (Niagara ribbon / spline mesh).
	LatestGrenadePreviewPath.Reset(Result.PathData.Num());
	for (const FPredictProjectilePathPointData& P : Result.PathData)
	{
		LatestGrenadePreviewPath.Add(P.Location);
	}
	LatestGrenadePreviewLanding = Result.HitResult.bBlockingHit
		? Result.HitResult.Location
		: (Result.PathData.Num() > 0 ? Result.PathData.Last().Location : MuzzleWorld);

	if (bDrawWeaponDebug)
	{
		// Debug visualisation (toggle off in shipping; the BP Niagara handles polished visuals).
		for (int32 i = 1; i < Result.PathData.Num(); ++i)
		{
			DrawDebugLine(World,
				Result.PathData[i - 1].Location, Result.PathData[i].Location,
				FColor::Yellow, false, -1.f, 0, 2.f);
		}
		DrawDebugSphere(World, LatestGrenadePreviewLanding, 60.f, 16, FColor::Yellow, false, -1.f, 0, 3.f);
	}

	// BIE per-tick on the owning pawn — drives the BP ground marker that follows the
	// parabolic landing point live as the player aims. OwnerPawn was already validated
	// at the top of this function, so we can reuse it directly.
	OwnerPawn->OnGrenadeAimTick(LatestGrenadePreviewLanding);
}

void UVehicleWeaponComponent::Fire_Grenade(const FVector& MuzzleWorld, const FVector& AimDir)
{
	if (!GrenadeProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleWeapon] GrenadeProjectileClass not set."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	AVehiclePawn* OwnerPawn = Cast<AVehiclePawn>(GetOwner());

	FVector LaunchVelocity = FVector::ZeroVector;
	bool bSolved = false;

	if (OwnerPawn)
	{
		FVector AimPoint;
		if (OwnerPawn->GetAimWorldPoint(AimPoint))
		{
			bSolved = ComputeGrenadeLaunchVelocity(MuzzleWorld, AimDir, AimPoint, LaunchVelocity);
		}
	}

	if (!bSolved)
	{
		// Fallback: legacy fixed-arc + fixed-speed launch.
		const FVector LegacyDir = (AimDir + FVector::UpVector * GrenadeLaunchArc).GetSafeNormal();
		LaunchVelocity = LegacyDir * GrenadeMuzzleSpeed;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const FRotator SpawnRot = LaunchVelocity.Rotation();
	AGrenadeProjectile* Grenade = World->SpawnActor<AGrenadeProjectile>(GrenadeProjectileClass, MuzzleWorld, SpawnRot, SpawnParams);
	if (Grenade && Grenade->ProjectileMovement)
	{
		Grenade->ProjectileMovement->Velocity = LaunchVelocity;
	}

	if (OwnerPawn)
	{
		OwnerPawn->OnGrenadeLaunched(MuzzleWorld, LaunchVelocity);
	}
}

AActor* UVehicleWeaponComponent::PerformHitscan(const FVector& Origin, const FVector& Direction,
	float Range, float Damage, float SpreadDegrees, FVector& OutShotEnd)
{
	UWorld* World = GetWorld();
	if (!World) { OutShotEnd = Origin; return nullptr; }

	FVector ShotDir = Direction;
	if (SpreadDegrees > KINDA_SMALL_NUMBER)
	{
		const float ConeRad = FMath::DegreesToRadians(SpreadDegrees);
		ShotDir = FMath::VRandCone(Direction, ConeRad);
	}

	const FVector End = Origin + ShotDir * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleWeapon), false, GetOwner());
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params);

	OutShotEnd = bHit ? Hit.ImpactPoint : End;

	if (bDrawWeaponDebug)
	{
		DrawDebugLine(World, Origin, OutShotEnd,
			bHit ? FColor::Green : FColor::Red, false, 2.f, 0, 2.f);
	}

	if (bHit && Hit.GetActor())
	{
		AController* InstigatorController = nullptr;
		if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			InstigatorController = OwnerPawn->GetController();
		}

		// Cache the actor pointer: a killing shot can destroy the actor synchronously
		// inside TakeDamage, after which Hit.GetActor() returns null.
		AActor* HitActor = Hit.GetActor();
		FDamageEvent DamageEvent;
		HitActor->TakeDamage(Damage, DamageEvent, InstigatorController, GetOwner());
		return IsValid(HitActor) ? HitActor : nullptr;
	}
	return nullptr;
}
