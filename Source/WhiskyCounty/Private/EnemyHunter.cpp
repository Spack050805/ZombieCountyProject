// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyHunter.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HealthComponent.h"

AEnemyHunter::AEnemyHunter()
{
	// Slightly slower than the chaser when pursuing — the danger is the kamikaze blast, not the chase.
	MoveSpeed = 500.f;
	AcceptanceRadius = 100.f;

	// No sustained contact damage. Damage comes from the kamikaze explosion.
	ContactDamage = 0.f;

	// Ramming the hunter still works — it makes the player STILL pay the ram cost while
	// triggering the explosion (you killed it but the blast hurts you back).
	RamDamageDealt = 80.f;
	RamCostToPlayer = 50.f;

	// Hunter immune to Onda d'urto (a kamikaze pushed by powerup would be too easy to defang).
	bImmuneToShockwave = true;
}

void AEnemyHunter::BeginPlay()
{
	Super::BeginPlay();

	// Bind capsule overlap so we can detect contact with the player in ANY state — not just
	// during the lunge sweep. This covers the case "player drives into a stationary hunter
	// during Charging/Recovery". The kamikaze theme = ANY touch = boom.
	if (CollisionCapsule)
	{
		CollisionCapsule->OnComponentBeginOverlap.AddDynamic(this, &AEnemyHunter::HandleCapsuleOverlap);
	}
}

void AEnemyHunter::HandleCapsuleOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (bExploded) return;
	if (!OtherActor || OtherActor == this) return;

	// Skip other enemies — crowd separation makes us overlap them all the time.
	if (OtherActor->IsA(AEnemyBase::StaticClass())) return;

	// Only the actual player target triggers detonation. Refresh target if missing.
	if (!IsValid(Target)) RefreshTarget();
	if (OtherActor != Target) return;

	UE_LOG(LogTemp, Log, TEXT("[Hunter] %s touched player → KAMIKAZE"), *GetNameSafe(this));
	Explode();
}

void AEnemyHunter::Explode()
{
	if (bExploded) return;
	bExploded = true;

	// Symmetry: fire the matching exit BIE for whatever state we were in, so BP cleanup
	// (windup VFX, dash trail) runs before the explosion VFX.
	if (State == EHunterState::Charging) OnExitCharging();
	if (State == EHunterState::Lunging)  OnExitLunging();

	UWorld* World = GetWorld();
	const FVector Origin = GetActorLocation();
	int32 Affected = 0;

	if (World)
	{
		// Sphere overlap to find anything caught in the blast radius.
		TArray<FOverlapResult> Overlaps;
		const FCollisionShape Shape = FCollisionShape::MakeSphere(ExplosionRadius);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(HunterKamikaze), false, this);
		Params.AddIgnoredActor(this);

		FCollisionObjectQueryParams ObjParams;
		ObjParams.AddObjectTypesToQuery(ECC_Pawn);
		ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody); // player chassis is PhysicsBody

		World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, ObjParams, Shape, Params);

		for (const FOverlapResult& O : Overlaps)
		{
			AActor* HitActor = O.GetActor();
			if (!HitActor || HitActor == this) continue;
			if (HitActor->IsA(AEnemyBase::StaticClass())) continue; // no friendly fire

			FDamageEvent Dmg;
			HitActor->TakeDamage(LungeImpactDamage, Dmg, GetController(), this);
			++Affected;
		}

		if (bDrawDebugTelegraph)
		{
			DrawDebugSphere(World, Origin, ExplosionRadius, 24, FColor::Red, false, 1.0f, 0, 4.f);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Hunter] KABOOM @ %s — affected %d, damage %.1f"),
		*Origin.ToCompactString(), Affected, LungeImpactDamage);

	OnHunterExploded(Origin, Affected);

	// Die through HealthComponent so the existing OnDeath/OnDeathFX/WaveDirector chain still
	// fires (kill counter increments, death emitter spawns via base BP, etc.).
	if (Health)
	{
		Health->Kill(this);
	}
	else
	{
		Destroy();
	}
}

void AEnemyHunter::UpdateMovement(float DeltaTime)
{
	if (!IsValid(Target))
	{
		RefreshTarget();
		if (!IsValid(Target)) return;
	}

	switch (State)
	{
	case EHunterState::Pursuing:
	{
		// Standard seek behavior from the base class.
		Super::UpdateMovement(DeltaTime);

		FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		ToTarget.Z = 0.f;
		if (ToTarget.Size() <= LungeRange)
		{
			EnterCharging();
		}
		break;
	}
	case EHunterState::Charging:
		TickCharging(DeltaTime);
		break;
	case EHunterState::Lunging:
		TickLunging(DeltaTime);
		break;
	case EHunterState::Recovery:
		TickRecovery(DeltaTime);
		break;
	}
}

void AEnemyHunter::EnterPursuing()
{
	State = EHunterState::Pursuing;
	StateTimer = 0.f;
	UE_LOG(LogTemp, Verbose, TEXT("[Hunter] %s -> Pursuing"), *GetNameSafe(this));
}

void AEnemyHunter::EnterCharging()
{
	State = EHunterState::Charging;
	StateTimer = ChargeDuration;
	OnEnterCharging();
	UE_LOG(LogTemp, Log, TEXT("[Hunter] %s -> Charging (%.2fs)"), *GetNameSafe(this), ChargeDuration);
}

