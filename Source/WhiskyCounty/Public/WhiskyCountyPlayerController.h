// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VehicleWeaponComponent.h" // for ESpecialWeaponType
#include "WaveDefinition.h"          // for FEnemySpawnEntry
#include "WhiskyCountyPlayerController.generated.h"

class UUserWidget;
class AVehiclePawn;
class UHealthComponent;
class UVehicleWeaponComponent;
class AWaveDirector;
class AEnemyBase;

// ===== Powerup data =====

UENUM(BlueprintType)
enum class EPowerupType : uint8
{
	Radiator   UMETA(DisplayName = "Radiatore Potenziato"), // -40% machinegun cooldown
	AmmoBoost  UMETA(DisplayName = "Munizioni +"),          // +2 max special ammo
	Chassis    UMETA(DisplayName = "Telaio Rinforzato"),    // -35% ram self-damage
	Turbo      UMETA(DisplayName = "Turbocompressore"),     // +35% ram recharge
	Workshop   UMETA(DisplayName = "Officina Mobile"),      // slow HP regen
	Shockwave  UMETA(DisplayName = "Onda d'Urto"),          // knockback radial on kill
};

UENUM(BlueprintType)
enum class ERewardKind : uint8
{
	Powerup  UMETA(DisplayName = "Powerup"),
	Weapon   UMETA(DisplayName = "Weapon"),
};

USTRUCT(BlueprintType)
struct FPowerupInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Powerup")
	EPowerupType Type = EPowerupType::Radiator;

	UPROPERTY(BlueprintReadOnly, Category = "Powerup")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Powerup")
	FText Description;
};

USTRUCT(BlueprintType)
struct FWeaponInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	ESpecialWeaponType Type = ESpecialWeaponType::Shotgun;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	FText Description;
};

// Fired when LastPowerupChoices/LastWeaponChoices are refreshed (= a new reward is ready).
// Reward widgets that exist before the wave ends should bind to this and re-read.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRewardChoicesUpdated,
	ERewardKind, Kind);

// Fired right after the player confirms a special weapon choice (wave-1 reward).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpecialWeaponSelected,
	ESpecialWeaponType, Type);

// Fired right after a powerup is applied (player confirmed a powerup card).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPowerupAcquired,
	EPowerupType, Type);

