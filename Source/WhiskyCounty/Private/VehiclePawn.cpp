// Fill out your copyright notice in the Description page of Project Settings.

#include "VehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EnemyBase.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HealthComponent.h"
#include "VehicleWeaponComponent.h"

AVehiclePawn::AVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetBoxExtent(FVector(220.f, 100.f, 60.f));
	CollisionBox->SetCollisionProfileName(TEXT("PhysicsActor"));
	CollisionBox->SetSimulatePhysics(true);
	CollisionBox->SetEnableGravity(true);
	CollisionBox->SetLinearDamping(LinearDamping);
	CollisionBox->SetAngularDamping(AngularDamping);
	// Pawn channel = enemy capsules. Symmetric Overlap (matching the enemy side) guarantees
	// Chaos generates no contact constraint, so a swarm cannot push the chassis around.
	// Overlap events still fire — ramming (D5) will hook them.
	CollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionBox->SetGenerateOverlapEvents(true);
	RootComponent = CollisionBox;

	VehicleMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VehicleMesh"));
	VehicleMesh->SetupAttachment(RootComponent);
	// Query-only collision: mesh shape blocks Pawn (enemies stop visually against the chassis)
	// but doesn't participate in physics simulation, so no fly-up or push on the chassis.
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VehicleMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VehicleMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	VehicleMesh->SetSimulatePhysics(false);
	VehicleMesh->SetRelativeLocation(FVector(0.f, 0.f, -60.f));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = CameraArmLength;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = CameraLagSpeed;
	SpringArm->SetRelativeRotation(CameraArmRotation);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = CameraFOV;

	Weapon = CreateDefaultSubobject<UVehicleWeaponComponent>(TEXT("Weapon"));

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));

	WheelOffsets = {
		FVector( 180.f, -85.f, 0.f), // FL
		FVector( 180.f,  85.f, 0.f), // FR
		FVector(-160.f, -85.f, 0.f), // RL
		FVector(-160.f,  85.f, 0.f), // RR
	};

	WheelSpinAngles.Init(0.f, 4);
	SuspensionOffsets.Init(0.f, 4);
	WheelGrounded.Init(false, 4);
	WheelCompressions.Init(0.f, 4);
}

void AVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	if (CollisionBox)
	{
		CollisionBox->SetMassOverrideInKg(NAME_None, ChassisMass, true);
		CollisionBox->SetLinearDamping(LinearDamping);
		CollisionBox->SetAngularDamping(AngularDamping);
		CollisionBox->SetCenterOfMass(CenterOfMassOffset);

		// Bind overlap so we can detect enemies the chassis touches during a ram dash.
		CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &AVehiclePawn::HandleChassisBeginOverlap);
	}

	if (SpringArm)
	{
		SpringArm->TargetArmLength = CameraArmLength;
		SpringArm->SetRelativeRotation(CameraArmRotation);
		SpringArm->CameraLagSpeed = CameraLagSpeed;
	}
	if (Camera)
	{
		Camera->FieldOfView = CameraFOV;
	}

	if (WheelSpinAngles.Num() != 4)    WheelSpinAngles.Init(0.f, 4);
	if (SuspensionOffsets.Num() != 4)  SuspensionOffsets.Init(0.f, 4);
	if (WheelGrounded.Num() != 4)      WheelGrounded.Init(false, 4);
	if (WheelCompressions.Num() != 4)  WheelCompressions.Init(0.f, 4);

	// Show the mouse cursor so the player can aim. Replace with a custom HUD crosshair later.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
	}

	if (Health)
	{
		Health->OnDeath.AddDynamic(this, &AVehiclePawn::HandleHealthDeath);
		Health->OnDamaged.AddDynamic(this, &AVehiclePawn::HandleHealthDamaged);
	}
}

float AVehiclePawn::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
                               AController* EventInstigator, AActor* DamageCauser)
{
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!Health) return 0.f;
	return Health->ApplyDamage(DamageAmount, DamageCauser);
}

void AVehiclePawn::HandleHealthDamaged(UHealthComponent* /*HealthComp*/, float Amount, float /*NewHealth*/, AActor* Causer)
{
	OnDamagedFX(Amount, Causer);
}

