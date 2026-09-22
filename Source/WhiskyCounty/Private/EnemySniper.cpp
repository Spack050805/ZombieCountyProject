// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemySniper.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SniperProjectile.h"

bool AEnemySniper::IsRetreating() const
{
	if (State != ESniperState::Idle) return false;
	if (!IsValid(Target)) return false;

	const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const FVector VelDir   = GetVelocity().GetSafeNormal2D();
	if (ToTarget.IsNearlyZero() || VelDir.IsNearlyZero()) return false;

	// Velocity dot direction-to-target < 0 means moving away.
	return FVector::DotProduct(VelDir, ToTarget) < -0.3f;
}

AEnemySniper::AEnemySniper()
{
	// Sniper repositions to keep the player in the engagement band — slower than the chaser
	// because the function is zone denial, not pursuit.
	MoveSpeed = 300.f;
	AcceptanceRadius = 0.f;  // band logic handled inline; AcceptanceRadius unused here
	ContactDamage = 0.f;

	// Ramming a sniper costs more (more HP, slightly more self-damage) than a chaser.
	RamDamageDealt = 100.f;
	RamCostToPlayer = 20.f;

	HeldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldMesh"));
	HeldMesh->SetupAttachment(Mesh); // socket binding deferred to BeginPlay
	HeldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AEnemySniper::BeginPlay()
{
	Super::BeginPlay();

	// Re-attach to the configured socket on the skeletal mesh. Constructor SetupAttachment
	// runs before the BP's mesh asset is set, so the socket may not exist there yet.
	if (HeldMesh && Mesh && HeldMeshSocketName != NAME_None)
	{
		if (Mesh->DoesSocketExist(HeldMeshSocketName))
		{
			HeldMesh->AttachToComponent(Mesh,
				FAttachmentTransformRules::SnapToTargetIncludingScale,
				HeldMeshSocketName);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sniper] HeldMeshSocketName '%s' not found on skeletal mesh."),
				*HeldMeshSocketName.ToString());
		}
		HeldMesh->SetVisibility(true);
	}
}

void AEnemySniper::UpdateMovement(float DeltaTime)
{
	if (!IsValid(Target))
	{
		RefreshTarget();
		if (!IsValid(Target)) return;
	}

	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	const float Dist = ToTarget.Size();
	const FVector Dir = ToTarget.GetSafeNormal();

	// Always face the target (smooth yaw) so the muzzle tracks even mid-windup.
	if (!Dir.IsNearlyZero())
	{
		const FRotator NewRot = FMath::RInterpConstantTo(GetActorRotation(),
			FRotator(0.f, Dir.Rotation().Yaw, 0.f), DeltaTime, TurnRateDeg);
		SetActorRotation(NewRot);
	}

	switch (State)
	{
	case ESniperState::Idle:
		// Out of band → reposition; inside the band → settle and aim.
		if (Dist > EngagementRange)
		{
			AddMovementInput(Dir, 1.f);
		}
		else if (Dist < RetreatRange)
		{
			AddMovementInput(-Dir, 1.f);
		}
		else
		{
			EnterAiming();
		}
		break;
	case ESniperState::Aiming:
		TickAiming(DeltaTime);
		break;
	case ESniperState::Firing:
		// Wait for the AnimNotify to call LaunchProjectileNow. Fallback if it doesn't arrive.
		StateTimer -= DeltaTime;
		if (StateTimer <= 0.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sniper] AnimNotify timeout (%.2fs) — fallback launch"),
				FiringTimeout);
			LaunchProjectileNow();
		}
		break;
	case ESniperState::Recovery:
		TickRecovery(DeltaTime);
		break;
	}
}

void AEnemySniper::EnterIdle()
{
	if (State == ESniperState::Aiming) OnExitAiming();
	State = ESniperState::Idle;
	StateTimer = 0.f;
	if (HeldMesh) HeldMesh->SetVisibility(true); // bomb is back in hand for the next throw
}

void AEnemySniper::EnterAiming()
{
	State = ESniperState::Aiming;
	StateTimer = TelegraphDuration;
	OnEnterAiming();
}

void AEnemySniper::EnterFiring()
{
	if (State == ESniperState::Aiming) OnExitAiming();
	State = ESniperState::Firing;
	StateTimer = FiringTimeout;
}

void AEnemySniper::EnterRecovery() { State = ESniperState::Recovery; StateTimer = RecoveryDuration; }

void AEnemySniper::LaunchProjectileNow()
{
	// Valid only during Aiming or Firing — ignore stray calls (e.g. notify on a re-played anim
	// during Recovery or Idle).
	if (State != ESniperState::Aiming && State != ESniperState::Firing) return;

	// Re-lock the aim direction at the exact moment of release (fallback only — the parabolic
	// solver in FireProjectile uses Target->GetActorLocation() directly).
	if (IsValid(Target))
	{
		const FVector MuzzleWorld = GetActorTransform().TransformPosition(MuzzleOffset);
		AimedDirection = (Target->GetActorLocation() - MuzzleWorld).GetSafeNormal();
		if (AimedDirection.IsNearlyZero()) AimedDirection = GetActorForwardVector();
	}

	FireProjectile();

	if (HeldMesh)
	{
		HeldMesh->SetVisibility(false); // bomb is now in flight
	}

	EnterRecovery();
}

