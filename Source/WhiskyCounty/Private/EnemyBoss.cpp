// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyBoss.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/Pawn.h"
#include "HealthComponent.h"

AEnemyBoss::AEnemyBoss()
{
	// Boss is fast in pursue (faster than the chaser) so it can actually catch up between leaps.
	// He doesn't need a separate "running" speed — pursue IS the run.
	MoveSpeed = 700.f;

	// Stay close to the player when pursuing — small ring radius so the boss overlaps the
	// chassis and the contact damage reliably triggers.
	AcceptanceRadius = 100.f;
	TurnRateDeg = 240.f;

	// Pursue damage is anim-driven (ApplyPursueDamageNow called from AnimNotify), not
	// per-tick. Disable the base class's auto contact attack so we don't double-hit.
	ContactDamage = 0.f;

	// Ramming the boss should NOT one-shot him like a chaser. Ram hurts him a little, but
	// the cost to the player is heavy — discourages cheese.
	RamDamageDealt  = 60.f;
	RamCostToPlayer = 60.f;

	// Boss is immune to Onda d'urto (a powerup shouldn't trivialize the final fight).
	bImmuneToShockwave = true;

	// Bigger capsule for a presence-on-screen boss silhouette.
	if (CollisionCapsule)
	{
		CollisionCapsule->InitCapsuleSize(80.f, 140.f);
	}
}

void AEnemyBoss::Tick(float DeltaTime)
{
	// We deliberately do NOT call Super::Tick. AEnemyBase::Tick runs UpdateMovement → ApplySeparation
	// → TryContactAttack → SnapToGround. We want to:
	//   - keep UpdateMovement (state machine)
	//   - skip ApplySeparation (boss is alone in the final wave)
	//   - skip the parent's TryContactAttack and run our own state-gated version instead
	//   - skip ground snap during Leaping (mid-air)
	APawn::Tick(DeltaTime);

	if (Health && Health->IsDead()) return;

	UpdateMovement(DeltaTime);

	// Update the in-range flag every frame (drives IsPursuingInContactRange for AnimBP →
	// when this becomes true the AnimBP enters the attack state which plays the swing anim
	// which fires the AnimNotify which calls ApplyPursueDamageNow). No damage here.
	UpdatePursueRangeState();

	if (bSnapToGround && !IsAirborne())
	{
		SnapToGround();
	}
}

void AEnemyBoss::UpdateMovement(float DeltaTime)
{
	if (!IsValid(Target))
	{
		RefreshTarget();
		if (!IsValid(Target)) return;
	}

	// Universal cooldown counter (decrements while not in the middle of a leap).
	if (LeapCooldownLeft > 0.f && State != EBossState::Leaping && State != EBossState::ChargingLeap)
	{
		LeapCooldownLeft = FMath::Max(0.f, LeapCooldownLeft - DeltaTime);
	}

	switch (State)
	{
	case EBossState::Pursuing:     TickPursuing(DeltaTime);     break;
	case EBossState::ChargingLeap: TickChargingLeap(DeltaTime); break;
	case EBossState::Leaping:      TickLeaping(DeltaTime);      break;
	case EBossState::Recovery:     TickRecovery(DeltaTime);     break;
	}
}

// ===== Pursuing — chaser-style chase + contact damage =====

void AEnemyBoss::TickPursuing(float DeltaTime)
{
	// Use the base class seek (ring formation around the player) to close the gap.
	// MoveSpeed = 700 means the boss aggressively closes when the player slows down.
	Super::UpdateMovement(DeltaTime);

	// Should we leap? Player has gotten far enough away AND cooldown is ready.
	if (ShouldLeapNow())
	{
		EnterChargingLeap();
	}
}

bool AEnemyBoss::ShouldLeapNow() const
{
	if (LeapCooldownLeft > 0.f) return false;
	if (!IsValid(Target)) return false;

	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	const float Dist = ToTarget.Size();

	// Player must be at least LeapTriggerDistance away (so we don't leap when we could just hit).
	// Capped at LeapMaxRange to avoid silly cross-map leaps.
	return Dist >= LeapTriggerDistance && Dist <= LeapMaxRange;
}

