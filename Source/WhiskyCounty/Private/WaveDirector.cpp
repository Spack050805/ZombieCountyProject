// Fill out your copyright notice in the Description page of Project Settings.

#include "WaveDirector.h"

#include "EnemyBase.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SpawnPoint.h"
#include "TimerManager.h"
#include "WaveDefinition.h"

AWaveDirector::AWaveDirector()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AWaveDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bSurvivalTimerActive)
	{
		SurvivalTime += DeltaTime;
	}
}

FString AWaveDirector::GetSurvivalTimeFormatted() const
{
	const int32 TotalSec = FMath::FloorToInt(SurvivalTime);
	const int32 Min = TotalSec / 60;
	const int32 Sec = TotalSec % 60;
	return FString::Printf(TEXT("%02d:%02d"), Min, Sec);
}

TArray<FEnemySpawnEntry> AWaveDirector::GetCurrentWaveEntries() const
{
	return GetWaveEntries(CurrentWaveIndex);
}

TArray<FEnemySpawnEntry> AWaveDirector::GetWaveEntries(int32 WaveIndex) const
{
	if (!Waves.IsValidIndex(WaveIndex)) return {};
	UWaveDefinition* Wave = Waves[WaveIndex];
	if (!Wave) return {};
	return Wave->Entries;
}

int32 AWaveDirector::GetCurrentWaveTotalEnemies() const
{
	return GetWaveTotalEnemies(CurrentWaveIndex);
}

int32 AWaveDirector::GetWaveTotalEnemies(int32 WaveIndex) const
{
	int32 Total = 0;
	for (const FEnemySpawnEntry& E : GetWaveEntries(WaveIndex))
	{
		Total += E.Count;
	}
	return Total;
}

int32 AWaveDirector::GetLiveCountOfClass(TSubclassOf<AEnemyBase> EnemyClass) const
{
	if (!EnemyClass) return 0;
	const int32* Found = LiveCountByClass.Find(EnemyClass);
	return Found ? *Found : 0;
}

void AWaveDirector::BeginPlay()
{
	Super::BeginPlay();

	RefreshSpawnPoints();

	if (bLogWaveEvents)
	{
		UE_LOG(LogTemp, Log, TEXT("[Waves] Director ready. SpawnPoints found: %d, Waves configured: %d"),
			SpawnPoints.Num(), Waves.Num());
	}

	if (SpawnPoints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Waves] No SpawnPoint actors found in level. Place a few before pressing Play."));
	}

	if (bAutoStart && Waves.Num() > 0)
	{
		BeginWave(0);
	}
}

void AWaveDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearWaveTimers();
	Super::EndPlay(Reason);
}

