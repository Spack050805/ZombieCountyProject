// Fill out your copyright notice in the Description page of Project Settings.

#include "WhiskyCountyPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "EnemyBase.h"
#include "HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "VehiclePawn.h"
#include "VehicleWeaponComponent.h"
#include "WaveDirector.h"

void AWhiskyCountyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Tutorial flow: if a TutorialWidgetClass is configured, show that INSTEAD of the HUD
	// and let the player kick off the run from the tutorial's button. Otherwise start the
	// HUD immediately (the WaveDirector still auto-starts per its bAutoStart flag).
	if (TutorialWidgetClass)
	{
		ShowTutorial();
	}
	else
	{
		ShowHUD();
	}

	// Bind reward + shockwave handlers to the WaveDirector. Director might spawn after us;
	// we retry once via timer if it's not available yet.
	if (AWaveDirector* WD = GetWaveDirector())
	{
		WD->OnWaveEnded.AddDynamic(this,   &AWhiskyCountyPlayerController::HandleWaveEnded);
		WD->OnWaveStarted.AddDynamic(this, &AWhiskyCountyPlayerController::HandleWaveStarted);
		WD->OnEnemyDied.AddDynamic(this,   &AWhiskyCountyPlayerController::HandleEnemyDied);
	}
	else
	{
		FTimerHandle Retry;
		GetWorldTimerManager().SetTimer(Retry, [this]()
		{
			if (AWaveDirector* WDLater = GetWaveDirector())
			{
				WDLater->OnWaveEnded.AddDynamic(this,   &AWhiskyCountyPlayerController::HandleWaveEnded);
				WDLater->OnWaveStarted.AddDynamic(this, &AWhiskyCountyPlayerController::HandleWaveStarted);
				WDLater->OnEnemyDied.AddDynamic(this,   &AWhiskyCountyPlayerController::HandleEnemyDied);
			}
		}, 0.2f, false);
	}

	// Debug auto-equip — pawn is usually possessed by now but we delay slightly to be safe.
	if (bDebugAutoEquipSpecial)
	{
		FTimerHandle EquipTimer;
		GetWorldTimerManager().SetTimer(EquipTimer, [this]()
		{
			SelectSpecialWeapon(DebugStartingSpecial);
			UE_LOG(LogTemp, Log, TEXT("[Run] Debug auto-equipped special: %d"), (int32)DebugStartingSpecial);
		}, 0.2f, false);
	}
}

void AWhiskyCountyPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	HideHUD();
	Super::EndPlay(Reason);
}

void AWhiskyCountyPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Bind player death exactly when the pawn becomes available. AddUniqueDynamic guards
	// against double-binding if OnPossess fires more than once for the same pawn.
	if (UHealthComponent* HC = GetHealthComponent())
	{
		HC->OnDeath.AddUniqueDynamic(this, &AWhiskyCountyPlayerController::HandlePlayerDeath);
	}
}

void AWhiskyCountyPlayerController::HandlePlayerDeath(UHealthComponent* /*HealthComp*/, AActor* /*Causer*/)
{
	UE_LOG(LogTemp, Log, TEXT("[Run] Player died — showing death UI"));

	// Freeze the survival timer so the death widget reads a stable value.
	if (AWaveDirector* WD = GetWaveDirector())
	{
		WD->StopSurvivalTimer();
	}

	HideHUD();
	OnPlayerDeathUI();
}

void AWhiskyCountyPlayerController::ShowHUD()
{
	if (HUDWidget) return; // already on screen

	if (!HUDWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlayerController] HUDWidgetClass not set on BP — assign W_HUD."));
		return;
	}

	HUDWidget = CreateWidget<UUserWidget>(this, HUDWidgetClass);
	if (HUDWidget)
	{
		HUDWidget->AddToViewport();
	}
}

void AWhiskyCountyPlayerController::HideHUD()
{
	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
}

// ===== Tutorial =====

