// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WaveDefinition.generated.h"

class AEnemyBase;

USTRUCT(BlueprintType)
struct FEnemySpawnEntry
{
	GENERATED_BODY()

	// Enemy class (typically a Blueprint subclass like BP_Chaser).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	TSubclassOf<AEnemyBase> EnemyClass;

	// How many of this class to spawn during the wave.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "1"))
	int32 Count = 1;

	// Seconds between consecutive spawns of this entry.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.0"))
	float SpawnInterval = 0.5f;

	// Seconds to wait after wave start before this entry begins spawning.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.0"))
	float StartDelay = 0.f;

	// Optional: if non-empty, the WaveDirector will only pick from SpawnPoint actors whose
	// Actor Tags array contains this tag. Tag your SpawnPoint in the Level Details panel
	// (Actor → Tags) and put the same tag here. Empty (None) = random selection from all
	// spawn points (default behavior). Useful to pin the boss to a fixed arena spawn.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FName SpawnPointTag = NAME_None;
};

UCLASS(BlueprintType)
class WHISKYCOUNTY_API UWaveDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// Designer-facing label, shown in the asset preview.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	TArray<FEnemySpawnEntry> Entries;
};