void AWaveDirector::BeginWave(int32 WaveIndex)
{
	if (!Waves.IsValidIndex(WaveIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Waves] BeginWave: invalid index %d (Waves.Num=%d)"),
			WaveIndex, Waves.Num());
		return;
	}

	UWaveDefinition* Wave = Waves[WaveIndex];
	if (!Wave)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Waves] BeginWave: Waves[%d] is null"), WaveIndex);
		return;
	}

	// Clean up any pending state from a previous wave.
	ClearWaveTimers();

	CurrentWaveIndex = WaveIndex;
	LiveEnemyCount = 0;
	LiveCountByClass.Reset();
	EntriesFullySpawned = 0;
	bWaveActive = true;

	// Start the survival timer at the first wave of the run.
	if (WaveIndex == 0)
	{
		SurvivalTime = 0.f;
		bSurvivalTimerActive = true;
	}

	const int32 NumEntries = Wave->Entries.Num();
	EntrySpawnedCount.Init(0, NumEntries);
	EntryTimers.SetNum(NumEntries);

	if (bLogWaveEvents)
	{
		UE_LOG(LogTemp, Log, TEXT("[Waves] Wave %d started — %s (%d entries) [Current=%d]"),
			WaveIndex,
			Wave->DisplayName.IsEmpty() ? *Wave->GetName() : *Wave->DisplayName,
			NumEntries, CurrentWaveIndex);

		// Composition breakdown
		int32 GrandTotal = 0;
		for (int32 i = 0; i < NumEntries; ++i)
		{
			const FEnemySpawnEntry& E = Wave->Entries[i];
			const FString ClassName = E.EnemyClass ? E.EnemyClass->GetName() : FString(TEXT("(null)"));
			UE_LOG(LogTemp, Log, TEXT("[Waves]   - %s × %d (interval=%.2fs, delay=%.2fs)"),
				*ClassName, E.Count, E.SpawnInterval, E.StartDelay);
			GrandTotal += E.Count;
		}
		UE_LOG(LogTemp, Log, TEXT("[Waves]   Total enemies for this wave: %d"), GrandTotal);
	}

	UE_LOG(LogTemp, Log, TEXT("[Waves] Broadcasting OnWaveStarted with WaveIndex=%d"), WaveIndex);
	OnWaveStarted.Broadcast(this, WaveIndex);

	UWorld* World = GetWorld();
	if (!World)
	{
		bWaveActive = false;
		return;
	}

	// Schedule each entry's first spawn after its StartDelay.
	for (int32 i = 0; i < NumEntries; ++i)
	{
		const FEnemySpawnEntry& Entry = Wave->Entries[i];
		if (!Entry.EnemyClass || Entry.Count <= 0)
		{
			++EntriesFullySpawned;
			continue;
		}

		const float Delay = FMath::Max(0.0001f, Entry.StartDelay);
		World->GetTimerManager().SetTimer(EntryTimers[i],
			FTimerDelegate::CreateUObject(this, &AWaveDirector::SpawnOneFromEntry, i),
			Delay, /*bLoop=*/ false);
	}

	// All entries skipped (e.g. all classes null) — wave is trivially done.
	if (EntriesFullySpawned >= NumEntries)
	{
		TryEndWave();
	}
}

void AWaveDirector::BeginNextWave()
{
	BeginWave(CurrentWaveIndex + 1);
}

void AWaveDirector::SpawnOneFromEntry(int32 EntryIndex)
{
	if (!bWaveActive || !Waves.IsValidIndex(CurrentWaveIndex)) return;

	UWaveDefinition* Wave = Waves[CurrentWaveIndex];
	if (!Wave || !Wave->Entries.IsValidIndex(EntryIndex)) return;
	if (!EntrySpawnedCount.IsValidIndex(EntryIndex))      return;

	const FEnemySpawnEntry& Entry = Wave->Entries[EntryIndex];

	UWorld* World = GetWorld();
	if (!World) return;

	// Pick a spawn location: tag-filtered if the entry specifies one, otherwise random from
	// the farthest-half pool. Falls back to the director's own transform if no points exist.
	ASpawnPoint* SP = nullptr;
	if (!Entry.SpawnPointTag.IsNone())
	{
		SP = PickSpawnPointByTag(Entry.SpawnPointTag);
		if (!SP && bLogWaveEvents)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Waves] Entry %d requested SpawnPointTag '%s' — no SpawnPoint with that tag, falling back to random."),
				EntryIndex, *Entry.SpawnPointTag.ToString());
		}
	}
	if (!SP) SP = PickSpawnPoint();

	const FVector  Loc = SP ? SP->GetActorLocation() : GetActorLocation();
	const FRotator Rot = SP ? SP->GetActorRotation() : GetActorRotation();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(Entry.EnemyClass, Loc, Rot, Params);
	if (Enemy)
	{
		if (UHealthComponent* HC = Enemy->FindComponentByClass<UHealthComponent>())
		{
			HC->OnDeath.AddDynamic(this, &AWaveDirector::HandleEnemyDeath);
			++LiveEnemyCount;
			++LiveCountByClass.FindOrAdd(Enemy->GetClass());
			OnLiveEnemyCountChanged.Broadcast(this, LiveEnemyCount);
		}

		if (bLogWaveEvents)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[Waves] Spawned %s at %s (entry %d: %d/%d)"),
				*Entry.EnemyClass->GetName(), *Loc.ToCompactString(),
				EntryIndex, EntrySpawnedCount[EntryIndex] + 1, Entry.Count);
		}
	}
	else if (bLogWaveEvents)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Waves] Failed to spawn %s at %s"),
			*Entry.EnemyClass->GetName(), *Loc.ToCompactString());
	}

	++EntrySpawnedCount[EntryIndex];

	// More to spawn for this entry? Reschedule. Otherwise mark entry done.
	if (EntrySpawnedCount[EntryIndex] >= Entry.Count)
	{
		++EntriesFullySpawned;
		TryEndWave();
	}
	else
	{
		const float Interval = FMath::Max(0.0001f, Entry.SpawnInterval);
		World->GetTimerManager().SetTimer(EntryTimers[EntryIndex],
			FTimerDelegate::CreateUObject(this, &AWaveDirector::SpawnOneFromEntry, EntryIndex),
			Interval, /*bLoop=*/ false);
	}
}