UCLASS()
class WHISKYCOUNTY_API AWhiskyCountyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnPossess(APawn* InPawn) override;

	// ===== Reward / progression =====
	// Picks N (default 2) random non-repeating powerup options the player hasn't acquired yet,
	// avoiding two from the same system on the same screen (per design).
	UFUNCTION(BlueprintCallable, Category = "Run|Powerups")
	TArray<FPowerupInfo> RollPowerupChoices(int32 Count = 2);

	// Returns the static info (name/description) for a powerup type.
	UFUNCTION(BlueprintPure, Category = "Run|Powerups")
	FPowerupInfo GetPowerupInfo(EPowerupType Type) const;

	// Returns the static info for a special weapon (used by the weapon-choice card).
	UFUNCTION(BlueprintPure, Category = "Run|Weapon")
	FWeaponInfo GetWeaponInfo(ESpecialWeaponType Type) const;

	// Both available special weapons (Shotgun + GrenadeLauncher), for the wave-1 menu.
	UFUNCTION(BlueprintPure, Category = "Run|Weapon")
	TArray<FWeaponInfo> GetAvailableSpecialWeapons() const;

	// Applies the selected powerup's gameplay effect immediately (mutates pawn / weapon /
	// health properties). Records the powerup as acquired so it won't roll again.
	UFUNCTION(BlueprintCallable, Category = "Run|Powerups")
	void ApplyPowerup(EPowerupType Type);

	UFUNCTION(BlueprintPure, Category = "Run|Powerups")
	bool HasPowerup(EPowerupType Type) const { return AcquiredPowerups.Contains(Type); }

	UFUNCTION(BlueprintPure, Category = "Run|Powerups")
	const TArray<EPowerupType>& GetAcquiredPowerups() const { return AcquiredPowerups; }

	// Latest rolled choices (set in HandleWaveEnded right before firing the UI event).
	// Read these from the reward menu widget to populate the cards.
	UFUNCTION(BlueprintPure, Category = "Run|UI")
	const TArray<FPowerupInfo>& GetLastPowerupChoices() const { return LastPowerupChoices; }

	UFUNCTION(BlueprintPure, Category = "Run|UI")
	const TArray<FWeaponInfo>& GetLastWeaponChoices() const { return LastWeaponChoices; }

	UFUNCTION(BlueprintPure, Category = "Run|UI")
	ERewardKind GetLastRewardKind() const { return LastRewardKind; }

	// Switches the equipped special weapon (called from the wave-1 reward UMG).
	UFUNCTION(BlueprintCallable, Category = "Run|Weapon")
	void SelectSpecialWeapon(ESpecialWeaponType Type);

	// Closes the reward UMG and starts the next wave. Call after the player confirms.
	UFUNCTION(BlueprintCallable, Category = "Run")
	void ConfirmAndStartNextWave();

	// ===== UMG hooks (override in BP_PlayerController) =====
	// Fired after wave 1 ends — show the weapon-choice widget. Choices contains both
	// available specials so the menu can render them with the same card widget as powerups.
	UFUNCTION(BlueprintImplementableEvent, Category = "Run|UI")
	void OnShowWeaponChoice(const TArray<FWeaponInfo>& Choices);

	// Fired after waves 2+ end — show the powerup-choice widget. Choices are pre-rolled and
	// passed in so the UMG just renders them.
	UFUNCTION(BlueprintImplementableEvent, Category = "Run|UI")
	void OnShowPowerupChoice(const TArray<FPowerupInfo>& Choices);

	UFUNCTION(BlueprintImplementableEvent, Category = "Run|UI")
	void OnAllWavesCompletedUI();

	// Fired when the player vehicle's health hits zero — show the death/game-over widget.
	// Stats are already frozen at this point: call GetSurvivalTimeFormatted / GetTotalEnemiesKilled /
	// GetCurrentWaveIndex+1 from the widget to populate "tempo, kill, wave raggiunta".
	UFUNCTION(BlueprintImplementableEvent, Category = "Run|UI")
	void OnPlayerDeathUI();

	// ===== Powerup FX hooks (override in BP to spawn Cascade emitters + sounds) =====
	// Fired EVERY time a powerup is applied (after the gameplay effect lands and the
	// powerup is recorded in AcquiredPowerups). Use for a generic "you just got a buff"
	// flash on the vehicle / screen tint / pickup sound. Branch on Type in BP if you want
	// per-powerup variations (different aura color per type, etc.).
	UFUNCTION(BlueprintImplementableEvent, Category = "Run|FX")
	void OnPowerupAppliedFX(EPowerupType Type, const FText& DisplayName);

	// Fired when the Onda d'urto powerup pushes enemies on a player kill. Spawn the radial
	// shockwave Cascade emitter at Origin (scale by Radius) + camera shake + impact sound.
	// AffectedCount is the number of enemies actually pushed (already excludes immune /
	// out-of-range). Useful to scale screen shake intensity (more enemies = bigger shake).
	UFUNCTION(BlueprintImplementableEvent, Category = "Run|FX")
	void OnShockwaveActivatedFX(const FVector& Origin, float Radius, int32 AffectedCount);

	// Fired at the start of every wave AFTER the special-weapon ammo has been refilled
	// (only if a special is equipped — otherwise skipped). Use for an "ammo restocked"
	// HUD pulse animation / mechanical-clack sound. NewAmmo == MaxAmmo by definition since
	// it's a full refill.
	UFUNCTION(BlueprintImplementableEvent, Category = "Run|FX")
	void OnWaveAmmoRefilledFX(int32 NewAmmo, int32 MaxAmmo);

	// Bind from the reward widget on Construct to refresh when new choices are ready.
	UPROPERTY(BlueprintAssignable, Category = "Run|UI")
	FOnRewardChoicesUpdated OnRewardChoicesUpdated;

	// HUD events — bind from W_HUD to update the special-weapon icon and the powerup tray.
	UPROPERTY(BlueprintAssignable, Category = "Run|UI")
	FOnSpecialWeaponSelected OnSpecialWeaponSelected;

	UPROPERTY(BlueprintAssignable, Category = "Run|UI")
	FOnPowerupAcquired OnPowerupAcquired;

	// ===== HUD =====
	// Assign your W_HUD widget class on the BP_WhiskyCountyPlayerController.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UUserWidget> HUDWidgetClass;

	// ===== Tutorial =====
	// Optional tutorial UMG. If set, it's shown at BeginPlay INSTEAD of the HUD, the input
	// mode is locked to UI, and Wave 0 is NOT started — the run begins only when the
	// tutorial calls DismissTutorialAndStart from its "Start" button.
	// Leave null to skip the tutorial flow entirely (HUD shown immediately, wave auto-starts
	// per the WaveDirector's bAutoStart flag — same behavior as before).
	// REQUIRED SETUP when using this: set bAutoStart = false on the WaveDirector instance in
	// the level, otherwise the wave starts during the tutorial.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tutorial")
	TSubclassOf<UUserWidget> TutorialWidgetClass;

	// Spawns the tutorial widget, sets input to UIOnly + cursor visible, hides the HUD if
	// present. Safe to call again — does nothing if the tutorial is already up.
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void ShowTutorial();

	// Hide tutorial widget, restore game input, show HUD, and BeginWave(0) on the
	// WaveDirector. Wire this to the tutorial widget's "Start" / "Got it" button.
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void DismissTutorialAndStart();

	UFUNCTION(BlueprintPure, Category = "Tutorial")
	bool IsTutorialActive() const { return TutorialWidget != nullptr; }

	// DEBUG: when true, the first wave's reward shows the powerup menu instead of the weapon
	// menu (skipping the initial weapon choice). Lets you test powerup UI without playing
	// through to wave 2. Turn off for shipping.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Debug")
	bool bDebugForcePowerupOnFirstWave = true;

	// DEBUG: auto-equips the chosen special weapon on BeginPlay so you can test special-fire
	// without playing through wave 1. Default is None = no auto-equip (matches release).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Debug")
	bool bDebugAutoEquipSpecial = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Debug")
	ESpecialWeaponType DebugStartingSpecial = ESpecialWeaponType::Shotgun;

	// ===== Powerup tunables (modify in BP_WhiskyCountyPlayerController defaults) =====
	// Radiatore Potenziato — multiplier on machinegun cooling rate. 1.667 ≈ -40% cooling time.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning")
	float Powerup_Radiator_CoolingMultiplier = 1.667f;

	// Munizioni + — flat additions to special ammo and max ammo.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "0"))
	int32 Powerup_AmmoBoost_AmmoAmount = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "0"))
	int32 Powerup_AmmoBoost_MaxAmmoAmount = 2;

	// Telaio Rinforzato — multiplier on ram self-damage. 0.65 = -35% cost.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "0", ClampMax = "1"))
	float Powerup_Chassis_SelfDamageMultiplier = 0.65f;

	// Turbocompressore — multiplier on ram charge recharge rate. 1.35 = +35%.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "1"))
	float Powerup_Turbo_RechargeMultiplier = 1.35f;

	// Officina Mobile — HP regenerated per second.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "0"))
	float Powerup_Workshop_RegenRate = 2.f;

	// Onda d'Urto — radius (cm) of the radial knockback on player kills.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "0"))
	float Powerup_Shockwave_Radius = 600.f;

	// Onda d'Urto — push distance (cm) applied to enemies inside the radius.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Run|Powerups|Tuning", meta = (ClampMin = "0"))
	float Powerup_Shockwave_PushDistance = 600.f;

	// Spawns and adds HUDWidgetClass to the viewport. Called automatically on BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ShowHUD();

	// Removes the HUD from the viewport (e.g. for game-over / cinematics).
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void HideHUD();

	UFUNCTION(BlueprintPure, Category = "HUD")
	UUserWidget* GetHUDWidget() const { return HUDWidget; }

	// ===== Data the HUD pulls (bind from UMG via Property Bindings or call from BP) =====
	UFUNCTION(BlueprintPure, Category = "HUD|Player")
	AVehiclePawn* GetVehiclePawn() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Player")
	float GetHealthNormalized() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Player")
	float GetHeatNormalized() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Player")
	float GetRamChargeNormalized() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Weapon")
	int32 GetCurrentSpecialAmmo() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Weapon")
	int32 GetMaxSpecialAmmo() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Weapon")
	ESpecialWeaponType GetSelectedSpecialWeapon() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	int32 GetCurrentWaveIndex() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	int32 GetTotalWaveCount() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	int32 GetEnemiesRemaining() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	int32 GetTotalEnemiesKilled() const;

	// Per-class breakdown of the current wave (e.g. "Chaser × 8, Sniper × 2"). Drive HUD
	// previews / "next wave" peek widgets with this.
	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	TArray<FEnemySpawnEntry> GetCurrentWaveEntries() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	TArray<FEnemySpawnEntry> GetWaveEntries(int32 WaveIndex) const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	int32 GetCurrentWaveTotalEnemies() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	float GetSurvivalTime() const;

	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	FString GetSurvivalTimeFormatted() const;

	// BlueprintPure access to the active WaveDirector — bind your HUD events from this in BP.
	UFUNCTION(BlueprintPure, Category = "HUD|Wave")
	AWaveDirector* GetActiveWaveDirector() const { return GetWaveDirector(); }