void AVehiclePawn::HandleHealthDeath(UHealthComponent* /*HealthComp*/, AActor* Causer)
{
	UE_LOG(LogTemp, Warning, TEXT("[Vehicle] Player died (caused by %s) — explosion + hide"),
		*GetNameSafe(Causer));

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}

	// Designer hooks the Cascade explosion in BP_VehiclePawn → "Spawn Emitter at Location" using
	// the passed-in Location.
	OnVehicleDeathFX(GetActorLocation());

	// Stop physics on the root (CollisionBox is what simulates) so the chassis freezes
	// in place — otherwise gravity pulls it through the floor after we disable collision,
	// dragging the camera with it. We don't Destroy() because the camera lives on this pawn
	// and the PlayerController still uses it as ViewTarget while the death widget is up.
	if (CollisionBox)
	{
		CollisionBox->SetSimulatePhysics(false);
	}
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
}

void AVehiclePawn::HandleChassisBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!bRamming) return;
	if (!OtherActor || OtherActor == this) return;

	AEnemyBase* Enemy = Cast<AEnemyBase>(OtherActor);
	if (!Enemy) return;

	// One-hit-per-dash: don't re-damage the same enemy on continued overlap.
	if (RammedThisDash.Contains(Enemy)) return;
	RammedThisDash.Add(Enemy);

	// Damage the enemy (scaled by enemy archetype via RamDamageDealt on EnemyBase).
	const float DmgToEnemy = Enemy->GetRamDamageDealt();
	if (DmgToEnemy > 0.f)
	{
		FDamageEvent DamageEvent;
		Enemy->TakeDamage(DmgToEnemy, DamageEvent, GetController(), this);
	}

	// Self-damage to the player — ramming is a trade-off, not a free attack.
	// Telaio rinforzato powerup reduces this via RamSelfDamageMultiplier.
	const float CostToSelf = Enemy->GetRamCostToPlayer() * RamSelfDamageMultiplier;
	if (CostToSelf > 0.f && Health)
	{
		Health->ApplyDamage(CostToSelf, Enemy);
	}

	OnRammedEnemy(Enemy);
}

void AVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent) return;

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AVehiclePawn::OnMoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"),   this, &AVehiclePawn::OnMoveRight);

	PlayerInputComponent->BindAction(TEXT("Handbrake"), IE_Pressed,  this, &AVehiclePawn::OnHandbrakePressed);
	PlayerInputComponent->BindAction(TEXT("Handbrake"), IE_Released, this, &AVehiclePawn::OnHandbrakeReleased);
	PlayerInputComponent->BindAction(TEXT("Ram"),       IE_Pressed,  this, &AVehiclePawn::OnRamPressed);
	PlayerInputComponent->BindAction(TEXT("Ram"),       IE_Released, this, &AVehiclePawn::OnRamReleased);

	PlayerInputComponent->BindAction(TEXT("Fire"), IE_Pressed,  this, &AVehiclePawn::OnFirePressed);
	PlayerInputComponent->BindAction(TEXT("Fire"), IE_Released, this, &AVehiclePawn::OnFireReleased);
	PlayerInputComponent->BindAction(TEXT("FireSpecial"), IE_Pressed,  this, &AVehiclePawn::OnFireSpecialPressed);
	PlayerInputComponent->BindAction(TEXT("FireSpecial"), IE_Released, this, &AVehiclePawn::OnFireSpecialReleased);
}

bool AVehiclePawn::IsAnyWheelGrounded() const
{
	for (bool bGrounded : WheelGrounded)
	{
		if (bGrounded) return true;
	}
	return false;
}

void AVehiclePawn::OnMoveForward(float Value) { ThrottleInput = Value; }
void AVehiclePawn::OnMoveRight  (float Value) { SteerInput    = Value; }

void AVehiclePawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateEngine(DeltaTime);
	UpdateRamming(DeltaTime);
	UpdateSteering(DeltaTime);
	UpdatePhysicsForces(DeltaTime);
	ApplyAntiRollBar();
	UpdateAnimationState(DeltaTime);
	UpdateFXStates();
}