void AWaveDirector::HandleEnemyDeath(UHealthComponent* HealthComp, AActor* Causer)
{
	AEnemyBase* DeadEnemy = HealthComp ? Cast<AEnemyBase>(HealthComp->GetOwner()) : nullptr;
	const FVector KillLoc = DeadEnemy ? DeadEnemy->GetActorLocation() : FVector::ZeroVector;

	LiveEnemyCount = FMath::Max(0, LiveEnemyCount - 1);
	++TotalEnemiesKilled;

	if (DeadEnemy)
	{
		if (int32* PerClass = LiveCountByClass.Find(DeadEnemy->GetClass()))
		{
			*PerClass = FMath::Max(0, *PerClass - 1);
			if (*PerClass == 0)
			{
				LiveCountByClass.Remove(DeadEnemy->GetClass());
			}
		}
	}

	if (bLogWaveEvents)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Waves] Enemy died, %d alive / %d killed total (wave %d)"),
			LiveEnemyCount, TotalEnemiesKilled, CurrentWaveIndex);
	}

	OnEnemyKilled.Broadcast(this, TotalEnemiesKilled);
	OnLiveEnemyCountChanged.Broadcast(this, LiveEnemyCount);
	OnEnemyDied.Broadcast(this, DeadEnemy, Causer, KillLoc);

	TryEndWave();
}

void AWaveDirector::TryEndWave()
{
	if (!bWaveActive) return;
	if (!Waves.IsValidIndex(CurrentWaveIndex)) return;

	UWaveDefinition* Wave = Waves[CurrentWaveIndex];
	if (!Wave) return;

	// All entries finished spawning AND no enemies left alive.
	if (EntriesFullySpawned >= Wave->Entries.Num() && LiveEnemyCount <= 0)
	{
		bWaveActive = false;
		ClearWaveTimers();

		if (bLogWaveEvents)
		{
			UE_LOG(LogTemp, Log, TEXT("[Waves] Wave %d cleared"), CurrentWaveIndex);
		}

		OnWaveEnded.Broadcast(this, CurrentWaveIndex);

		if (CurrentWaveIndex + 1 >= Waves.Num())
		{
			if (bLogWaveEvents)
			{
				UE_LOG(LogTemp, Log, TEXT("[Waves] All waves completed."));
			}
			bSurvivalTimerActive = false; // freeze the timer at run completion
			OnAllWavesCompleted.Broadcast(this);
		}
	}
}

void AWaveDirector::ClearWaveTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		for (FTimerHandle& Handle : EntryTimers)
		{
			TM.ClearTimer(Handle);
		}
	}
	EntryTimers.Reset();
}

