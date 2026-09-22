// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnHealthDamaged,
	UHealthComponent*, HealthComp, float, Amount, float, NewHealth, AActor*, Causer);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthDeath,
	UHealthComponent*, HealthComp, AActor*, Causer);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WHISKYCOUNTY_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ===== Tunables =====
	// BlueprintReadWrite so power-ups (e.g. Officina mobile) can bump it at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	// If true, ApplyDamage is a no-op. Use for ram i-frames or scripted invulnerability.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	bool bInvulnerable = false;

	// HP regenerated per second. 0 = no regen (default). Officina mobile powerup raises this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float RegenRate = 0.f;

	// Toggle UE_LOG output. Leave on while wiring things up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Debug")
	bool bLogHealthEvents = true;

	// ===== API =====
	// Returns the damage actually applied (0 if dead, invulnerable, or non-positive amount).
	UFUNCTION(BlueprintCallable, Category = "Health")
	float ApplyDamage(float Amount, AActor* Causer);

	// Returns the HP actually restored (0 if dead or non-positive amount).
	UFUNCTION(BlueprintCallable, Category = "Health")
	float Heal(float Amount);

	// Instakill — bypasses bInvulnerable. Broadcasts OnDamaged then OnDeath.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Kill(AActor* Causer);

	// Reset to full HP. If NewMax > 0, also overrides MaxHealth.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ResetHealth(float NewMax = 0.f);

	// ===== Read-only state (for HUD bindings) =====
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthNormalized() const { return FMath::Clamp(CurrentHealth / FMath::Max(1.f, MaxHealth), 0.f, 1.f); }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	// ===== Events (BlueprintAssignable so UMG / BP can bind) =====
	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnHealthDamaged OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnHealthDeath OnDeath;

private:
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Health|State", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Health|State", meta = (AllowPrivateAccess = "true"))
	bool bDead = false;
};