void AVehiclePawn::UpdateFXStates()
{
	// ----- Drift state -----
	// Drift = handbrake held AND vehicle is moving fast enough to make smoke. Below the
	// threshold (parked or rolling slow) the FX would look silly so we suppress it.
	const bool bDriftingNow = bHandbrakeActive && FMath::Abs(ForwardSpeed) > DriftSpeedThreshold;
	if (bDriftingNow != bDrifting)
	{
		bDrifting = bDriftingNow;
		if (bDrifting) OnEnterDrift();
		else           OnExitDrift();
	}

	// ----- Low health state -----
	// Skip this branch entirely once the vehicle is dead — HandleHealthDeath does its own
	// FX cleanup and we don't want spurious OnExitLowHealth firing on the death frame.
	if (!Health || Health->IsDead()) return;

	const float CurrentHP = Health->GetCurrentHealth();
	const bool bIsLowNow = CurrentHP > 0.f && CurrentHP <= LowHealthThreshold;
	if (bIsLowNow != bInLowHealthState)
	{
		bInLowHealthState = bIsLowNow;
		if (bIsLowNow) OnEnterLowHealth(CurrentHP);
		else           OnExitLowHealth(CurrentHP);
	}
}

void AVehiclePawn::AddRamCharge(float Amount)
{
	CurrentRamCharge = FMath::Clamp(CurrentRamCharge + Amount, 0.f, RamMaxCharge);
}

void AVehiclePawn::StartRam()
{
	bRamming = true;

	// Snapshot the forward direction at ram start (useful for VFX trail orientation).
	RamDirection = GetActorForwardVector();
	RamDirection.Z = 0.f;
	RamDirection = RamDirection.GetSafeNormal();
	if (RamDirection.IsNearlyZero()) RamDirection = FVector::ForwardVector;

	RammedThisDash.Reset();
	OnEnterRam(RamDirection);
}

void AVehiclePawn::EndRam()
{
	bRamming = false;
	RammedThisDash.Reset();
	OnExitRam();
}

void AVehiclePawn::UpdateRamming(float DeltaTime)
{
	// Hold-to-use: ram is active while button held AND there's charge in the tank.
	const bool bWantRam = bRamHeld && CurrentRamCharge > 0.f;

	if (bWantRam && !bRamming)        StartRam();
	else if (!bWantRam && bRamming)   EndRam();

	if (bRamming)
	{
		CurrentRamCharge = FMath::Max(0.f, CurrentRamCharge - RamDrainRate * DeltaTime);
	}
	else if (!bRamHeld)
	{
		// Recharge only when the button is fully released — prevents stuttering refills
		// while the player is mashing the key with an empty tank.
		CurrentRamCharge = FMath::Min(RamMaxCharge, CurrentRamCharge + RamRechargeRate * DeltaTime);
	}
}

void AVehiclePawn::UpdateEngine(float DeltaTime)
{
	// Target RPM = how hard the player is asking the engine to spin.
	// Active when accelerating forward, or when reversing from low forward speed.
	const bool bAcceleratingForward = ThrottleInput > KINDA_SMALL_NUMBER;
	const bool bReversing           = ThrottleInput < -KINDA_SMALL_NUMBER && ForwardSpeed < 50.f;
	const float TargetRPM = (bAcceleratingForward || bReversing) ? FMath::Abs(ThrottleInput) : 0.f;

	const float TimeConst = (TargetRPM > EngineRPM)
		? FMath::Max(0.05f, EngineSpoolUpTime)
		: FMath::Max(0.05f, EngineSpoolDownTime);
	// FInterpTo with InterpSpeed = 1/timeConst yields ~63% of target after timeConst seconds
	EngineRPM = FMath::FInterpTo(EngineRPM, TargetRPM, DeltaTime, 1.f / TimeConst);
}

void AVehiclePawn::UpdateSteering(float DeltaTime)
{
	// Reduce max steer at high speed for stability
	const float SpeedRatio = (CollisionBox)
		? FMath::Clamp(FMath::Abs(ForwardSpeed) / FMath::Max(1.f, MaxSpeed), 0.f, 1.f)
		: 0.f;
	const float EffectiveMaxSteer = MaxSteerAngle * (1.f - HighSpeedSteerReduction * SpeedRatio);

	const float TargetSteer = SteerInput * EffectiveMaxSteer;
	CurrentSteerAngle = FMath::FInterpTo(CurrentSteerAngle, TargetSteer, DeltaTime, SteerInterpSpeed);
}

