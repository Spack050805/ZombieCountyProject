// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VehiclePawn.generated.h"

class UBoxComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UVehicleWeaponComponent;
class UHealthComponent;

UCLASS()
class WHISKYCOUNTY_API AVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	AVehiclePawn();

	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Vehicle|Health")
	UHealthComponent* GetHealthComponent() const { return Health; }

	// ===== Animation drivers (read by AnimBP / Control Rig) =====
	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	float GetSpeed() const { return ForwardSpeed; }

	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	float GetSteerAngle() const { return CurrentSteerAngle; }

	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	float GetEngineRPM() const { return EngineRPM; }

	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	float GetWheelSpinAngle(int32 WheelIndex) const;

	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	float GetSuspensionOffset(int32 WheelIndex) const;

	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	bool IsWheelGrounded(int32 WheelIndex) const;

	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	bool IsHandbrakeActive() const { return bHandbrakeActive; }

	// ===== Ram state for HUD / VFX =====
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ram")
	bool IsRamming() const { return bRamming; }

	// 0..1 — current ram charge (full = 1, empty = 0)
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ram")
	float GetRamChargeNormalized() const { return FMath::Clamp(CurrentRamCharge / FMath::Max(1.f, RamMaxCharge), 0.f, 1.f); }

	// Seconds of ram left at the current charge if drained continuously (charge / DrainRate).
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ram")
	float GetRamTimeRemaining() const { return CurrentRamCharge / FMath::Max(0.001f, RamDrainRate); }

	// World direction the ram dash is locked to (zero if not ramming).
	UFUNCTION(BlueprintPure, Category = "Vehicle|Ram")
	FVector GetRamDirection() const { return RamDirection; }

	// ===== Aiming / Weapon mount (used by UVehicleWeaponComponent) =====
	// World point where the mouse is, projected onto a horizontal plane at chassis Z + AimPlaneOffset.
	UFUNCTION(BlueprintPure, Category = "Vehicle|Weapon")
	bool GetAimWorldPoint(FVector& OutPoint) const;

	// World position of the primary muzzle (machinegun + shotgun). Reads from skeletal mesh socket
	// PrimaryMuzzleSocketName; falls back to MuzzleOffsetFallback if the socket is missing.
	UFUNCTION(BlueprintPure, Category = "Vehicle|Weapon")
	FVector GetPrimaryMuzzleWorldLocation() const;

	// World position of the grenade launcher muzzle. Reads from socket GrenadeMuzzleSocketName.
	UFUNCTION(BlueprintPure, Category = "Vehicle|Weapon")
	FVector GetGrenadeMuzzleWorldLocation() const;

	// Yaw (degrees) the turret bone should point in chassis-local space to face the aim point.
	// Drive a Transform (Modify) Bone in the AnimBP with this value.
	UFUNCTION(BlueprintPure, Category = "Vehicle|Animation")
	float GetTurretYaw() const;

	// ===== Powerup mutators (called by the PlayerController on Apply) =====
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Ram|Powerup")
	void MultiplyRamRechargeRate(float Factor) { RamRechargeRate *= FMath::Max(0.f, Factor); }

	UFUNCTION(BlueprintCallable, Category = "Vehicle|Ram|Powerup")
	void MultiplyRamSelfDamage(float Factor) { RamSelfDamageMultiplier *= FMath::Clamp(Factor, 0.f, 1.f); }

	// ===== FX hooks (override in the BP_VehiclePawn to spawn Niagara + Sound) =====
	// Each shot of the machinegun. ShotEnd = where the bullet visually ended (hit point or
	// end of trace if no hit). Spawn a beam Niagara from MuzzleWorld to ShotEnd for the tracer.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnPrimaryFired(const FVector& MuzzleWorld, const FVector& AimDir, const FVector& ShotEnd);

	// Once per shotgun blast — for muzzle flash + sound. Pellet tracers are reported
	// separately via OnShotgunPelletFired (one call per pellet).
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnShotgunFired(const FVector& MuzzleWorld, const FVector& AimDir);

	// Fired once per pellet inside a shotgun blast. ShotEnd = where the pellet ended.
	// Spawn one beam tracer per call from MuzzleWorld to ShotEnd.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnShotgunPelletFired(const FVector& MuzzleWorld, const FVector& ShotEnd);

	// Each grenade launch (when RMB is released while grenade is equipped).
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnGrenadeLaunched(const FVector& MuzzleWorld, const FVector& LaunchVelocity);

	// Player started aiming the grenade launcher (RMB pressed). Spawn the trajectory VFX.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnGrenadeAimStarted();

	// Player released RMB (with or without firing). Destroy the trajectory VFX.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnGrenadeAimEnded();

	// Fired EVERY FRAME while the player is aiming the grenade launcher (RMB held).
	// PredictedLanding is where the grenade will hit if released this frame. Wire this
	// to a BP ground marker that tracks the parabolic landing live as the player aims.
	// Hidden on OnGrenadeAimEnded.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnGrenadeAimTick(const FVector& PredictedLanding);

	// Ram dash started. Spawn flame trail / color shift VFX, play sound.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnEnterRam(const FVector& RamDir);

	// Ram dash ended (duration expired or canceled). Stop the ram VFX.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnExitRam();

	// Player rammed an enemy mid-dash. Hit reaction / impact VFX. Already after damage applied.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnRammedEnemy(AActor* Enemy);

	// Vehicle just died. Spawn the explosion Cascade emitter at Location (Spawn Emitter at Location).
	// Fires before the actor is hidden, so any one-shot PS gets to play out independently.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnVehicleDeathFX(const FVector& Location);

	// ===== Drift FX (handbrake + moving) =====
	// Fires when the player engages the drift state: handbrake held AND speed > DriftSpeedThreshold.
	// BP override: spawn the rear-wheel smoke + skid sound + decal track on the ground.
	// Use Spawn Emitter Attached so the smoke follows the rear wheels as they move.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnEnterDrift();

	// Fires when the drift state ends: handbrake released OR speed dropped below threshold.
	// BP override: deactivate the smoke emitter (stop spawning new particles, let existing ones fade).
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnExitDrift();

	// ===== Low health FX =====
	// Fires once the moment current HP crosses BELOW LowHealthThreshold. BP override: spawn
	// engine smoke trail (attached, persistent) + sparks + low-HP alarm sound (looping).
	// CurrentHealth is the absolute HP value at the moment of the transition.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnEnterLowHealth(float CurrentHealth);

	// Fires once the moment current HP crosses BACK ABOVE LowHealthThreshold (heal via Workshop
	// powerup). BP override: stop the engine smoke, kill the alarm sound, screen vignette off.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnExitLowHealth(float CurrentHealth);

	// Fires every time the vehicle takes damage (any source). BP override: ClientStartCameraShake
	// on the player controller, hit-flash on the chassis material, low-pass audio sting, etc.
	// Amount is the damage actually applied (post-clamp) — scale shake intensity with this.
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|FX")
	void OnDamagedFX(float Amount, AActor* Causer);