void AWaveDirector::RefreshSpawnPoints()
{
	SpawnPoints.Reset();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ASpawnPoint::StaticClass(), Found);
	SpawnPoints.Reserve(Found.Num());
	for (AActor* A : Found)
	{
		if (ASpawnPoint* SP = Cast<ASpawnPoint>(A))
		{
			SpawnPoints.Add(SP);
		}
	}

	if (bLogWaveEvents)
	{
		UE_LOG(LogTemp, Log, TEXT("[Waves] RefreshSpawnPoints: %d SpawnPoint(s) discovered"), SpawnPoints.Num());
		for (int32 i = 0; i < SpawnPoints.Num(); ++i)
		{
			ASpawnPoint* SP = SpawnPoints[i];
			if (!SP) continue;

			FString TagsStr;
			for (const FName& T : SP->Tags)
			{
				if (!TagsStr.IsEmpty()) TagsStr += TEXT(", ");
				TagsStr += T.ToString();
			}
			if (TagsStr.IsEmpty()) TagsStr = TEXT("(none)");

			UE_LOG(LogTemp, Log, TEXT("[Waves]   [%d] %s  exclude=%d  tags=[%s]"),
				i, *SP->GetName(),
				SP->bExcludeFromRandom ? 1 : 0,
				*TagsStr);
		}
	}
}

ASpawnPoint* AWaveDirector::PickSpawnPointByTag(FName Tag) const
{
	if (Tag.IsNone()) return nullptr;

	TArray<ASpawnPoint*> Matches;
	for (ASpawnPoint* SP : SpawnPoints)
	{
		if (!IsValid(SP)) continue;
		if (SP->Tags.Contains(Tag))
		{
			Matches.Add(SP);
		}
	}

	if (Matches.Num() == 0)
	{
		// Diagnostic: list every tag we DID see, so the designer can spot typos / wrong section.
		FString AvailableTagsStr;
		for (ASpawnPoint* SP : SpawnPoints)
		{
			if (!IsValid(SP)) continue;
			for (const FName& T : SP->Tags)
			{
				if (!AvailableTagsStr.IsEmpty()) AvailableTagsStr += TEXT(", ");
				AvailableTagsStr += FString::Printf(TEXT("'%s' on %s"), *T.ToString(), *SP->GetName());
			}
		}
		if (AvailableTagsStr.IsEmpty()) AvailableTagsStr = TEXT("(no tags on any SpawnPoint)");

		UE_LOG(LogTemp, Warning, TEXT("[Waves] PickSpawnPointByTag('%s') — NO MATCH. Available tags: %s"),
			*Tag.ToString(), *AvailableTagsStr);
		return nullptr;
	}

	ASpawnPoint* Picked = (Matches.Num() == 1) ? Matches[0] : Matches[FMath::RandRange(0, Matches.Num() - 1)];
	UE_LOG(LogTemp, Log, TEXT("[Waves] PickSpawnPointByTag('%s') -> %s (out of %d match(es))"),
		*Tag.ToString(), *Picked->GetName(), Matches.Num());
	return Picked;
}

ASpawnPoint* AWaveDirector::PickSpawnPoint() const
{
	// Build the random-eligible pool first: anything with bExcludeFromRandom is locked off here
	// (it's still reachable via PickSpawnPointByTag for tagged entries like the boss spawn).
	TArray<ASpawnPoint*> Eligible;
	Eligible.Reserve(SpawnPoints.Num());
	for (ASpawnPoint* SP : SpawnPoints)
	{
		if (IsValid(SP) && !SP->bExcludeFromRandom)
		{
			Eligible.Add(SP);
		}
	}

	if (Eligible.Num() == 0) return nullptr;
	if (Eligible.Num() == 1) return Eligible[0];

	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const APawn* Player = PC ? PC->GetPawn() : nullptr;

	if (!Player)
	{
		// No player yet — pick at random.
		return Eligible[FMath::RandRange(0, Eligible.Num() - 1)];
	}

	// Sort eligible points by distance to player (descending).
	TArray<ASpawnPoint*>& Valid = Eligible;

	const FVector PlayerLoc = Player->GetActorLocation();
	Valid.Sort([&PlayerLoc](const ASpawnPoint& A, const ASpawnPoint& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), PlayerLoc) >
		       FVector::DistSquared(B.GetActorLocation(), PlayerLoc);
	});

	// Pick randomly from the farthest half — avoids spawning on top of the player
	// while still varying the angle of attack from wave to wave.
	const int32 PickFromTop = FMath::Max(1, Valid.Num() / 2);
	return Valid[FMath::RandRange(0, PickFromTop - 1)];
}