void AVehiclePawn::UpdatePhysicsForces(float DeltaTime)
{
	if (!CollisionBox) return;

	const FVector Velocity = CollisionBox->GetPhysicsLinearVelocity();
	const FVector Forward  = GetActorForwardVector();
	ForwardSpeed = FVector::DotProduct(Velocity, Forward);

	UWorld* World = GetWorld();
	if (!World) return;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleSuspension), false, this);
	Params.AddIgnoredActor(this);

	// Wheel suspension queries only world geometry. Querying by Visibility would also hit
	// enemy capsules (which need Visibility=Block for weapon hitscan), causing the chassis
	// to be lifted onto enemies driven through.
	FCollisionObjectQueryParams SuspensionObjectParams;
	SuspensionObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	SuspensionObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	const FTransform ActorTM = GetActorTransform();
	const int32 N = WheelOffsets.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector WheelWorld = ActorTM.TransformPosition(WheelOffsets[i]);
		const FVector TraceStart = WheelWorld;
		const FVector TraceEnd   = WheelWorld - FVector::UpVector * (SuspensionRestLength + WheelRadius);

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, SuspensionObjectParams, Params);

		if (i < WheelGrounded.Num()) WheelGrounded[i] = bHit;

		const float Compression = bHit
			? FMath::Max(0.f, (SuspensionRestLength + WheelRadius) - Hit.Distance)
			: 0.f;
		if (i < WheelCompressions.Num()) WheelCompressions[i] = Compression;

		if (i < SuspensionOffsets.Num())
		{
			const float TargetOffset = bHit
				? FMath::Clamp(Compression, 0.f, SuspensionRestLength)
				: 0.f;
			SuspensionOffsets[i] = FMath::FInterpTo(SuspensionOffsets[i], TargetOffset, DeltaTime, 15.f);
		}

		if (bDrawSuspensionDebug)
		{
			DrawDebugLine(World, TraceStart, bHit ? Hit.ImpactPoint : TraceEnd,
				bHit ? FColor::Green : FColor::Red, false, 0.f, 0, 2.f);
			if (bHit)
			{
				DrawDebugSphere(World, Hit.ImpactPoint, 6.f, 8, FColor::Yellow, false, 0.f, 0, 1.f);
			}
		}

		if (bHit)
		{
			ApplyWheelForces(i, WheelWorld, Hit, DeltaTime);
		}
	}
}