void AWhiskyCountyPlayerController::ShowTutorial()
{
	if (TutorialWidget) return; // already on screen

	if (!TutorialWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] ShowTutorial called but TutorialWidgetClass is None."));
		return;
	}

	// HUD is normally up by the time the tutorial fires; tear it down so it doesn't peek
	// behind the tutorial.
	HideHUD();

	TutorialWidget = CreateWidget<UUserWidget>(this, TutorialWidgetClass);
	if (!TutorialWidget) return;

	TutorialWidget->AddToViewport(/*ZOrder=*/ 100); // above the HUD layer just in case

	// Lock input to UI so the player can't drive while reading. The cursor is already
	// visible (the vehicle pawn enables it for aiming).
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(TutorialWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[Tutorial] Shown — wave will start on DismissTutorialAndStart()"));
}

void AWhiskyCountyPlayerController::DismissTutorialAndStart()
{
	if (TutorialWidget)
	{
		TutorialWidget->RemoveFromParent();
		TutorialWidget = nullptr;
	}

	// Restore the standard play input mode (game receives input, UI still gets the cursor
	// for aiming + reward menus).
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;

	ShowHUD();

	// Start Wave 0. If the WaveDirector already auto-started (e.g. designer forgot to
	// uncheck bAutoStart), CurrentWaveIndex won't be INDEX_NONE and we leave it alone.
	if (AWaveDirector* WD = GetWaveDirector())
	{
		if (WD->GetCurrentWaveIndex() == INDEX_NONE)
		{
			UE_LOG(LogTemp, Log, TEXT("[Tutorial] Dismissed — starting Wave 0"));
			WD->BeginWave(0);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Tutorial] Dismissed but a wave is already in progress (idx=%d). "
				"Did you forget to uncheck bAutoStart on the WaveDirector?"), WD->GetCurrentWaveIndex());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] Dismissed but no WaveDirector found in level."));
	}
}

// ===== Pawn / component lookup =====

AVehiclePawn* AWhiskyCountyPlayerController::GetVehiclePawn() const
{
	return Cast<AVehiclePawn>(GetPawn());
}

UVehicleWeaponComponent* AWhiskyCountyPlayerController::GetWeaponComponent() const
{
	if (AVehiclePawn* V = GetVehiclePawn())
	{
		return V->FindComponentByClass<UVehicleWeaponComponent>();
	}
	return nullptr;
}

UHealthComponent* AWhiskyCountyPlayerController::GetHealthComponent() const
{
	if (AVehiclePawn* V = GetVehiclePawn())
	{
		return V->GetHealthComponent();
	}
	return nullptr;
}

AWaveDirector* AWhiskyCountyPlayerController::GetWaveDirector() const
{
	if (UWorld* World = GetWorld())
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, AWaveDirector::StaticClass(), Found);
		if (Found.Num() > 0)
		{
			return Cast<AWaveDirector>(Found[0]);
		}
	}
	return nullptr;
}

// ===== HUD data forwarders =====

float AWhiskyCountyPlayerController::GetHealthNormalized() const
{
	const UHealthComponent* HC = GetHealthComponent();
	return HC ? HC->GetHealthNormalized() : 0.f;
}

float AWhiskyCountyPlayerController::GetHeatNormalized() const
{
	const UVehicleWeaponComponent* W = GetWeaponComponent();
	return W ? W->GetHeatNormalized() : 0.f;
}

float AWhiskyCountyPlayerController::GetRamChargeNormalized() const
{
	const AVehiclePawn* V = GetVehiclePawn();
	return V ? V->GetRamChargeNormalized() : 0.f;
}

int32 AWhiskyCountyPlayerController::GetCurrentSpecialAmmo() const
{
	const UVehicleWeaponComponent* W = GetWeaponComponent();
	return W ? W->GetCurrentSpecialAmmo() : 0;
}

int32 AWhiskyCountyPlayerController::GetMaxSpecialAmmo() const
{
	const UVehicleWeaponComponent* W = GetWeaponComponent();
	return W ? W->GetMaxSpecialAmmo() : 0;
}

ESpecialWeaponType AWhiskyCountyPlayerController::GetSelectedSpecialWeapon() const
{
	const UVehicleWeaponComponent* W = GetWeaponComponent();
	return W ? W->GetSelectedSpecialWeaponType() : ESpecialWeaponType::Shotgun;
}