void AEnemyBoss::UpdatePursueRangeState()
{
	// Only "active" while pursuing — during windup/leap/recovery the boss cannot enter the
	// attack state in the AnimBP, so the bool is false there.
	if (State != EBossState::Pursuing || !IsValid(Target))
	{
		bInPursueContactRange = false;
		return;
	}

	// Use closest-point-on-collision (like the base class TryContactAttack) so a long, rectangular
	// chassis registers contact uniformly on all sides instead of only on the short axis.
	float Dist = TNumericLimits<float>::Max();
	if (UPrimitiveComponent* TargetPrim = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		FVector ClosestPt;
		const float D = TargetPrim->GetClosestPointOnCollision(GetActorLocation(), ClosestPt);
		if (D >= 0.f) Dist = D;
	}
	else
	{
		FVector PlanarTo = Target->GetActorLocation() - GetActorLocation();
		PlanarTo.Z = 0.f;
		Dist = PlanarTo.Size();
	}

	bInPursueContactRange = (Dist <= PursueContactDamageRange);
}

void AEnemyBoss::ApplyPursueDamageNow()
{
	// Anim-driven entry point. Called by AnimNotify on the "hit frame" of the pursue attack
	// animation. Self-checks so it's safe to leave a notify in the asset even if the boss
	// transitions out of Pursuing mid-anim.

	// Gate 1: must be in Pursuing state. If the boss got interrupted (started a leap, was
	// just damaged into Recovery), the swing should NOT connect.
	if (State != EBossState::Pursuing)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Boss] ApplyPursueDamageNow ignored — state=%d not Pursuing"), (int32)State);
		return;
	}

	// Gate 2: target must exist and be alive.
	if (!IsValid(Target) || PursueContactDamage <= 0.f) return;

	// Gate 3: target must be in range AT THE INSTANT OF THE HIT (not when the anim started).
	// This gives the player a real "dodge by moving" window mid-swing.
	float Dist = TNumericLimits<float>::Max();
	if (UPrimitiveComponent* TargetPrim = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		FVector ClosestPt;
		const float D = TargetPrim->GetClosestPointOnCollision(GetActorLocation(), ClosestPt);
		if (D >= 0.f) Dist = D;
	}
	else
	{
		FVector PlanarTo = Target->GetActorLocation() - GetActorLocation();
		PlanarTo.Z = 0.f;
		Dist = PlanarTo.Size();
	}

	if (Dist > PursueContactDamageRange)
	{
		UE_LOG(LogTemp, Log, TEXT("[Boss] Pursue swing whiff — target moved out of range (%.0f > %.0f)"),
			Dist, PursueContactDamageRange);
		return;
	}

	// Gate 4: defensive cooldown — if a short/looped anim fires the notify too fast, drop hits
	// that arrive within PursueDamageMinInterval of the previous successful hit.
	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if ((Now - LastPursueHitTime) < PursueDamageMinInterval)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Boss] Pursue notify rate-limited (last hit %.2fs ago < %.2fs)"),
			Now - LastPursueHitTime, PursueDamageMinInterval);
		return;
	}
	LastPursueHitTime = Now;

	const float Damage = ScaledDamage(PursueContactDamage);
	FDamageEvent DmgEvent;
	Target->TakeDamage(Damage, DmgEvent, GetController(), this);

	OnPursueContactHit(Target, Damage);

	UE_LOG(LogTemp, Log, TEXT("[Boss] Pursue swing landed on %s for %.1f"),
		*GetNameSafe(Target), Damage);
}

// ===== Charging Leap =====

void AEnemyBoss::EnterChargingLeap()
{
	State = EBossState::ChargingLeap;
	StateTimer = ScaledWindup(LeapWindupDuration);

	// Stop any residual movement so the windup is a clean stand-still pose.
	if (Movement)
	{
		Movement->StopMovementImmediately();
	}

	// Drop the in-range flag so the AnimBP exits the attack state immediately (the leap
	// telegraph anim takes over).
	bInPursueContactRange = false;

	const FVector TgtLoc = IsValid(Target) ? Target->GetActorLocation() : GetActorLocation();
	OnEnterChargingLeap(TgtLoc);

	UE_LOG(LogTemp, Log, TEXT("[Boss] %s -> ChargingLeap (%.2fs, phase2=%d)"),
		*GetNameSafe(this), StateTimer, bPhase2 ? 1 : 0);
}