void AVehiclePawn::ApplyWheelForces(int32 WheelIndex, const FVector& WheelWorldPos, const FHitResult& Hit, float DeltaTime)
{
	if (!CollisionBox) return;

	const FVector WorldUp = FVector::UpVector;
	const FTransform ActorTM = GetActorTransform();

	const bool bIsFrontWheel = (WheelIndex == 0 || WheelIndex == 1);
	const bool bIsRearWheel  = !bIsFrontWheel;

	// Wheel-frame forward/right (front wheels include steer angle)
	const FRotator WheelLocalRot = bIsFrontWheel ? FRotator(0.f, CurrentSteerAngle, 0.f) : FRotator::ZeroRotator;
	FVector WheelForward = ActorTM.TransformVector(WheelLocalRot.RotateVector(FVector::ForwardVector));
	FVector WheelRight   = ActorTM.TransformVector(WheelLocalRot.RotateVector(FVector::RightVector));
	WheelForward.Z = 0.f; WheelForward = WheelForward.GetSafeNormal();
	WheelRight.Z   = 0.f; WheelRight   = WheelRight.GetSafeNormal();

	const FVector PointVel = CollisionBox->GetPhysicsLinearVelocityAtPoint(WheelWorldPos);

	// ----- Suspension (spring + damper, world up) -----
	const float Compression = (SuspensionRestLength + WheelRadius) - Hit.Distance;
	const float VerticalVel = FVector::DotProduct(PointVel, WorldUp);
	const float SpringForceMag = FMath::Max(0.f, Compression * SpringStrength - VerticalVel * SpringDamping);
	CollisionBox->AddForceAtLocation(WorldUp * SpringForceMag, WheelWorldPos);

	// ----- Lateral grip with speed-dependent loss + handbrake / drift modifiers -----
	const FVector PointVelPlanar = FVector(PointVel.X, PointVel.Y, 0.f);
	const float LateralVel = FVector::DotProduct(PointVelPlanar, WheelRight);
	const float ForwardVel = FVector::DotProduct(PointVelPlanar, WheelForward);

	const float SpeedRatioAbs = FMath::Clamp(FMath::Abs(ForwardSpeed) / FMath::Max(1.f, MaxSpeed), 0.f, 1.f);
	float GripScale = 1.f - HighSpeedGripLoss * SpeedRatioAbs;

	if (bIsRearWheel && bHandbrakeActive)
	{
		// Handbrake doubles as drift assist: rear grip drops AND brake force is applied,
		// so holding it slows you down while letting the tail swing wide.
		GripScale *= HandbrakeRearGripScale;
	}

	const float PerWheel = 1.f / FMath::Max(1, WheelOffsets.Num());
	const float LateralForceMag = -LateralVel * (ChassisMass * PerWheel) * LateralGripStrength * GripScale;
	CollisionBox->AddForceAtLocation(WheelRight * LateralForceMag, WheelWorldPos);

	// ----- Longitudinal: handbrake first (only on rear), then drive/brake/coast -----
	const bool bIsDriveWheel = bAllWheelDrive || bIsRearWheel; // RWD = rear only

	if (bHandbrakeActive && bIsRearWheel)
	{
		// Strong rear brake — opposite to the wheel's own forward velocity, so it works in both directions
		if (FMath::Abs(ForwardVel) > 1.f)
		{
			const float BrakeMag = HandbrakeBrakeForce * PerWheel;
			const FVector BrakeDir = -WheelForward * FMath::Sign(ForwardVel);
			CollisionBox->AddForceAtLocation(BrakeDir * BrakeMag, WheelWorldPos);
		}
	}
	else if (bIsDriveWheel && ThrottleInput > KINDA_SMALL_NUMBER)
	{
		// Forward drive, gated by EngineRPM (spool up) and falling off near MaxSpeed.
		// Ram dash multiplies both the soft speed cap and the accel force while active.
		const float EffectiveMaxSpeed   = MaxSpeed   * (bRamming ? RamSpeedMultiplier : 1.f);
		const float EffectiveAccelForce = AccelForce * (bRamming ? RamAccelMultiplier : 1.f);

		const float SpeedRatio = FMath::Clamp(ForwardSpeed / FMath::Max(1.f, EffectiveMaxSpeed), 0.f, 1.f);
		const float Falloff   = FMath::Max(0.f, 1.f - SpeedRatio);
		const float Mag = ThrottleInput * EngineRPM * EffectiveAccelForce * PerWheel * Falloff;
		CollisionBox->AddForceAtLocation(WheelForward * Mag, WheelWorldPos);
	}
	else if (ThrottleInput < -KINDA_SMALL_NUMBER)
	{
		if (ForwardSpeed > 50.f)
		{
			// Brake while moving forward
			const float Mag = BrakeForce * PerWheel;
			CollisionBox->AddForceAtLocation(-WheelForward * Mag, WheelWorldPos);
		}
		else if (bIsDriveWheel)
		{
			// Reverse drive (also gated by EngineRPM)
			const float ReverseMaxSpeed = MaxSpeed * ReverseForceFactor;
			const float SpeedRatio = FMath::Clamp(-ForwardSpeed / FMath::Max(1.f, ReverseMaxSpeed), 0.f, 1.f);
			const float Falloff   = FMath::Max(0.f, 1.f - SpeedRatio);
			const float Mag = ThrottleInput * EngineRPM * AccelForce * ReverseForceFactor * PerWheel * Falloff;
			CollisionBox->AddForceAtLocation(WheelForward * Mag, WheelWorldPos);
		}
	}
	else
	{
		// Coast: rolling resistance
		const float Mag = -ForwardVel * RollingResistance * (ChassisMass * PerWheel);
		CollisionBox->AddForceAtLocation(WheelForward * Mag, WheelWorldPos);
	}
}

void AVehiclePawn::ApplyAntiRollBar()
{
	if (!CollisionBox || WheelCompressions.Num() < 4) return;

	auto ApplyPair = [&](int32 LeftIdx, int32 RightIdx)
	{
		if (!WheelOffsets.IsValidIndex(LeftIdx) || !WheelOffsets.IsValidIndex(RightIdx)) return;

		const float TravelDelta = WheelCompressions[LeftIdx] - WheelCompressions[RightIdx];
		if (FMath::IsNearlyZero(TravelDelta)) return;

		const float ForceMag = TravelDelta * AntiRollStiffness;
		const FTransform ActorTM = GetActorTransform();
		const FVector LeftPos  = ActorTM.TransformPosition(WheelOffsets[LeftIdx]);
		const FVector RightPos = ActorTM.TransformPosition(WheelOffsets[RightIdx]);

		// Push down on the more compressed side, lift the other
		if (WheelGrounded.IsValidIndex(LeftIdx) && WheelGrounded[LeftIdx])
		{
			CollisionBox->AddForceAtLocation(-FVector::UpVector * ForceMag, LeftPos);
		}
		if (WheelGrounded.IsValidIndex(RightIdx) && WheelGrounded[RightIdx])
		{
			CollisionBox->AddForceAtLocation( FVector::UpVector * ForceMag, RightPos);
		}
	};

	ApplyPair(0, 1); // front pair
	ApplyPair(2, 3); // rear pair
}