int32 AWhiskyCountyPlayerController::GetCurrentWaveIndex() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetCurrentWaveIndex() : INDEX_NONE;
}

int32 AWhiskyCountyPlayerController::GetTotalWaveCount() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetTotalWaveCount() : 0;
}

int32 AWhiskyCountyPlayerController::GetEnemiesRemaining() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetLiveEnemyCount() : 0;
}

int32 AWhiskyCountyPlayerController::GetTotalEnemiesKilled() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetTotalEnemiesKilled() : 0;
}

TArray<FEnemySpawnEntry> AWhiskyCountyPlayerController::GetCurrentWaveEntries() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetCurrentWaveEntries() : TArray<FEnemySpawnEntry>{};
}

TArray<FEnemySpawnEntry> AWhiskyCountyPlayerController::GetWaveEntries(int32 WaveIndex) const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetWaveEntries(WaveIndex) : TArray<FEnemySpawnEntry>{};
}

int32 AWhiskyCountyPlayerController::GetCurrentWaveTotalEnemies() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetCurrentWaveTotalEnemies() : 0;
}

float AWhiskyCountyPlayerController::GetSurvivalTime() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetSurvivalTime() : 0.f;
}

FString AWhiskyCountyPlayerController::GetSurvivalTimeFormatted() const
{
	const AWaveDirector* WD = GetWaveDirector();
	return WD ? WD->GetSurvivalTimeFormatted() : FString(TEXT("00:00"));
}

// ===== Reward flow =====

void AWhiskyCountyPlayerController::HandleWaveStarted(AWaveDirector* /*Director*/, int32 WaveIndex)
{
	// Refill the special weapon ammo to max at the start of every wave (only if equipped).
	if (UVehicleWeaponComponent* W = GetWeaponComponent())
	{
		if (W->IsSpecialEquipped())
		{
			W->AddSpecialAmmo(W->GetMaxSpecialAmmo()); // clamps to max internally
			UE_LOG(LogTemp, Log, TEXT("[Run] Wave %d started — refilled special ammo to %d/%d"),
				WaveIndex, W->GetCurrentSpecialAmmo(), W->GetMaxSpecialAmmo());

			// FX hook: BP plays the "ammo restocked" HUD pulse / mechanical-clack sound.
			OnWaveAmmoRefilledFX(W->GetCurrentSpecialAmmo(), W->GetMaxSpecialAmmo());
		}
	}
}

void AWhiskyCountyPlayerController::HandleWaveEnded(AWaveDirector* /*Director*/, int32 WaveIndex)
{
	// Wave 0 (first wave) ended → weapon choice. Subsequent non-final waves → powerup choice.
	const AWaveDirector* WD = GetWaveDirector();
	const int32 LastWaveIndex = WD ? WD->GetTotalWaveCount() - 1 : INDEX_NONE;

	UE_LOG(LogTemp, Log, TEXT("[Run] HandleWaveEnded: WaveIndex=%d, LastWaveIndex=%d, TotalWaves=%d"),
		WaveIndex, LastWaveIndex, WD ? WD->GetTotalWaveCount() : -1);

	if (WaveIndex == LastWaveIndex)
	{
		// Final wave just cleared (= miniboss in the design). No reward — show win UI.
		UE_LOG(LogTemp, Log, TEXT("[Run] Final wave cleared — firing OnAllWavesCompletedUI"));
		OnAllWavesCompletedUI();
		return;
	}

	const bool bIsWeaponWave = (WaveIndex == 0) && !bDebugForcePowerupOnFirstWave;

	if (bIsWeaponWave)
	{
		LastWeaponChoices = GetAvailableSpecialWeapons();
		LastPowerupChoices.Reset();
		LastRewardKind = ERewardKind::Weapon;
		// Fire the BP event FIRST so BP can spawn the reward widget. The widget binds to
		// OnRewardChoicesUpdated inside its Construct, then we broadcast right after so the
		// freshly-bound widget actually receives the signal.
		OnShowWeaponChoice(LastWeaponChoices);
		OnRewardChoicesUpdated.Broadcast(LastRewardKind);
	}
	else
	{
		LastPowerupChoices = RollPowerupChoices(2);
		LastWeaponChoices.Reset();
		LastRewardKind = ERewardKind::Powerup;
		OnShowPowerupChoice(LastPowerupChoices);
		OnRewardChoicesUpdated.Broadcast(LastRewardKind);
	}

	UE_LOG(LogTemp, Log, TEXT("[Run] Reward broadcast complete (Kind=%d, Powerup=%d, Weapon=%d)"),
		(int32)LastRewardKind, LastPowerupChoices.Num(), LastWeaponChoices.Num());
}