void AEnemyBoss::TickChargingLeap(float DeltaTime)
{
	StateTimer -= DeltaTime;

	// Track the player with rotation during windup so the leap snapshot at lock-time is fair.
	if (IsValid(Target))
	{
		FVector ToTgt = Target->GetActorLocation() - GetActorLocation();
		ToTgt.Z = 0.f;
		const FVector Dir = ToTgt.GetSafeNormal();
		if (!Dir.IsNearlyZero())
		{
			const FRotator NewRot = FMath::RInterpConstantTo(GetActorRotation(),
				FRotator(0.f, Dir.Rotation().Yaw, 0.f), DeltaTime, TurnRateDeg);
			SetActorRotation(NewRot);
		}
	}

	// Telegraph: yellow circle on the ground at the predicted landing (live, not locked yet).
	if (bDrawDebugTelegraph && IsValid(Target) && GetWorld())
	{
		DrawDebugCircle(GetWorld(), Target->GetActorLocation(), LeapImpactRadius, 24,
			FColor::Yellow, false, -1.f, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}

	// BIE per-tick — drives the BP ground marker so it follows the player live.
	if (IsValid(Target))
	{
		OnLeapTelegraphTick(Target->GetActorLocation());
	}

	if (StateTimer <= 0.f)
	{
		EnterLeaping();
	}
}

// ===== Leaping =====

void AEnemyBoss::EnterLeaping()
{
	OnExitChargingLeap();

	State = EBossState::Leaping;
	StateTimer = LeapTravelDuration;

	// Snapshot the path. The leap is committed: even if the player dodges, we still land here.
	LeapStartLoc  = GetActorLocation();
	LeapTargetLoc = IsValid(Target) ? Target->GetActorLocation() : LeapStartLoc;
	LeapTargetLoc.Z = LeapStartLoc.Z; // simplification: land at the same Z as launch

	// Make sure no leftover velocity drives us off the curve mid-flight.
	if (Movement)
	{
		Movement->StopMovementImmediately();
	}

	const FVector ApexPoint = (LeapStartLoc + LeapTargetLoc) * 0.5f + FVector(0, 0, LeapApexHeight);
	OnEnterLeaping(ApexPoint, LeapTargetLoc);

	if (bDrawDebugTelegraph && GetWorld())
	{
		// Red circle on the ground = locked landing. Player has LeapTravelDuration to clear it.
		DrawDebugCircle(GetWorld(), LeapTargetLoc, LeapImpactRadius, 24,
			FColor::Red, false, LeapTravelDuration + 0.5f, 0, 5.f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
	}

	UE_LOG(LogTemp, Log, TEXT("[Boss] %s -> Leaping  start=%s -> target=%s  duration=%.2fs"),
		*GetNameSafe(this), *LeapStartLoc.ToCompactString(), *LeapTargetLoc.ToCompactString(),
		LeapTravelDuration);
}

void AEnemyBoss::TickLeaping(float DeltaTime)
{
	StateTimer -= DeltaTime;

	const float TotalTime = FMath::Max(0.001f, LeapTravelDuration);
	const float t = FMath::Clamp(1.f - (StateTimer / TotalTime), 0.f, 1.f);

	// Linear XY interpolation between start and locked target.
	FVector NewLoc = FMath::Lerp(LeapStartLoc, LeapTargetLoc, t);

	// Parabolic Z curve: 4*h*t*(1-t) peaks at t=0.5 with height = LeapApexHeight.
	const float ApexLift = 4.f * LeapApexHeight * t * (1.f - t);
	NewLoc.Z = FMath::Lerp(LeapStartLoc.Z, LeapTargetLoc.Z, t) + ApexLift;

	// Direct teleport (no sweep) — the boss is supposed to glide over enemies/light cover.
	SetActorLocation(NewLoc, /*bSweep=*/ false);

	if (StateTimer <= 0.f)
	{
		// Force-land at the exact target so the impact circle and the boss agree.
		SetActorLocation(LeapTargetLoc, /*bSweep=*/ false);

		ApplyLandingDamage(LeapTargetLoc);
		OnLanded(LeapTargetLoc);

		// Phase 2 shortens the cooldown so the boss leaps more often when enraged.
		LeapCooldownLeft = bPhase2 ? LeapCooldown * Phase2LeapCooldownMultiplier : LeapCooldown;
		EnterRecovery(LandingRecoveryDuration);
	}
}

float AEnemyBoss::GetLeapProgress() const
{
	if (State != EBossState::Leaping) return 0.f;
	const float TotalTime = FMath::Max(0.001f, LeapTravelDuration);
	return FMath::Clamp(1.f - (StateTimer / TotalTime), 0.f, 1.f);
}

float AEnemyBoss::GetLeapCooldownNormalized() const
{
	// Use the phase-aware cooldown duration as the denominator so the bar fills up at the same
	// visual rate in phase 1 and phase 2 (in phase 2 the absolute time is shorter, but the
	// 0..1 progress reads the same to the BP).
	const float Total = bPhase2 ? LeapCooldown * Phase2LeapCooldownMultiplier : LeapCooldown;
	if (Total <= 0.f) return 1.f;
	return FMath::Clamp(1.f - (LeapCooldownLeft / Total), 0.f, 1.f);
}

void AEnemyBoss::ApplyLandingDamage(const FVector& LandingLoc)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float Damage = ScaledDamage(LeapImpactDamage);

	// Sphere overlap at landing point. We damage Pawn + PhysicsBody (the player vehicle is
	// PhysicsActor → PhysicsBody object channel). Other enemies are filtered out by class check
	// to avoid friendly fire.
	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Shape = FCollisionShape::MakeSphere(LeapImpactRadius);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BossLanding), false, this);
	Params.AddIgnoredActor(this);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	World->OverlapMultiByObjectType(Overlaps, LandingLoc, FQuat::Identity, ObjectParams, Shape, Params);

	int32 HitCount = 0;
	for (const FOverlapResult& O : Overlaps)
	{
		AActor* HitActor = O.GetActor();
		if (!HitActor || HitActor == this) continue;
		if (HitActor->IsA(AEnemyBase::StaticClass())) continue; // no friendly fire

		FDamageEvent DmgEvent;
		HitActor->TakeDamage(Damage, DmgEvent, GetController(), this);
		++HitCount;
	}

	if (bDrawDebugTelegraph)
	{
		DrawDebugSphere(World, LandingLoc, LeapImpactRadius, 24, FColor::Orange, false, 0.5f, 0, 4.f);
	}

	UE_LOG(LogTemp, Log, TEXT("[Boss] Landing AOE @ %s — hit %d actor(s) for %.1f"),
		*LandingLoc.ToCompactString(), HitCount, Damage);
}