void AVehiclePawn::UpdateAnimationState(float DeltaTime)
{
	const float SpinDelta = FMath::RadiansToDegrees(ForwardSpeed / FMath::Max(1.f, WheelRadius)) * DeltaTime;
	for (int32 i = 0; i < WheelSpinAngles.Num(); ++i)
	{
		WheelSpinAngles[i] = FMath::Fmod(WheelSpinAngles[i] + SpinDelta, 360.f);
	}
}

float AVehiclePawn::GetWheelSpinAngle(int32 WheelIndex) const
{
	return WheelSpinAngles.IsValidIndex(WheelIndex) ? WheelSpinAngles[WheelIndex] : 0.f;
}

float AVehiclePawn::GetSuspensionOffset(int32 WheelIndex) const
{
	return SuspensionOffsets.IsValidIndex(WheelIndex) ? SuspensionOffsets[WheelIndex] : 0.f;
}

bool AVehiclePawn::IsWheelGrounded(int32 WheelIndex) const
{
	return WheelGrounded.IsValidIndex(WheelIndex) ? WheelGrounded[WheelIndex] : false;
}

// ===== Aiming =====

bool AVehiclePawn::GetAimWorldPoint(FVector& OutPoint) const
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return false;

	FVector WorldOrigin, WorldDir;
	if (!PC->DeprojectMousePositionToWorld(WorldOrigin, WorldDir)) return false;

	// Twin-stick top-down: project the mouse ray onto a horizontal plane at
	// the chassis height + AimPlaneOffset. This gives a stable XY direction
	// regardless of what the cursor visually lands on (floor, wall, sky).
	// Projecting onto the world via line trace would put the aim point on the
	// floor, which is between the player and the enemy due to camera parallax,
	// and would make the shot direction miss elevated targets.
	if (FMath::IsNearlyZero(WorldDir.Z)) return false;

	const float AimZ = GetActorLocation().Z + AimPlaneOffset;
	const float t = (AimZ - WorldOrigin.Z) / WorldDir.Z;
	if (t <= 0.f) return false;

	OutPoint = WorldOrigin + WorldDir * t;
	return true;
}

FVector AVehiclePawn::GetPrimaryMuzzleWorldLocation() const
{
	if (VehicleMesh && PrimaryMuzzleSocketName != NAME_None &&
	    VehicleMesh->DoesSocketExist(PrimaryMuzzleSocketName))
	{
		return VehicleMesh->GetSocketLocation(PrimaryMuzzleSocketName);
	}
	return GetActorTransform().TransformPosition(MuzzleOffsetFallback);
}

FVector AVehiclePawn::GetGrenadeMuzzleWorldLocation() const
{
	if (VehicleMesh && GrenadeMuzzleSocketName != NAME_None &&
	    VehicleMesh->DoesSocketExist(GrenadeMuzzleSocketName))
	{
		return VehicleMesh->GetSocketLocation(GrenadeMuzzleSocketName);
	}
	return GetActorTransform().TransformPosition(MuzzleOffsetFallback);
}

float AVehiclePawn::GetTurretYaw() const
{
	FVector AimPoint;
	if (!GetAimWorldPoint(AimPoint)) return 0.f;

	// Vector from chassis to aim, projected horizontally and into chassis-local space.
	FVector ToAim = AimPoint - GetActorLocation();
	ToAim.Z = 0.f;
	if (ToAim.IsNearlyZero()) return 0.f;

	const FVector LocalAim = GetActorTransform().InverseTransformVectorNoScale(ToAim);
	return FMath::RadiansToDegrees(FMath::Atan2(LocalAim.Y, LocalAim.X));
}

// ===== Weapon (input forwarders to UVehicleWeaponComponent) =====

void AVehiclePawn::OnFirePressed()
{
	if (Weapon) Weapon->StartFirePrimary();
}

void AVehiclePawn::OnFireReleased()
{
	if (Weapon) Weapon->StopFirePrimary();
}

void AVehiclePawn::OnFireSpecialPressed()
{
	if (Weapon) Weapon->FireSpecialPressed();
}

void AVehiclePawn::OnFireSpecialReleased()
{
	if (Weapon) Weapon->FireSpecialReleased();
}