FWeaponInfo AWhiskyCountyPlayerController::GetWeaponInfo(ESpecialWeaponType Type) const
{
	FWeaponInfo Info;
	Info.Type = Type;
	switch (Type)
	{
	case ESpecialWeaponType::Shotgun:
		Info.DisplayName = FText::FromString(TEXT("Shotgun"));
		Info.Description = FText::FromString(TEXT("Devastante a corto raggio. Munizioni limitate."));
		break;
	case ESpecialWeaponType::GrenadeLauncher:
		Info.DisplayName = FText::FromString(TEXT("Lanciagranate"));
		Info.Description = FText::FromString(TEXT("Area damage a parabola. Cooldown e munizioni."));
		break;
	}
	return Info;
}

TArray<FWeaponInfo> AWhiskyCountyPlayerController::GetAvailableSpecialWeapons() const
{
	TArray<FWeaponInfo> Out;
	Out.Add(GetWeaponInfo(ESpecialWeaponType::Shotgun));
	Out.Add(GetWeaponInfo(ESpecialWeaponType::GrenadeLauncher));
	return Out;
}

FPowerupInfo AWhiskyCountyPlayerController::GetPowerupInfo(EPowerupType Type) const
{
	FPowerupInfo Info;
	Info.Type = Type;
	switch (Type)
	{
	case EPowerupType::Radiator:
		Info.DisplayName = FText::FromString(TEXT("Radiatore Potenziato"));
		Info.Description = FText::FromString(TEXT("-40% tempo raffreddamento mitra"));
		break;
	case EPowerupType::AmmoBoost:
		Info.DisplayName = FText::FromString(TEXT("Munizioni +"));
		Info.Description = FText::FromString(TEXT("+2 colpi extra speciale"));
		break;
	case EPowerupType::Chassis:
		Info.DisplayName = FText::FromString(TEXT("Telaio Rinforzato"));
		Info.Description = FText::FromString(TEXT("-35% danno auto-inflitto da ram"));
		break;
	case EPowerupType::Turbo:
		Info.DisplayName = FText::FromString(TEXT("Turbocompressore"));
		Info.Description = FText::FromString(TEXT("+35% velocità di ricarica ram"));
		break;
	case EPowerupType::Workshop:
		Info.DisplayName = FText::FromString(TEXT("Officina Mobile"));
		Info.Description = FText::FromString(TEXT("Rigenerazione lenta della vita"));
		break;
	case EPowerupType::Shockwave:
		Info.DisplayName = FText::FromString(TEXT("Onda d'Urto"));
		Info.Description = FText::FromString(TEXT("Knockback radiale ad ogni kill"));
		break;
	}
	return Info;
}

TArray<FPowerupInfo> AWhiskyCountyPlayerController::RollPowerupChoices(int32 Count)
{
	// Build the pool of un-acquired powerups.
	TArray<EPowerupType> Pool;
	for (uint8 i = 0; i < (uint8)EPowerupType::Shockwave + 1; ++i)
	{
		const EPowerupType T = static_cast<EPowerupType>(i);
		if (!AcquiredPowerups.Contains(T))
		{
			Pool.Add(T);
		}
	}

	// Pick Count distinct random elements.
	TArray<FPowerupInfo> Result;
	Result.Reserve(Count);
	for (int32 n = 0; n < Count && Pool.Num() > 0; ++n)
	{
		const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
		Result.Add(GetPowerupInfo(Pool[Idx]));
		Pool.RemoveAt(Idx);
	}

	// Diagnostic: confirm we're rolling distinct random types.
	for (int32 i = 0; i < Result.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("[Run] RollPowerupChoices[%d] = %s (%d)"),
			i, *Result[i].DisplayName.ToString(), (int32)Result[i].Type);
	}

	return Result;
}

