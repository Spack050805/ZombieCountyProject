// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// UFloatingPawnMovement only ticks when the pawn has a local controller. Without this,
	// runtime-spawned enemies stand still because no controller possesses them.
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	CollisionCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionCapsule"));
	CollisionCapsule->InitCapsuleSize(50.f, 100.f); // radius 50cm, half-height 100cm
	CollisionCapsule->SetCollisionProfileName(TEXT("Pawn"));
	// UE5's default Pawn profile ignores the Visibility channel, but our weapon
	// hitscan uses Visibility traces. Force Block so weapons can hit the enemy.
	CollisionCapsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// Enemies pass through each other instead of forming walls in a swarm.
	CollisionCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	// Also overlap the player vehicle (PhysicsBody): the simulating chassis was launching
	// kinematic capsules upward via physics resolution. Contact damage works via distance,
	// not collision response, so it still triggers. Ramming (D5) will be wired via overlap
	// events on the vehicle, not via physical collision.
	CollisionCapsule->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	RootComponent = CollisionCapsule;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = MoveSpeed;
	Movement->Acceleration = 2000.f;
	Movement->Deceleration = 2000.f;

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	if (Movement)
	{
		Movement->MaxSpeed = MoveSpeed;
	}

	if (Health)
	{
		Health->OnDamaged.AddDynamic(this, &AEnemyBase::HandleHealthDamaged);
		Health->OnDeath.AddDynamic(this, &AEnemyBase::HandleHealthDeath);
	}

	// Random angular slot — each enemy seeks its own point on the ring around the player.
	AngularOffsetRad = FMath::FRandRange(0.f, 2.f * PI);

	RefreshTarget();

	// Spawn-in FX hook: fires AFTER all init is done, so the BP can read GetActorLocation /
	// Mesh / etc. without surprises. Designer overrides in BP_Chaser/Sniper/Hunter/Boss to
	// spawn the materialization effect.
	OnSpawnFX();
}

void AEnemyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Health && Health->IsDead()) return;

	UpdateMovement(DeltaTime);
	ApplySeparation();
	TryContactAttack(DeltaTime);

	if (bSnapToGround)
	{
		SnapToGround();
	}
}

void AEnemyBase::SnapToGround()
{
	if (!CollisionCapsule) return;
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector ActorLoc = GetActorLocation();
	const float HalfHeight = CollisionCapsule->GetScaledCapsuleHalfHeight();

	const FVector TraceStart = ActorLoc + FVector::UpVector * GroundTraceOriginUpOffset;
	const FVector TraceEnd   = ActorLoc - FVector::UpVector * (HalfHeight + GroundTraceMaxDrop);

	// Query only world geometry (static + dynamic). Skipping pawns prevents enemies from
	// snapping onto the heads of other enemies and stacking upward each frame.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyGroundSnap), false, this);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectParams, QueryParams))
	{
		FVector NewLoc = ActorLoc;
		NewLoc.Z = Hit.ImpactPoint.Z + HalfHeight;
		SetActorLocation(NewLoc, /*bSweep=*/ false);
	}
}

void AEnemyBase::RefreshTarget()
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		Target = PC->GetPawn();
	}
}

