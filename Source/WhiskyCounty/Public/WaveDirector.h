// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "WaveDefinition.h"
#include "WaveDirector.generated.h"

class UWaveDefinition;
class ASpawnPoint;
class AEnemyBase;
class UHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaveStarted,
	AWaveDirector*, Director, int32, WaveIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaveEnded,
	AWaveDirector*, Director, int32, WaveIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAllWavesCompleted,
	AWaveDirector*, Director);

// Fires every time an enemy dies. NewKillCount is the running total for the run.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyKilled,
	AWaveDirector*, Director, int32, NewKillCount);

// Fires when LiveEnemyCount changes (spawn or death). Useful for "Nemici rimanenti" HUD.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLiveEnemyCountChanged,
	AWaveDirector*, Director, int32, NewLiveCount);

// Fires when an enemy dies, with the dead actor + causer + location. Used by powerups
// like Onda d'urto that need spatial info on the kill.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnEnemyDied,
	AWaveDirector*, Director, AEnemyBase*, Enemy, AActor*, Causer, FVector, KillLocation);

UCLASS()
class WHISKYCOUNTY_API AWaveDirector : public AActor
{
	GENERATED_BODY()

public:
	AWaveDirector();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaTime) override;

	// ===== Designer-facing =====
	UPROPERTY(EditAnywhere, Category = "Waves")
	TArray<TObjectPtr<UWaveDefinition>> Waves;

	// If true, BeginWave(0) is called automatically on BeginPlay.
	UPROPERTY(EditAnywhere, Category = "Waves")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, Category = "Waves|Debug")
	bool bLogWaveEvents = true;

	// ===== API =====
	UFUNCTION(BlueprintCallable, Category = "Waves")
	void BeginWave(int32 WaveIndex);

	UFUNCTION(BlueprintCallable, Category = "Waves")
	void BeginNextWave();

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetCurrentWaveIndex() const { return CurrentWaveIndex; }

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetLiveEnemyCount() const { return LiveEnemyCount; }

	// How many enemies of a specific class are currently alive (spawned by this director
	// and not yet dead). Returns 0 if the class isn't represented.
	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetLiveCountOfClass(TSubclassOf<AEnemyBase> EnemyClass) const;

	// Snapshot of live counts grouped by enemy class. Only classes with >0 alive are present,
	// so iterating gives the HUD exactly the rows it needs.
	UFUNCTION(BlueprintPure, Category = "Waves")
	TMap<TSubclassOf<AEnemyBase>, int32> GetLiveCountsByClass() const { return LiveCountByClass; }

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetTotalEnemiesKilled() const { return TotalEnemiesKilled; }

	// Number of waves configured on the director (= Waves.Num()).
	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetTotalWaveCount() const { return Waves.Num(); }

	UFUNCTION(BlueprintPure, Category = "Waves")
	bool IsWaveActive() const { return bWaveActive; }

	// Composition of the current wave (per-entry: enemy class + count + spawn timing).
	UFUNCTION(BlueprintPure, Category = "Waves")
	TArray<FEnemySpawnEntry> GetCurrentWaveEntries() const;

	// Same as above, but for any wave index (useful for the HUD to peek next wave's content).
	UFUNCTION(BlueprintPure, Category = "Waves")
	TArray<FEnemySpawnEntry> GetWaveEntries(int32 WaveIndex) const;

	// Sum of Count across all entries of the current wave (= total enemies expected to spawn).
	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetCurrentWaveTotalEnemies() const;

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetWaveTotalEnemies(int32 WaveIndex) const;

	// Seconds since the run started (Wave 0 begin). Frozen on game over / all waves completed.
	UFUNCTION(BlueprintPure, Category = "Waves")
	float GetSurvivalTime() const { return SurvivalTime; }

	// Convenience for HUD: "MM:SS" formatted version of GetSurvivalTime.
	UFUNCTION(BlueprintPure, Category = "Waves")
	FString GetSurvivalTimeFormatted() const;

	UFUNCTION(BlueprintPure, Category = "Waves")
	bool IsSurvivalTimerActive() const { return bSurvivalTimerActive; }

	// Freezes the timer at its current value (e.g. on game over).
	UFUNCTION(BlueprintCallable, Category = "Waves")
	void StopSurvivalTimer() { bSurvivalTimerActive = false; }

	// ===== Events (BlueprintAssignable for UMG / BP) =====
	UPROPERTY(BlueprintAssignable, Category = "Waves|Events")
	FOnWaveStarted OnWaveStarted;

	// Fires when all entries are spawned AND every spawned enemy is dead.
	// Hook this from your reward UMG.
	UPROPERTY(BlueprintAssignable, Category = "Waves|Events")
	FOnWaveEnded OnWaveEnded;

	UPROPERTY(BlueprintAssignable, Category = "Waves|Events")
	FOnAllWavesCompleted OnAllWavesCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Waves|Events")
	FOnEnemyKilled OnEnemyKilled;

	UPROPERTY(BlueprintAssignable, Category = "Waves|Events")
	FOnLiveEnemyCountChanged OnLiveEnemyCountChanged;

	UPROPERTY(BlueprintAssignable, Category = "Waves|Events")
	FOnEnemyDied OnEnemyDied;

private:
	UFUNCTION()
	void HandleEnemyDeath(UHealthComponent* HealthComp, AActor* Causer);

	void SpawnOneFromEntry(int32 EntryIndex);

	void RefreshSpawnPoints();
	ASpawnPoint* PickSpawnPoint() const;

	// Filtered variant: returns a spawn point whose Actor Tags array contains Tag. If multiple
	// matches exist, picks one at random. Returns nullptr if no spawn point has that tag.
	ASpawnPoint* PickSpawnPointByTag(FName Tag) const;

	void TryEndWave();
	void ClearWaveTimers();

	int32 CurrentWaveIndex = INDEX_NONE;
	int32 LiveEnemyCount = 0;

	// Per-class live count, parallel to LiveEnemyCount. Updated on spawn / death.
	TMap<TSubclassOf<AEnemyBase>, int32> LiveCountByClass;

	int32 TotalEnemiesKilled = 0; // accumulates across all waves for the run
	int32 EntriesFullySpawned = 0;
	bool  bWaveActive = false;

	// Survival timer (seconds since BeginWave(0)). Tick-driven.
	float SurvivalTime = 0.f;
	bool  bSurvivalTimerActive = false;

	// Per-entry tracking for the active wave (sized to current wave's Entries.Num()).
	TArray<int32> EntrySpawnedCount;
	TArray<FTimerHandle> EntryTimers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASpawnPoint>> SpawnPoints;
};