void AWhiskyCountyPlayerController::ApplyPowerup(EPowerupType Type)
{
	if (HasPowerup(Type))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Run] Powerup %d already acquired — ignoring duplicate."), (int32)Type);
		return;
	}

	AVehiclePawn* V = GetVehiclePawn();
	UVehicleWeaponComponent* W = GetWeaponComponent();
	UHealthComponent* HC = GetHealthComponent();

	switch (Type)
	{
	case EPowerupType::Radiator:
		if (W)
		{
			const float Old = W->CoolingRate;
			W->CoolingRate *= Powerup_Radiator_CoolingMultiplier;
			UE_LOG(LogTemp, Log, TEXT("[Powerup] Radiator: CoolingRate %.1f -> %.1f (×%.3f)"),
				Old, W->CoolingRate, Powerup_Radiator_CoolingMultiplier);
		}
		break;
	case EPowerupType::AmmoBoost:
		if (W)
		{
			const int32 OldMax = W->MaxSpecialAmmo;
			W->MaxSpecialAmmo += Powerup_AmmoBoost_MaxAmmoAmount;
			W->AddSpecialAmmo(Powerup_AmmoBoost_AmmoAmount);
			UE_LOG(LogTemp, Log, TEXT("[Powerup] AmmoBoost: MaxAmmo %d -> %d, Current %d (+%d max, +%d ammo)"),
				OldMax, W->MaxSpecialAmmo, W->GetCurrentSpecialAmmo(),
				Powerup_AmmoBoost_MaxAmmoAmount, Powerup_AmmoBoost_AmmoAmount);
		}
		break;
	case EPowerupType::Chassis:
		if (V)
		{
			V->MultiplyRamSelfDamage(Powerup_Chassis_SelfDamageMultiplier);
			UE_LOG(LogTemp, Log, TEXT("[Powerup] Chassis: SelfDamageMultiplier ×%.3f"),
				Powerup_Chassis_SelfDamageMultiplier);
		}
		break;
	case EPowerupType::Turbo:
		if (V)
		{
			V->MultiplyRamRechargeRate(Powerup_Turbo_RechargeMultiplier);
			UE_LOG(LogTemp, Log, TEXT("[Powerup] Turbo: RamRechargeRate ×%.3f"),
				Powerup_Turbo_RechargeMultiplier);
		}
		break;
	case EPowerupType::Workshop:
		if (HC)
		{
			const float Old = HC->RegenRate;
			HC->RegenRate += Powerup_Workshop_RegenRate;
			UE_LOG(LogTemp, Log, TEXT("[Powerup] Workshop: RegenRate %.1f -> %.1f HP/s (+%.1f)"),
				Old, HC->RegenRate, Powerup_Workshop_RegenRate);
		}
		break;
	case EPowerupType::Shockwave:
		UE_LOG(LogTemp, Log, TEXT("[Powerup] Shockwave: enabled (radius %.0f, push %.0f)"),
			Powerup_Shockwave_Radius, Powerup_Shockwave_PushDistance);
		break;
	}

	AcquiredPowerups.Add(Type);

	const FPowerupInfo Info = GetPowerupInfo(Type);
	UE_LOG(LogTemp, Log, TEXT("[Run] Powerup acquired: %s (total %d)"),
		*Info.DisplayName.ToString(), AcquiredPowerups.Num());

	// HUD/data delegate (bound by widgets that listen to acquisitions).
	OnPowerupAcquired.Broadcast(Type);

	// FX hook: BP spawns the "powered up" flash / pickup sound. Generic across all 6 types
	// — branch on Type in BP for per-powerup color / SFX variations.
	OnPowerupAppliedFX(Type, Info.DisplayName);
}

void AWhiskyCountyPlayerController::SelectSpecialWeapon(ESpecialWeaponType Type)
{
	if (UVehicleWeaponComponent* W = GetWeaponComponent())
	{
		W->SetSelectedSpecialWeaponType(Type);
		UE_LOG(LogTemp, Log, TEXT("[Run] Special weapon set to %d"), (int32)Type);
		OnSpecialWeaponSelected.Broadcast(Type);
	}
}