// ===== Recovery / Pursuing =====

void AEnemyBoss::EnterRecovery(float Duration)
{
	State = EBossState::Recovery;
	StateTimer = Duration;

	if (Movement)
	{
		Movement->StopMovementImmediately();
	}

	// Drop the in-range flag so the AnimBP exits the attack state — boss is stunned, no swings.
	bInPursueContactRange = false;

	UE_LOG(LogTemp, Log, TEXT("[Boss] %s -> Recovery (%.2fs)"), *GetNameSafe(this), Duration);
}

void AEnemyBoss::TickRecovery(float DeltaTime)
{
	StateTimer -= DeltaTime;
	if (StateTimer <= 0.f)
	{
		EnterPursuing();
	}
}

void AEnemyBoss::EnterPursuing()
{
	State = EBossState::Pursuing;
	StateTimer = 0.f;
	UE_LOG(LogTemp, Verbose, TEXT("[Boss] %s -> Pursuing"), *GetNameSafe(this));
}

// ===== Phase 2 transition =====

void AEnemyBoss::OnDamaged(float Amount, AActor* Causer)
{
	Super::OnDamaged(Amount, Causer);

	if (!Health) return;

	const float HpFrac = Health->GetHealthNormalized();

	// Adds threshold (independent of Phase 2 — designer can stack them at the same value
	// for a single big "I'm hurt" moment, or stagger them at e.g. 70% / 50%).
	if (!bAddsSpawned && HpFrac <= AddSpawnHealthFraction)
	{
		// Mark as triggered REGARDLESS of whether spawn succeeds, so we don't spam this branch
		// every frame after the threshold is crossed.
		bAddsSpawned = true;

		UE_LOG(LogTemp, Log, TEXT("[Boss] Adds threshold crossed (HP=%.0f%% <= %.0f%%) — Count=%d, Class=%s"),
			HpFrac * 100.f, AddSpawnHealthFraction * 100.f,
			AddSpawnCount,
			AddSpawnClass ? *AddSpawnClass->GetName() : TEXT("(NONE)"));

		if (!AddSpawnClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[Boss] Cannot summon adds: AddSpawnClass is None! "
				"Open BP_Boss → Class Defaults → Boss|Adds → set 'Add Spawn Class' to BP_Chaser (or another EnemyBase subclass)."));
		}
		else if (AddSpawnCount <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Boss] AddSpawnCount <= 0; nothing to summon."));
		}
		else
		{
			SpawnAdds();
		}
	}

	if (!bPhase2 && HpFrac <= Phase2HealthFraction)
	{
		bPhase2 = true;
		OnEnterPhase2();
		UE_LOG(LogTemp, Log, TEXT("[Boss] PHASE 2 — windup ×%.2f, damage ×%.2f"),
			Phase2WindupMultiplier, Phase2DamageMultiplier);
	}
}

