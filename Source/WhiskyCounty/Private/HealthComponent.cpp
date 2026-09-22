// Fill out your copyright notice in the Description page of Project Settings.

#include "HealthComponent.h"
#include "GameFramework/Actor.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Slow regen (Officina mobile powerup). Skipped if dead, full, or RegenRate == 0.
	if (!bDead && RegenRate > 0.f && CurrentHealth < MaxHealth)
	{
		CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + RegenRate * DeltaTime);
	}
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;
	bDead = false;

	if (bLogHealthEvents)
	{
		UE_LOG(LogTemp, Log, TEXT("[Health] %s spawned with %.1f / %.1f HP"),
			*GetNameSafe(GetOwner()), CurrentHealth, MaxHealth);
	}
}

float UHealthComponent::ApplyDamage(float Amount, AActor* Causer)
{
	if (bDead || bInvulnerable || Amount <= 0.f)
	{
		if (bLogHealthEvents && bInvulnerable && !bDead)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[Health] %s is invulnerable, ignored %.1f damage from %s"),
				*GetNameSafe(GetOwner()), Amount, *GetNameSafe(Causer));
		}
		return 0.f;
	}

	const float Before = CurrentHealth;
	CurrentHealth = FMath::Max(0.f, CurrentHealth - Amount);
	const float Applied = Before - CurrentHealth;

	if (bLogHealthEvents)
	{
		UE_LOG(LogTemp, Log, TEXT("[Health] %s took %.1f damage from %s -> %.1f / %.1f HP"),
			*GetNameSafe(GetOwner()), Applied, *GetNameSafe(Causer), CurrentHealth, MaxHealth);
	}

	OnDamaged.Broadcast(this, Applied, CurrentHealth, Causer);

	if (CurrentHealth <= 0.f)
	{
		bDead = true;

		if (bLogHealthEvents)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Health] %s DIED (caused by %s)"),
				*GetNameSafe(GetOwner()), *GetNameSafe(Causer));
		}

		OnDeath.Broadcast(this, Causer);
	}

	return Applied;
}

float UHealthComponent::Heal(float Amount)
{
	if (bDead || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Before = CurrentHealth;
	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + Amount);
	const float Applied = CurrentHealth - Before;

	if (bLogHealthEvents && Applied > 0.f)
	{
		UE_LOG(LogTemp, Log, TEXT("[Health] %s healed %.1f -> %.1f / %.1f HP"),
			*GetNameSafe(GetOwner()), Applied, CurrentHealth, MaxHealth);
	}

	return Applied;
}

void UHealthComponent::Kill(AActor* Causer)
{
	if (bDead)
	{
		return;
	}

	const float Applied = CurrentHealth;
	CurrentHealth = 0.f;
	bDead = true;

	if (bLogHealthEvents)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Health] %s KILLED by %s (bypassed invulnerability)"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Causer));
	}

	OnDamaged.Broadcast(this, Applied, 0.f, Causer);
	OnDeath.Broadcast(this, Causer);
}

void UHealthComponent::ResetHealth(float NewMax)
{
	if (NewMax > 0.f)
	{
		MaxHealth = NewMax;
	}
	CurrentHealth = MaxHealth;
	bDead = false;

	if (bLogHealthEvents)
	{
		UE_LOG(LogTemp, Log, TEXT("[Health] %s reset to %.1f / %.1f HP"),
			*GetNameSafe(GetOwner()), CurrentHealth, MaxHealth);
	}
}