void AWhiskyCountyPlayerController::ConfirmAndStartNextWave()
{
	if (AWaveDirector* WD = GetWaveDirector())
	{
		const int32 NextIdx = WD->GetCurrentWaveIndex() + 1;
		const int32 Total   = WD->GetTotalWaveCount();
		UE_LOG(LogTemp, Log, TEXT("[Run] ConfirmAndStartNextWave: requesting wave %d / %d"),
			NextIdx, Total);
		WD->BeginNextWave();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Run] ConfirmAndStartNextWave: no WaveDirector found!"));
	}
}

// ===== Shockwave (Onda d'urto) =====

void AWhiskyCountyPlayerController::HandleEnemyDied(AWaveDirector* /*Director*/, AEnemyBase* /*Enemy*/,
	AActor* Causer, FVector KillLocation)
{
	const bool bHasShockwave = HasPowerup(EPowerupType::Shockwave);
	UE_LOG(LogTemp, Verbose, TEXT("[Shockwave] HandleEnemyDied: Causer=%s, HasShockwave=%d"),
		*GetNameSafe(Causer), bHasShockwave ? 1 : 0);

	if (!bHasShockwave) return;

	const AVehiclePawn* V = GetVehiclePawn();
	if (!V) return;

	bool bByPlayer = false;
	if (Causer)
	{
		if (Causer == V) bByPlayer = true;
		else if (Causer->GetOwner() == V) bByPlayer = true;
		else if (APawn* CausePawn = Cast<APawn>(Causer)) bByPlayer = (CausePawn->GetInstigator() == V);
	}

	UE_LOG(LogTemp, Log, TEXT("[Shockwave] Causer=%s OwnerOfCauser=%s bByPlayer=%d"),
		*GetNameSafe(Causer),
		Causer ? *GetNameSafe(Causer->GetOwner()) : TEXT("null"),
		bByPlayer ? 1 : 0);

	if (!bByPlayer) return;

	// Onda d'urto = "panic button" del player: l'onda nasce SEMPRE dal centro del player
	// (non dal nemico morto), creando spazio attorno alla macchina. Il kill del nemico è
	// solo il trigger; KillLocation lo logghiamo per debug ma non lo usiamo come origine.
	const FVector PlayerOrigin = V->GetActorLocation();
	UE_LOG(LogTemp, Log, TEXT("[Shockwave] Trigger from kill @ %s, but origin = player @ %s"),
		*KillLocation.ToCompactString(), *PlayerOrigin.ToCompactString());

	ApplyShockwaveAt(PlayerOrigin);
}

void AWhiskyCountyPlayerController::ApplyShockwaveAt(const FVector& Origin)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float Radius       = Powerup_Shockwave_Radius;
	const float PushDistance = Powerup_Shockwave_PushDistance;

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(World, AEnemyBase::StaticClass(), Found);

	int32 PushedCount = 0;
	int32 SkippedImmune = 0;
	int32 SkippedRange = 0;

	for (AActor* A : Found)
	{
		AEnemyBase* E = Cast<AEnemyBase>(A);
		if (!E) continue;
		if (E->IsImmuneToShockwave()) { ++SkippedImmune; continue; }

		FVector ToEnemy = E->GetActorLocation() - Origin;
		ToEnemy.Z = 0.f;
		const float DistSq = ToEnemy.SizeSquared();
		if (DistSq > Radius * Radius || DistSq < KINDA_SMALL_NUMBER) { ++SkippedRange; continue; }

		const FVector PushDir = ToEnemy.GetSafeNormal();
		E->AddActorWorldOffset(PushDir * PushDistance, /*bSweep=*/ false);
		++PushedCount;
	}

	UE_LOG(LogTemp, Log, TEXT("[Shockwave] At %s — pushed %d enemies (immune skipped %d, out of range %d)"),
		*Origin.ToCompactString(), PushedCount, SkippedImmune, SkippedRange);

	// FX hook: BP spawns the radial shockwave Cascade + camera shake + sound.
	OnShockwaveActivatedFX(Origin, Radius, PushedCount);
}