void AEnemyBoss::SpawnAdds()
{
	UWorld* World = GetWorld();
	if (!World || !AddSpawnClass || AddSpawnCount <= 0) return;

	const FVector Center = GetActorLocation();
	const float AngleStep = 2.f * PI / FMath::Max(1, AddSpawnCount);

	// Random rotation offset so the ring isn't always axis-aligned.
	const float StartAngle = FMath::FRandRange(0.f, 2.f * PI);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	TArray<AEnemyBase*> SpawnedAdds;
	SpawnedAdds.Reserve(AddSpawnCount);

	for (int32 i = 0; i < AddSpawnCount; ++i)
	{
		const float Angle = StartAngle + AngleStep * i;
		const FVector Offset(FMath::Cos(Angle) * AddSpawnRadius,
		                     FMath::Sin(Angle) * AddSpawnRadius,
		                     0.f);
		const FVector Loc = Center + Offset;

		// Face the center (toward the player who's near the boss).
		const FRotator Rot = (-Offset.GetSafeNormal2D()).Rotation();

		AEnemyBase* Add = World->SpawnActor<AEnemyBase>(AddSpawnClass, Loc, Rot, Params);
		if (Add)
		{
			SpawnedAdds.Add(Add);
			// Track via weak ptr so we can wipe survivors when the boss dies. Weak ptrs
			// auto-null on actor destruction, so the OnDeath sweep only touches live ones.
			TrackedAdds.Add(Add);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Boss] Summoned %d adds at HP %.0f%% (radius %.0f cm)"),
		SpawnedAdds.Num(),
		Health ? Health->GetHealthNormalized() * 100.f : -1.f,
		AddSpawnRadius);

	if (bDrawDebugTelegraph)
	{
		DrawDebugSphere(World, Center, AddSpawnRadius, 24, FColor::Purple, false, 1.0f, 0, 4.f);
	}

	OnSpawnAdds(SpawnedAdds);
}

void AEnemyBoss::OnDeath(AActor* Causer)
{
	// The boss's adds are its summons — when the master falls, they fall too. We use
	// HealthComponent::Kill (not Destroy) so each add's OnDeath/OnDeathFX BIE chain still
	// fires, giving you a free "death cascade" VFX moment. Weak ptrs may have nulled if some
	// adds died naturally during the fight; we skip those.
	int32 KilledCount = 0;
	for (const TWeakObjectPtr<AEnemyBase>& AddPtr : TrackedAdds)
	{
		AEnemyBase* Add = AddPtr.Get();
		if (!IsValid(Add)) continue;

		UHealthComponent* HC = Add->GetHealthComponent();
		if (!HC || HC->IsDead()) continue;

		HC->Kill(this);
		++KilledCount;
	}
	TrackedAdds.Reset();

	if (KilledCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[Boss] Death cascade — wiped %d surviving add(s)."), KilledCount);
	}

	// Hand off to the base class which calls Destroy() on the boss itself.
	Super::OnDeath(Causer);
}