protected:
	virtual void BeginPlay() override;

	// ===== Components =====
	UPROPERTY(VisibleAnywhere, Category = "Vehicle|Components")
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle|Components")
	TObjectPtr<USkeletalMeshComponent> VehicleMesh;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle|Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle|Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle|Components")
	TObjectPtr<UVehicleWeaponComponent> Weapon;

	UPROPERTY(VisibleAnywhere, Category = "Vehicle|Components")
	TObjectPtr<UHealthComponent> Health;

	// ===== Physics =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Physics", meta = (ClampMin = "100"))
	float ChassisMass = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Physics", meta = (ClampMin = "0"))
	float LinearDamping = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Physics", meta = (ClampMin = "0"))
	float AngularDamping = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Physics")
	FVector CenterOfMassOffset = FVector(0.f, 0.f, -30.f);

	// ===== Suspension =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Suspension", meta = (ClampMin = "1"))
	float WheelRadius = 35.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Suspension")
	TArray<FVector> WheelOffsets;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Suspension", meta = (ClampMin = "1"))
	float SuspensionRestLength = 60.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Suspension", meta = (ClampMin = "0"))
	float SpringStrength = 80000.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Suspension", meta = (ClampMin = "0"))
	float SpringDamping = 5000.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Suspension", meta = (ClampMin = "0"))
	float AntiRollStiffness = 30000.f;

	// ===== Drive =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0"))
	float MaxSpeed = 3611.f; // ~130 km/h (cm/s)

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0"))
	float AccelForce = 6500000.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0"))
	float BrakeForce = 7000000.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0", ClampMax = "1"))
	float ReverseForceFactor = 0.4f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0", ClampMax = "1"))
	float RollingResistance = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive")
	bool bAllWheelDrive = true;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0.1"))
	float EngineSpoolUpTime = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Drive", meta = (ClampMin = "0.1"))
	float EngineSpoolDownTime = 0.6f;

	// ===== Grip =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Grip", meta = (ClampMin = "0"))
	float LateralGripStrength = 8.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Grip", meta = (ClampMin = "0", ClampMax = "0.95"))
	float HighSpeedGripLoss = 0.6f;

	// ===== Handbrake (SpaceBar) =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Handbrake", meta = (ClampMin = "0"))
	float HandbrakeBrakeForce = 5000000.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Handbrake", meta = (ClampMin = "0", ClampMax = "1"))
	float HandbrakeRearGripScale = 0.05f;

	// Minimum |ForwardSpeed| (cm/s) for the drift FX state to engage. Below this — handbrake
	// while parked or rolling slowly — does NOT fire OnEnterDrift (no smoke when standing still).
	// 200 cm/s ≈ 7 km/h.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Handbrake", meta = (ClampMin = "0"))
	float DriftSpeedThreshold = 200.f;

	// ===== Low health FX threshold =====
	// Below this absolute HP value, OnEnterLowHealth fires (engine smoke, alarm). Crosses back
	// to OnExitLowHealth when healed above (e.g. via Officina Mobile powerup). Polled in Tick.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Health", meta = (ClampMin = "0"))
	float LowHealthThreshold = 25.f;

	// ===== Ram (LeftShift) — replaces nitro. Charge fills automatically; press consumes ALL
	// current charge for a forward dash that locks steering and damages enemies on contact. =====
	// All tunables BlueprintReadWrite so power-ups (e.g. Turbo, Telaio) can mutate them.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Ram", meta = (ClampMin = "1"))
	float RamMaxCharge = 100.f;

	// Charge units gained per second when not ramming. 100 / 33 ≈ 3s to fully charge from 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Ram", meta = (ClampMin = "0"))
	float RamRechargeRate = 33.f;

	// Charge units consumed per second while ram is held. 100 / 50 = 2s max ram on full charge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Ram", meta = (ClampMin = "0"))
	float RamDrainRate = 50.f;

	// Top-speed multiplier on MaxSpeed during the ram dash. 130 × 1.54 ≈ 200 km/h.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Ram", meta = (ClampMin = "1"))
	float RamSpeedMultiplier = 1.54f;

	// Drive force multiplier during the ram dash.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Ram", meta = (ClampMin = "1"))
	float RamAccelMultiplier = 2.0f;

	// Multiplier on the self-damage taken when ramming an enemy. 1.0 = full cost, 0.65 after
	// Telaio rinforzato powerup (-35% cost).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Ram", meta = (ClampMin = "0", ClampMax = "1"))
	float RamSelfDamageMultiplier = 1.0f;

	// Refill the ram tank by Amount (clamped to RamMaxCharge). Call from pickup actors.
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Ram")
	void AddRamCharge(float Amount);

	UFUNCTION(BlueprintPure, Category = "Vehicle|Ram")
	float GetCurrentRamCharge() const { return CurrentRamCharge; }

	// ===== Steering =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Steering", meta = (ClampMin = "0", ClampMax = "60"))
	float MaxSteerAngle = 35.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Steering", meta = (ClampMin = "0"))
	float SteerInterpSpeed = 8.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Steering", meta = (ClampMin = "0", ClampMax = "1"))
	float HighSpeedSteerReduction = 0.45f;

	// ===== Camera =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Camera", meta = (ClampMin = "100"))
	float CameraArmLength = 2000.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Camera")
	FRotator CameraArmRotation = FRotator(-55.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Vehicle|Camera", meta = (ClampMin = "0"))
	float CameraLagSpeed = 8.f;

	UPROPERTY(EditAnywhere, Category = "Vehicle|Camera", meta = (ClampMin = "30", ClampMax = "120"))
	float CameraFOV = 80.f;

	// ===== Weapon mount (vehicle-specific; weapon logic lives in UVehicleWeaponComponent) =====
	// Skeletal mesh socket for the primary hitscan muzzle (machinegun + shotgun share this).
	UPROPERTY(EditAnywhere, Category = "Vehicle|Weapon")
	FName PrimaryMuzzleSocketName = TEXT("MuzzleSocket_Primary");

	// Skeletal mesh socket for the grenade launcher muzzle.
	UPROPERTY(EditAnywhere, Category = "Vehicle|Weapon")
	FName GrenadeMuzzleSocketName = TEXT("MuzzleSocket_Grenade");

	// Bone name driven by GetTurretYaw() (read by the AnimBP, not by C++). Stored here
	// so designer and animator agree on the bone name in one place.
	UPROPERTY(EditAnywhere, Category = "Vehicle|Weapon")
	FName TurretBoneName = TEXT("turret");

	// Fallback used if a muzzle socket is missing on the mesh.
	UPROPERTY(EditAnywhere, Category = "Vehicle|Weapon")
	FVector MuzzleOffsetFallback = FVector(220.f, 0.f, 60.f);

	// Z offset from chassis at which the mouse cursor is projected onto a horizontal aiming plane
	UPROPERTY(EditAnywhere, Category = "Vehicle|Weapon")
	float AimPlaneOffset = 50.f;

	// ===== Debug =====
	UPROPERTY(EditAnywhere, Category = "Vehicle|Debug")
	bool bDrawSuspensionDebug = false;

private:
	// ----- Input state -----
	float ThrottleInput = 0.f;
	float SteerInput = 0.f;
	bool bHandbrakeActive = false;
	bool bRamHeld = false;

	// ----- Runtime state -----
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	float ForwardSpeed = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	float CurrentSteerAngle = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	float EngineRPM = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	float CurrentRamCharge = 0.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	bool bRamming = false;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	bool bDrifting = false;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Vehicle|State")
	bool bInLowHealthState = false;

	FVector RamDirection = FVector::ForwardVector;

	// Enemies already hit by the current ram dash (prevents double-damage on continued overlap).
	UPROPERTY(Transient)
	TSet<TObjectPtr<AActor>> RammedThisDash;

	UPROPERTY(Transient)
	TArray<float> WheelSpinAngles;

	UPROPERTY(Transient)
	TArray<float> SuspensionOffsets;

	UPROPERTY(Transient)
	TArray<bool> WheelGrounded;

	TArray<float> WheelCompressions;

	// ----- Input handlers -----
	void OnMoveForward(float Value);
	void OnMoveRight(float Value);
	void OnHandbrakePressed()  { bHandbrakeActive = true;  }
	void OnHandbrakeReleased() { bHandbrakeActive = false; }
	void OnRamPressed()        { bRamHeld = true;          }
	void OnRamReleased()       { bRamHeld = false;         }

	// Forwarded to Weapon component
	void OnFirePressed();
	void OnFireReleased();
	void OnFireSpecialPressed();
	void OnFireSpecialReleased();

	// ----- Tick steps -----
	void UpdateSteering(float DeltaTime);
	void UpdateEngine(float DeltaTime);
	void UpdateRamming(float DeltaTime);
	void UpdatePhysicsForces(float DeltaTime);
	void ApplyAntiRollBar();
	void UpdateAnimationState(float DeltaTime);

	// Polls drift + low-health state and fires the corresponding BIE on transitions.
	// Cheap (a couple of float comparisons + bool checks per tick).
	void UpdateFXStates();

	bool IsAnyWheelGrounded() const;

	void ApplyWheelForces(int32 WheelIndex, const FVector& WheelWorldPos, const FHitResult& Hit, float DeltaTime);

	void StartRam();
	void EndRam();

	UFUNCTION()
	void HandleHealthDeath(UHealthComponent* HealthComp, AActor* Causer);

	UFUNCTION()
	void HandleHealthDamaged(UHealthComponent* HealthComp, float Amount, float NewHealth, AActor* Causer);

	UFUNCTION()
	void HandleChassisBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
