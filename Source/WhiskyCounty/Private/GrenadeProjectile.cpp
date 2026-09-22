// Fill out your copyright notice in the Description page of Project Settings.

#include "GrenadeProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AGrenadeProjectile::AGrenadeProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(15.f);
	CollisionSphere->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	CollisionSphere->OnComponentHit.AddDynamic(this, &AGrenadeProjectile::OnHit);
	RootComponent = CollisionSphere;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bAutoActivate = true;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.3f;
	ProjectileMovement->Friction = 0.5f;
	ProjectileMovement->ProjectileGravityScale = 1.5f;
	ProjectileMovement->bRotationFollowsVelocity = false;
	ProjectileMovement->InitialSpeed = 0.f; // set by spawner
	ProjectileMovement->MaxSpeed = 6000.f;

	InitialLifeSpan = 5.f; // safety net if Detonate never fires
}

void AGrenadeProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (FuseTime > 0.f)
	{
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AGrenadeProjectile::Detonate, FuseTime, false);
	}
}

void AGrenadeProjectile::OnHit(UPrimitiveComponent* /*HitComp*/, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComp*/,
	FVector /*NormalImpulse*/, const FHitResult& /*Hit*/)
{
	if (bDetonateOnHit)
	{
		Detonate();
	}
}

void AGrenadeProjectile::Detonate()
{
	if (bDetonated) return;
	bDetonated = true;

	if (ExplosionRadius > 0.f && ExplosionDamage > 0.f)
	{
		TArray<AActor*> IgnoreActors;
		// Don't damage the player vehicle that fired this grenade — Owner is the firing pawn.
		if (AActor* MyOwner = GetOwner())
		{
			IgnoreActors.Add(MyOwner);
		}
		UGameplayStatics::ApplyRadialDamage(
			this, ExplosionDamage, GetActorLocation(), ExplosionRadius,
			UDamageType::StaticClass(), IgnoreActors, this, GetInstigatorController(), /*bDoFullDamage=*/ true);
	}

	if (bDrawDebug)
	{
		if (UWorld* World = GetWorld())
		{
			DrawDebugSphere(World, GetActorLocation(), ExplosionRadius, 24, FColor::Yellow, false, 1.5f, 0, 3.f);
		}
	}

	OnExplode(GetActorLocation());

	Destroy();
}