void AEnemyBase::UpdateMovement(float DeltaTime)
{
	if (!IsValid(Target))
	{
		RefreshTarget();
		if (!IsValid(Target)) return;
	}

	// Always face the target — runs whether we're moving or stopped at the anchor, so
	// stationary attack animations (melee swings) play with the enemy facing the player.
	{
		FVector ToPlayer = Target->GetActorLocation() - GetActorLocation();
		ToPlayer.Z = 0.f;
		const FVector FaceDir = ToPlayer.GetSafeNormal();
		if (!FaceDir.IsNearlyZero())
		{
			const FRotator CurrentRot   = GetActorRotation();
			const FRotator TargetYawRot = FRotator(0.f, FaceDir.Rotation().Yaw, 0.f);
			const FRotator NewRot       = FMath::RInterpConstantTo(CurrentRot, TargetYawRot, DeltaTime, TurnRateDeg);
			SetActorRotation(NewRot);
		}
	}

	// Anchor = a point on the ring of radius AcceptanceRadius around the player, at this
	// enemy's angular slot. Seeking the anchor (instead of the player center) spreads the
	// swarm into a ring instead of a blob.
	const FVector AnchorOffset = FVector(FMath::Cos(AngularOffsetRad),
	                                     FMath::Sin(AngularOffsetRad),
	                                     0.f) * AcceptanceRadius;
	const FVector AnchorWorld = Target->GetActorLocation() + AnchorOffset;

	// Steering point = either the next NavMesh waypoint (so we route around walls) or
	// the anchor directly if NavMesh pathing is disabled / unavailable.
	const FVector SteerTarget = bUseNavMeshPathing
		? GetNextNavSteeringPoint(AnchorWorld, DeltaTime)
		: AnchorWorld;

	FVector ToSteer = SteerTarget - GetActorLocation();
	ToSteer.Z = 0.f;
	const float DistToSteer = ToSteer.Size();

	// Small slack so the enemy doesn't jitter trying to land exactly on its slot.
	const float StopSlack = 30.f;

	if (DistToSteer > StopSlack)
	{
		const FVector MoveDir = ToSteer.GetSafeNormal();
		AddMovementInput(MoveDir, 1.f);
	}
}

FVector AEnemyBase::GetNextNavSteeringPoint(const FVector& DesiredDestination, float DeltaTime)
{
	// Decide whether to re-query the path. Two triggers:
	//   1) Periodic timer (NavPathRequeryInterval) — keeps paths fresh as the world changes.
	//   2) Target moved more than NavTargetMoveThreshold — react fast when the player drives away.
	NavRequeryTimer -= DeltaTime;

	const float DestMoveDistSq = FVector::DistSquared(LastQueriedDestination, DesiredDestination);
	const bool  bForceRequery  = DestMoveDistSq > FMath::Square(NavTargetMoveThreshold);
	const bool  bNeedRequery   = (NavRequeryTimer <= 0.f) || bForceRequery || (CachedNavPath.Num() == 0);

	if (bNeedRequery)
	{
		NavRequeryTimer = NavPathRequeryInterval;
		LastQueriedDestination = DesiredDestination;
		CachedNavPath.Reset();

		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
		{
			UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(
				GetWorld(),
				GetActorLocation(),
				DesiredDestination,
				/*PathfindingContext=*/ this);

			if (Path && Path->IsValid())
			{
				// Both full and partial paths are useful. Partial = target unreachable, but the
				// closest reachable point is still a sensible direction to walk in.
				CachedNavPath = Path->PathPoints;
			}
		}
	}

	// Pop any waypoints we've already reached. The 0th point is "current location at query
	// time" — once we're close enough to the next, that becomes the new "current" too.
	while (CachedNavPath.Num() >= 2)
	{
		FVector NextPoint = CachedNavPath[1];
		FVector MyPos = GetActorLocation();
		// Compare in 2D so vertical mismatches (path on ground vs actor at capsule center) don't
		// prevent us from advancing to the next waypoint.
		NextPoint.Z = MyPos.Z;
		if (FVector::DistSquared(MyPos, NextPoint) < FMath::Square(NavWaypointReachDist))
		{
			CachedNavPath.RemoveAt(0);
		}
		else
		{
			break;
		}
	}

	if (bDrawNavDebug && GetWorld() && CachedNavPath.Num() >= 2)
	{
		for (int32 i = 0; i < CachedNavPath.Num() - 1; ++i)
		{
			DrawDebugLine(GetWorld(), CachedNavPath[i], CachedNavPath[i + 1],
				FColor::Red, false, /*LifeTime=*/ 0.f, 0, /*Thickness=*/ 3.f);
		}
		DrawDebugSphere(GetWorld(), CachedNavPath.Last(), 30.f, 8, FColor::Yellow,
			false, 0.f, 0, 2.f);
	}

	// Return the next waypoint to steer toward. Fallback to the desired destination if the
	// nav query failed (no NavMeshBoundsVolume covering this area, etc.) — preserves legacy
	// straight-line behavior so the enemy still moves instead of standing still.
	if (CachedNavPath.Num() >= 2)
	{
		return CachedNavPath[1];
	}
	if (CachedNavPath.Num() == 1)
	{
		return CachedNavPath[0];
	}
	return DesiredDestination;
}

