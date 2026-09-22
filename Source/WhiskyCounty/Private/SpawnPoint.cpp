// Fill out your copyright notice in the Description page of Project Settings.

#include "SpawnPoint.h"

#include "Components/ArrowComponent.h"

ASpawnPoint::ASpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	SetRootComponent(Arrow);

#if WITH_EDITORONLY_DATA
	if (Arrow)
	{
		Arrow->ArrowColor = FColor::Red;
		Arrow->ArrowSize  = 1.5f;
		Arrow->ArrowLength = 80.f;
	}
#endif
}