protected:
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> TutorialWidget;

	UPROPERTY(Transient)
	TArray<EPowerupType> AcquiredPowerups;

	UPROPERTY(Transient)
	TArray<FPowerupInfo> LastPowerupChoices;

	UPROPERTY(Transient)
	TArray<FWeaponInfo> LastWeaponChoices;

	UPROPERTY(Transient)
	ERewardKind LastRewardKind = ERewardKind::Powerup;

	// Bound to WaveDirector->OnWaveEnded to drive the reward UMG flow.
	UFUNCTION()
	void HandleWaveEnded(AWaveDirector* Director, int32 WaveIndex);

	// Bound to WaveDirector->OnWaveStarted — refills the special weapon ammo at every wave start.
	UFUNCTION()
	void HandleWaveStarted(AWaveDirector* Director, int32 WaveIndex);

	// Bound to WaveDirector->OnEnemyDied for the Onda d'urto powerup.
	UFUNCTION()
	void HandleEnemyDied(AWaveDirector* Director, AEnemyBase* Enemy, AActor* Causer, FVector KillLocation);

	// Bound to the player Health->OnDeath in OnPossess. Freezes the run and fires OnPlayerDeathUI.
	UFUNCTION()
	void HandlePlayerDeath(UHealthComponent* HealthComp, AActor* Causer);

	// Applies the radial knockback at the kill location.
	void ApplyShockwaveAt(const FVector& Origin);

	// Helpers (not exposed to BP — internal lookups)
	UVehicleWeaponComponent* GetWeaponComponent() const;
	UHealthComponent*        GetHealthComponent() const;
	AWaveDirector*           GetWaveDirector()    const;
};