void AEnemyBase::ApplySeparation()
{
	if (SeparationRadius <= 0.f || SeparationStrength <= 0.f) return;
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector MyLoc = GetActorLocation();

	// Sphere overlap to find nearby pawns. Cheap enough at slice scale (<30 enemies).
	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Shape = FCollisionShape::MakeSphere(SeparationRadius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySeparation), false, this);
	Params.AddIgnoredActor(this);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	World->OverlapMultiByObjectType(Overlaps, MyLoc, FQuat::Identity, ObjectParams, Shape, Params);

	FVector AvoidanceVector = FVector::ZeroVector;
	int32 Count = 0;
	for (const FOverlapResult& O : Overlaps)
	{
		AActor* Other = O.GetActor();
		if (!Other || Other == this) continue;
		if (!Other->IsA(AEnemyBase::StaticClass())) continue;

		FVector ToMe = MyLoc - Other->GetActorLocation();
		ToMe.Z = 0.f;
		const float Dist = ToMe.Size();
		if (Dist < KINDA_SMALL_NUMBER) continue;

		// Stronger when closer; falls off linearly to 0 at SeparationRadius.
		const float Falloff = FMath::Clamp(1.f - Dist / SeparationRadius, 0.f, 1.f);
		AvoidanceVector += ToMe.GetSafeNormal() * Falloff;
		++Count;
	}

	if (Count > 0)
	{
		AvoidanceVector /= float(Count);
		AddMovementInput(AvoidanceVector, SeparationStrength);
	}
}

float AEnemyBase::GetMovementSpeed() const
{
	return GetVelocity().Size2D();
}

float AEnemyBase::GetForwardSpeed() const
{
	return FVector::DotProduct(GetVelocity(), GetActorForwardVector());
}

void AEnemyBase::TryContactAttack(float DeltaTime)
{
	if (ContactDamage <= 0.f || !IsValid(Target))
	{
		bIsInAttackRange = false;
		return;
	}

	// Distance to the target's collision surface (not its center) so a long, rectangular
	// chassis registers contact uniformly on all sides instead of only on the short axis.
	float Dist = TNumericLimits<float>::Max();
	if (UPrimitiveComponent* TargetPrim = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		FVector ClosestPt;
		const float D = TargetPrim->GetClosestPointOnCollision(GetActorLocation(), ClosestPt);
		// 0 = origin is inside the shape (overlap), >0 = surface distance, <0 = query failed
		if (D >= 0.f) Dist = D;
	}
	else
	{
		// Fallback for targets without a primitive root: planar center-to-center.
		FVector PlanarTo = Target->GetActorLocation() - GetActorLocation();
		PlanarTo.Z = 0.f;
		Dist = PlanarTo.Size();
	}

	if (Dist > ContactDamageRange)
	{
		// Out of range: reset accumulator so re-entering doesn't grant a free instant hit.
		bIsInAttackRange = false;
		ContactAccum = 0.f;
		return;
	}

	bIsInAttackRange = true;
	ContactAccum += DeltaTime;
	if (ContactAccum < ContactDamageInterval) return;

	ContactAccum -= ContactDamageInterval;

	FDamageEvent DamageEvent;
	Target->TakeDamage(ContactDamage, DamageEvent, GetController(), this);
}

float AEnemyBase::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
                             AController* EventInstigator, AActor* DamageCauser)
{
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (!Health) return 0.f;
	return Health->ApplyDamage(DamageAmount, DamageCauser);
}

void AEnemyBase::HandleHealthDamaged(UHealthComponent* /*HealthComp*/, float Amount, float /*NewHealth*/, AActor* Causer)
{
	OnDamaged(Amount, Causer);
	OnDamagedFX(Amount, Causer); // BP override spawns damage numbers, hit flash, etc.
}

void AEnemyBase::HandleHealthDeath(UHealthComponent* /*HealthComp*/, AActor* Causer)
{
	// BP-side FX FIRST (spawn particle at world location), then C++ OnDeath which destroys.
	OnDeathFX(Causer);
	OnDeath(Causer);
}

void AEnemyBase::OnDamaged(float /*Amount*/, AActor* /*Causer*/)
{
	// Subclasses override (stagger, rage mode, audio cues, etc.)
}

void AEnemyBase::OnDeath(AActor* /*Causer*/)
{
	Destroy();
}