void AEnemySniper::TickAiming(float DeltaTime)
{
	StateTimer -= DeltaTime;

	// Bail out of the shot if the target left the band in either direction.
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	const float AimDist = ToTarget.Size();
	if (AimDist > EngagementRange * 1.2f || AimDist < RetreatRange * 0.8f)
	{
		EnterIdle();
		return;
	}

	if (bDrawDebugTelegraph)
	{
		const FVector MuzzleWorld = GetActorTransform().TransformPosition(MuzzleOffset);
		DrawDebugLine(GetWorld(), MuzzleWorld, Target->GetActorLocation(),
			FColor::Red, false, -1.f, 0, 3.f);
	}

	// BIE per-tick — drives the BP ground marker so it follows the player's live position
	// during the windup. The bomb actually lands at the locked direction at end of windup,
	// so the marker shown here = "if you stay where you are, here's where the bomb hits".
	OnAimTelegraphTick(Target->GetActorLocation());

	if (StateTimer <= 0.f)
	{
		// Lock the aim direction at the moment the telegraph completes.
		const FVector MuzzleWorld = GetActorTransform().TransformPosition(MuzzleOffset);
		AimedDirection = (Target->GetActorLocation() - MuzzleWorld).GetSafeNormal();
		if (AimedDirection.IsNearlyZero()) AimedDirection = GetActorForwardVector();
		EnterFiring();
	}
}

void AEnemySniper::TickRecovery(float DeltaTime)
{
	StateTimer -= DeltaTime;
	if (StateTimer <= 0.f)
	{
		EnterIdle();
	}
}

void AEnemySniper::FireProjectile()
{
	if (!ProjectileClass || !IsValid(Target)) return;
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector MuzzleWorld = GetActorTransform().TransformPosition(MuzzleOffset);
	const FVector TargetLoc   = Target->GetActorLocation();

	// Solve the parabolic trajectory: what initial velocity lands us at TargetLoc given the
	// world's gravity and our chosen arc shape? OverrideGravityZ=0 means "use world gravity",
	// which the projectile experiences at GravityScale=1 — same value, so the math matches.
	FVector TossVelocity = FVector::ZeroVector;
	const bool bSolved = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		World, TossVelocity, MuzzleWorld, TargetLoc,
		/*OverrideGravityZ=*/ 0.f, TossArcParam);

	if (!bSolved)
	{
		// Fallback: shoot along the locked aim direction with a sensible speed. Will under-arc
		// but at least the projectile fires.
		TossVelocity = AimedDirection * 1500.f;
		UE_LOG(LogTemp, Warning, TEXT("[Sniper] Toss math failed (start=%s end=%s) — using flat fallback"),
			*MuzzleWorld.ToCompactString(), *TargetLoc.ToCompactString());
	}

	UE_LOG(LogTemp, Log, TEXT("[Sniper] Fire: muzzle=%s target=%s toss=%s |v|=%.0f solved=%d"),
		*MuzzleWorld.ToCompactString(), *TargetLoc.ToCompactString(),
		*TossVelocity.ToCompactString(), TossVelocity.Size(), bSolved ? 1 : 0);

	if (bDrawDebugTelegraph)
	{
		// Visible spawn marker (cyan) + a few sample points along the predicted parabolic arc.
		DrawDebugSphere(World, MuzzleWorld, 25.f, 12, FColor::Cyan, false, 1.5f, 0, 2.f);

		const float GravityZ = World->GetGravityZ();
		FVector PrevPt = MuzzleWorld;
		for (int32 i = 1; i <= 24; ++i)
		{
			const float t = i * 0.05f;
			const FVector Pt = MuzzleWorld + TossVelocity * t + 0.5f * FVector(0, 0, GravityZ) * t * t;
			DrawDebugLine(World, PrevPt, Pt, FColor::Magenta, false, 1.5f, 0, 2.f);
			PrevPt = Pt;
		}
	}

	FActorSpawnParameters Params;
	Params.Owner      = this;
	Params.Instigator = GetInstigator();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Standard (non-deferred) spawn: BeginPlay/InitializeComponent run during this call. Then
	// we overwrite InitialSpeed and Velocity AFTER, so nothing else can mutate them before
	// the projectile's first tick consumes our chosen TossVelocity.
	ASniperProjectile* Proj = World->SpawnActor<ASniperProjectile>(
		ProjectileClass, MuzzleWorld, TossVelocity.Rotation(), Params);

	if (Proj && Proj->ProjectileMovement)
	{
		Proj->ProjectileMovement->InitialSpeed = 0.f; // disarm the forward*InitialSpeed override
		Proj->ProjectileMovement->MaxSpeed     = FMath::Max(TossVelocity.Size() * 1.5f, 4000.f);
		Proj->ProjectileMovement->Velocity     = TossVelocity;
		Proj->ProjectileMovement->UpdateComponentVelocity();

		UE_LOG(LogTemp, Log, TEXT("[Sniper] Post-spawn: actor=%s vel=%s |v|=%.0f"),
			*Proj->GetActorLocation().ToCompactString(),
			*Proj->ProjectileMovement->Velocity.ToCompactString(),
			Proj->ProjectileMovement->Velocity.Size());
	}
	else if (!Proj)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Sniper] SpawnActor returned null"));
	}
}
