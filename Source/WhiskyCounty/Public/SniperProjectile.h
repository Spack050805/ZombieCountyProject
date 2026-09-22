// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SniperProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

UCLASS()
class WHISKYCOUNTY_API ASniperProjectile : public AActor
{
	GENERATED_BODY()

public:
	ASniperProjectile();

	UPROPERTY(VisibleAnywhere, Category = "Sniper|Projectile")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, Category = "Sniper|Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Sniper|Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// Damage at the center of the explosion (full damage applied within the radius).
	UPROPERTY(EditAnywhere, Category = "Sniper|Projectile", meta = (ClampMin = "0"))
	float ExplosionDamage = 35.f;

	// Radius of the AOE blast on impact.
	UPROPERTY(EditAnywhere, Category = "Sniper|Projectile", meta = (ClampMin = "0"))
	float ExplosionRadius = 350.f;

	UPROPERTY(EditAnywhere, Category = "Sniper|Projectile", meta = (ClampMin = "0.1"))
	float LifeSeconds = 5.f;

	UPROPERTY(EditAnywhere, Category = "Sniper|Projectile")
	bool bDrawDebug = true;

	// Override in BP to spawn the explosion VFX + sound at the impact location.
	UFUNCTION(BlueprintImplementableEvent, Category = "Sniper|Projectile|FX")
	void OnExplode(const FVector& Location);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	           FVector NormalImpulse, const FHitResult& Hit);

	void Detonate();

private:
	bool bDetonated = false;
};
