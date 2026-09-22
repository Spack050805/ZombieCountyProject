// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpawnPoint.generated.h"

class UArrowComponent;

UCLASS()
class WHISKYCOUNTY_API ASpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ASpawnPoint();

	// When true, the WaveDirector's random PickSpawnPoint() ignores this point. It is still
	// reachable via the tag-filtered PickSpawnPointByTag() path. Use this for arenas reserved
	// to specific encounters (e.g. the boss spawn) so regular wave entries don't land here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnPoint")
	bool bExcludeFromRandom = false;

private:
	UPROPERTY(VisibleAnywhere, Category = "SpawnPoint")
	TObjectPtr<UArrowComponent> Arrow;
};
