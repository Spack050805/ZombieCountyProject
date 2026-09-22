// Fill out your copyright notice in the Description page of Project Settings.

#include "SniperProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EnemyBase.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"

ASniperProjectile::ASniperProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(15.f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// Phase through other enemies (Pawn channel). The blast still damages them via
	// ApplyRadialDamage on detonation; the projectile just shouldn't be eaten by chasers.
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	CollisionSphere->SetGenerateOverlapEvents(false);
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	SetRootComponent(CollisionSphere);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(CollisionSphere);
	ProjectileMovement->bAutoActivate = true;
	ProjectileMovement->bShouldBounce = false;
	// Initial speed left at 0 — the sniper sets the toss velocity directly after spawn so the
	// trajectory matches the parabolic solution from SuggestProjectileVelocity_CustomArc.
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->MaxSpeed = 4000.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->ProjectileGravityScale = 1.f;
}

void ASniperProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (CollisionSphere)
	{
		CollisionSphere->OnComponentHit.AddDynamic(this, &ASniperProjectile::OnHit);
		if (AActor* MyOwner = GetOwner())
		{
			CollisionSphere->IgnoreActorWhenMoving(MyOwner, true);
		}
	}

	SetLifeSpan(LifeSeconds);
}

void ASniperProjectile::OnHit(UPrimitiveComponent* /*HitComp*/, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                              FVector /*NormalImpulse*/, const FHitResult& Hit)
{
	UE_LOG(LogTemp, Log, TEXT("[SniperProj] OnHit at %s (actor=%s comp=%s normal=%s travel=%.0fcm)"),
		*GetActorLocation().ToCompactString(),
		*GetNameSafe(OtherActor),
		OtherComp ? *OtherComp->GetName() : TEXT("?"),
		*Hit.ImpactNormal.ToCompactString(),
		FVector::Distance(GetActorLocation(), Hit.TraceStart));
	Detonate();
}

void ASniperProjectile::Detonate()
{
	if (bDetonated) return;
	bDetonated = true;

	if (ExplosionRadius > 0.f && ExplosionDamage > 0.f)
	{
		// Sniper bombs damage ONLY the player. We ignore the firing sniper AND every other
		// enemy in the world — no friendly fire on chasers/hunters/snipers.
		TArray<AActor*> IgnoreActors;
		if (AActor* MyOwner = GetOwner())
		{
			IgnoreActors.Add(MyOwner);
		}

		TArray<AActor*> Enemies;
		UGameplayStatics::GetAllActorsOfClass(this, AEnemyBase::StaticClass(), Enemies);
		IgnoreActors.Append(Enemies);

		UGameplayStatics::ApplyRadialDamage(
			this, ExplosionDamage, GetActorLocation(), ExplosionRadius,
			UDamageType::StaticClass(), IgnoreActors, this, GetInstigatorController(),
			/*bDoFullDamage=*/ true);
	}

	if (bDrawDebug)
	{
		if (UWorld* World = GetWorld())
		{
			DrawDebugSphere(World, GetActorLocation(), ExplosionRadius, 16, FColor::Red, false, 0.5f, 0, 2.f);
		}
	}

	OnExplode(GetActorLocation());

	Destroy();
}