void AEnemyHunter::EnterLunging()
{
	if (State == EHunterState::Charging) OnExitCharging();

	State = EHunterState::Lunging;
	StateTimer = LungeDuration;
	OnEnterLunging(LungeDirection);

	UE_LOG(LogTemp, Log, TEXT("[Hunter] %s -> Lunging dir=%s speed=%.0f duration=%.2f"),
		*GetNameSafe(this), *LungeDirection.ToCompactString(), LungeSpeed, LungeDuration);
}

void AEnemyHunter::EnterRecovery()
{
	if (State == EHunterState::Lunging) OnExitLunging();

	State = EHunterState::Recovery;
	StateTimer = RecoveryDuration;

	UE_LOG(LogTemp, Log, TEXT("[Hunter] %s -> Recovery (%.2fs)"), *GetNameSafe(this), RecoveryDuration);
}

void AEnemyHunter::TickCharging(float DeltaTime)
{
	StateTimer -= DeltaTime;

	// Track the player with rotation during the windup (so the snapshot at lock-time is fair).
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	const FVector Dir = ToTarget.GetSafeNormal();
	if (!Dir.IsNearlyZero())
	{
		const FRotator NewRot = FMath::RInterpConstantTo(GetActorRotation(),
			FRotator(0.f, Dir.Rotation().Yaw, 0.f), DeltaTime, TurnRateDeg);
		SetActorRotation(NewRot);
	}

	// Telegraph: red line from hunter to current target position. The path is NOT yet locked —
	// it locks at the moment we transition to Lunging.
	if (bDrawDebugTelegraph)
	{
		DrawDebugLine(GetWorld(), GetActorLocation(), Target->GetActorLocation(),
			FColor::Red, false, -1.f, 0, 5.f);
	}

	if (StateTimer <= 0.f)
	{
		// Lock direction now: snapshot of the player's position at the END of the windup.
		FVector LockedTo = Target->GetActorLocation() - GetActorLocation();
		LockedTo.Z = 0.f;
		LungeDirection = LockedTo.GetSafeNormal();
		if (LungeDirection.IsNearlyZero()) LungeDirection = GetActorForwardVector();
		EnterLunging();
	}
}

void AEnemyHunter::TickLunging(float DeltaTime)
{
	StateTimer -= DeltaTime;

	UWorld* World = GetWorld();
	const FVector StartLoc = GetActorLocation();
	const FVector EndLoc   = StartLoc + LungeDirection * LungeSpeed * DeltaTime;

	// Manual capsule sweep so we can filter out "ground-like" hits (landscape, floor) by normal —
	// otherwise micro-rises in the terrain stop the dash on the first frame.
	bool bImpact = false;
	FHitResult RealHit;

	if (World && CollisionCapsule)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HunterLunge), false, this);
		Params.AddIgnoredActor(this);

		const float Radius     = CollisionCapsule->GetScaledCapsuleRadius();
		const float HalfHeight = CollisionCapsule->GetScaledCapsuleHalfHeight();
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);

		TArray<FHitResult> Hits;
		World->SweepMultiByChannel(Hits, StartLoc, EndLoc, FQuat::Identity,
			ECC_Pawn, Shape, Params);

		for (const FHitResult& H : Hits)
		{
			if (!H.bBlockingHit) continue;

			AActor* HA = H.GetActor();
			// Skip other enemies — no friendly fire from the lunge.
			if (HA && HA != this && HA->IsA(AEnemyBase::StaticClass())) continue;

			// Skip ground/landscape: a near-vertical normal means we hit a floor surface.
			const float UpDot = FVector::DotProduct(H.ImpactNormal, FVector::UpVector);
			if (UpDot > 0.7f) continue;

			RealHit = H;
			bImpact = true;
			break;
		}
	}

	if (bImpact)
	{
		// Move up to the impact point so the explosion origin is right where we crashed.
		SetActorLocation(RealHit.Location, /*bSweep=*/ false);

		if (bDrawDebugTelegraph)
		{
			DrawDebugLine(World, StartLoc, RealHit.Location,
				FColor::Yellow, false, 0.4f, 0, 4.f);
		}

		UE_LOG(LogTemp, Log, TEXT("[Hunter] Lunge contact with %s — KAMIKAZE"),
			*GetNameSafe(RealHit.GetActor()));

		// KABOOM — the AOE handles damage now, no separate point damage.
		// Note: HandleCapsuleOverlap may have already fired Explode this same frame, but
		// the bExploded guard makes it idempotent.
		Explode();
		return;
	}

	// No real obstacle — teleport to the next position. SnapToGround in the parent Tick
	// re-aligns Z to the terrain, so the hunter follows slopes naturally.
	SetActorLocation(EndLoc, /*bSweep=*/ false);

	if (bDrawDebugTelegraph)
	{
		DrawDebugLine(World, StartLoc, EndLoc,
			FColor::Yellow, false, /*LifeTime=*/ 0.4f, 0, 4.f);
	}

	if (StateTimer <= 0.f)
	{
		// Lunge timer expired without hitting. Either commit fully (kamikaze theme) or
		// retreat to retry (tactical theme), per the bExplodeOnLungeWhiff toggle.
		if (bExplodeOnLungeWhiff)
		{
			UE_LOG(LogTemp, Log, TEXT("[Hunter] Lunge whiffed — exploding anyway (kamikaze fully committed)"));
			Explode();
		}
		else
		{
			EnterRecovery();
		}
	}
}

void AEnemyHunter::TickRecovery(float DeltaTime)
{
	StateTimer -= DeltaTime;
	if (StateTimer <= 0.f)
	{
		EnterPursuing();
	}
}
