// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrenadeProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

UCLASS()
class WHISKYCOUNTY_API AGrenadeProjectile : public AActor
{
	GENERATED_BODY()

public:
	AGrenadeProjectile();

	UPROPERTY(VisibleAnywhere, Category = "Grenade|Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, Category = "Grenade|Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Grenade|Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditAnywhere, Category = "Grenade", meta = (ClampMin = "0"))
	float ExplosionRadius = 400.f;

	UPROPERTY(EditAnywhere, Category = "Grenade", meta = (ClampMin = "0"))
	float ExplosionDamage = 500.f;

	// Time before auto-detonation (also bounces during this window)
	UPROPERTY(EditAnywhere, Category = "Grenade", meta = (ClampMin = "0"))
	float FuseTime = 2.5f;

	// If true, detonates on first hit; if false, bounces until FuseTime expires
	UPROPERTY(EditAnywhere, Category = "Grenade")
	bool bDetonateOnHit = true;

	UPROPERTY(EditAnywhere, Category = "Grenade")
	bool bDrawDebug = true;

	// Override in BP to spawn the explosion VFX + sound at the impact location.
	UFUNCTION(BlueprintImplementableEvent, Category = "Grenade|FX")
	void OnExplode(const FVector& Location);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	           FVector NormalImpulse, const FHitResult& Hit);

	void Detonate();

private:
	FTimerHandle FuseTimerHandle;
	bool bDetonated = false;
};
