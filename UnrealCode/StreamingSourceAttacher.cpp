#include "StreamingSourceAttacher.h"

#include "Blueprint/UserWidget.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "CineCameraComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#endif

// Prefer a camera on the source named "StreamCamera", else first camera
static UCameraComponent* FindStreamCameraOnSource(AActor* Source)
{
	if (!Source) return nullptr;
	TArray<UCameraComponent*> Cams;
	Source->GetComponents(Cams);
	for (UCameraComponent* Cam : Cams)
	{
		if (Cam && Cam->GetName().Contains(TEXT("StreamCamera")))
		{
			return Cam;
		}
	}
	return Cams.Num() > 0 ? Cams[0] : nullptr;
}

AStreamingSourceAttacher::AStreamingSourceAttacher()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.016f; // overridden in BeginPlay
	bReplicates = false;
	SetReplicatingMovement(false);
}

void AStreamingSourceAttacher::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld()) return;

	PrimaryActorTick.TickInterval = FMath::Max(0.001f, TickIntervalSeconds);
	AttemptCount = 0;
	bAttachmentComplete = false;
	RotationAccumSeconds.Empty();
	BelowSpeedTimeSeconds.Empty();

	if (!StreamingSourceClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[SSA] StreamingSourceClass not set!"));
		return;
	}

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[SSA] BeginPlay: Pattern='%s' SkipStationary=%d MinSpeedThreshold=%.2f km/h"),
			*VehicleNamePattern,
			bSkipStationaryVehicles ? 1 : 0,
			MinSpeedThresholdKmh);
	}

	if (StreamHUDClass)
	{
		StreamHUDWidget = CreateWidget(GetWorld(), StreamHUDClass);
	}

	if (!StreamWidgetRenderer.IsValid())
	{
		StreamWidgetRenderer = MakeShareable(new FWidgetRenderer(true));
	}
}

void AStreamingSourceAttacher::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FAttachedSourceInfo& Info : AttachedSources)
	{
		StopNDICaptureForSource(Info);
		if (bReturnToOriginalPosition && Info.Source.IsValid())
		{
			Info.Source->SetActorTransform(Info.OriginalTransform);
		}
	}
	AttachedSources.Empty();

	if (StreamWidgetRenderer.IsValid())
	{
		StreamWidgetRenderer.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AStreamingSourceAttacher::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Progressive discovery
	if (!bAttachmentComplete && AttemptCount < MaxCheckAttempts)
	{
		DiscoverAndAttach();
	}

	// Update captures
	for (FAttachedSourceInfo& Info : AttachedSources)
	{
		UpdateSceneCaptureForSource(Info, DeltaSeconds);
		if (bOverridePlayerViewToVehicle && Info.AttachedVehicleCamera)
		{
			ForcePlayerViewToCamera(Info.AttachedVehicleCamera);
		}
		Info.TimeAttached += DeltaSeconds;
	}

	// Per-source rotation with refactored speed checks (km/h)
	if (bRotationEnabled)
	{
		for (FAttachedSourceInfo& Info : AttachedSources)
		{
			AActor* CurrentVeh = Info.Vehicle.Get();
			if (!CurrentVeh) continue;

			float& Acc = RotationAccumSeconds.FindOrAdd(Info.Source);
			Acc += DeltaSeconds;

			const float TargetSec = 60.f * FMath::FRandRange(MinRotationMinutes, MaxRotationMinutes);

			// Get current speed in cm/s and convert to km/h
			const float CurrentSpeedCmS = CurrentVeh->GetVelocity().Size();
			const float CurrentSpeedKmh = CmSToKmh(CurrentSpeedCmS);

			// Check if below minimum speed for grace period tracking
			bool bBelowMinSpeed = false;
			if (!bIncludeStationaryOnRetarget && CurrentVehicleMinSpeedForRetargetKmh > 0.f)
			{
				float& BelowSpeedAcc = BelowSpeedTimeSeconds.FindOrAdd(Info.Vehicle);
				if (CurrentSpeedKmh < CurrentVehicleMinSpeedForRetargetKmh)
				{
					BelowSpeedAcc += DeltaSeconds;
					bBelowMinSpeed = true;
				}
				else
				{
					BelowSpeedAcc = 0.f;
					bBelowMinSpeed = false;
				}
			}

			// Condition 1: Force immediate retarget if below min speed for grace period AND flag enabled
			bool bShouldRetargetNow = false;
			if (bBelowMinSpeed && bForceRetargetOnBelowMinSpeed)
			{
				const float BelowSpeedAcc = BelowSpeedTimeSeconds.FindOrAdd(Info.Vehicle);
				if (BelowSpeedAcc >= RetargetBelowSpeedGraceSeconds)
				{
					bShouldRetargetNow = true;
				}
			}

			// Condition 2: Retarget if rotation timer reached (normal rotation)
			if (!bShouldRetargetNow && Acc >= TargetSec)
			{
				// If below min speed and NOT forcing retarget, restart timer and wait for vehicle to speed up
				if (bBelowMinSpeed && !bForceRetargetOnBelowMinSpeed)
				{
					Acc = 0.f;
					continue;
				}
				bShouldRetargetNow = true;
			}

			// Perform retarget if conditions met
			if (bShouldRetargetNow)
			{
				// Gather and choose new vehicle
				TArray<AActor*> Cands;
				GatherRetargetCandidates(Cands);
				AActor* NewVeh = nullptr;
				TArray<AActor*> Pool;
				for (AActor* C : Cands)
				{
					if (C && C != CurrentVeh)
					{
						Pool.Add(C);
					}
				}

				if (Pool.Num() > 0)
				{
					NewVeh = Pool[FMath::RandRange(0, Pool.Num() - 1)];
				}
				else if (Cands.Num() > 0)
				{
					NewVeh = Cands[0];
				}

				if (NewVeh && NewVeh != CurrentVeh)
				{
					RetargetOne(Info, NewVeh);
					BelowSpeedTimeSeconds.FindOrAdd(Info.Vehicle) = 0.f;
				}

				Acc = 0.f;
			}
		}
	}

	RenderHUDOverlay(DeltaSeconds);
}

// ========================================================================
// Discovery & Attach
// ========================================================================

void AStreamingSourceAttacher::DiscoverAndAttach()
{
	AttemptCount++;
	UWorld* World = GetWorld();
	if (!World) return;

	TArray<AActor*> FoundSources;
	for (TActorIterator<AActor> It(World, StreamingSourceClass); It; ++It)
	{
		AActor* Source = *It;
		if (Source)
		{
			FoundSources.Add(Source);
		}
	}

	if (bDebugLogging && FoundSources.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[SSA] Attempt %d: Found %d source(s)"), AttemptCount, FoundSources.Num());
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	TArray<AActor*> Vehicles;
	for (AActor* A : AllActors)
	{
		if (!A) continue;
		const FString ActorName = A->GetName();
		if (!ActorName.Contains(VehicleNamePattern)) continue;

		if (bSkipStationaryVehicles)
		{
			const float SpeedCmS = A->GetVelocity().Size();
			const float SpeedKmh = CmSToKmh(SpeedCmS);
			if (SpeedKmh < MinSpeedThresholdKmh)
			{
				continue;
			}
		}

		Vehicles.Add(A);
	}

	if (bDebugLogging && Vehicles.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[SSA] Found %d candidate vehicle(s)"), Vehicles.Num());
	}

	for (AActor* Source : FoundSources)
	{
		bool bAlreadyAttached = false;
		for (const FAttachedSourceInfo& Existing : AttachedSources)
		{
			if (Existing.Source == Source)
			{
				bAlreadyAttached = true;
				break;
			}
		}
		if (bAlreadyAttached) continue;

		for (AActor* Veh : Vehicles)
		{
			FAttachedSourceInfo NewInfo;
			if (TryAttachSourceToVehicle(Source, Veh, NewInfo))
			{
				AttachedSources.Add(NewInfo);
				if (bDebugLogging)
				{
					UE_LOG(LogTemp, Log, TEXT("[SSA] Attached '%s' -> '%s' (Stream: %s)"),
						*Source->GetName(),
						*Veh->GetName(),
						*NewInfo.StreamName);
				}

				if (bAttachToFirstVehicleOnly)
				{
					bAttachmentComplete = true;
					return;
				}
				break;
			}
		}
	}

	if (AttachedSources.Num() > 0)
	{
		bAttachmentComplete = true;
	}
}

bool AStreamingSourceAttacher::TryAttachSourceToVehicle(AActor* SourceActor, AActor* VehicleActor, FAttachedSourceInfo& OutInfo)
{
	if (!SourceActor || !VehicleActor) return false;

	OutInfo.OriginalTransform = SourceActor->GetActorTransform();
	OutInfo.Source = SourceActor;
	OutInfo.Vehicle = VehicleActor;
	OutInfo.TimeAttached = 0.f;

	UCameraComponent* VehCam = FindBestCameraOnActor(VehicleActor);
	if (!VehCam)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSA] No camera on vehicle '%s'"), *VehicleActor->GetName());
		}
		return false;
	}

	OutInfo.AttachedVehicleCamera = VehCam;
	SourceActor->AttachToActor(VehicleActor, FAttachmentTransformRules::KeepWorldTransform);

	FTransform VehCamTransform = VehCam->GetComponentTransform();
	VehCamTransform.AddToTranslation(StreamCameraOffset);
	VehCamTransform.SetRotation((VehCamTransform.Rotator() + StreamCameraRotationOffset).Quaternion());
	SourceActor->SetActorTransform(VehCamTransform);

	OutInfo.StreamName = MakeStreamName(SourceActor, VehicleActor);

	// Create render target
	if (bOutputRenderTarget)
	{
		const FString RTName = MakeRTName(OutInfo.StreamName);
		FString RTPath;
		UTextureRenderTarget2D* RT = CreatePersistentRenderTarget(RTName, RTPath);

		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSA] RT Created: %s (Valid=%d)"),
				*RTName, RT ? 1 : 0);
		}

		if (RT)
		{
			OutInfo.DynamicRenderTarget = RT;
			OutInfo.EditorRenderTarget = RT;
			OutInfo.EditorRenderTargetPath = RTPath;

			USceneCaptureComponent2D* Capture = FindSceneCaptureOnActor(SourceActor);
			if (Capture)
			{
				Capture->TextureTarget = RT;
				Capture->bCaptureEveryFrame = true;
				Capture->bCaptureOnMovement = false;

				if (bDebugLogging)
				{
					UE_LOG(LogTemp, Warning, TEXT("[SSA] SceneCapture configured"));
				}
			}
			else
			{
				if (bDebugLogging)
				{
					UE_LOG(LogTemp, Error, TEXT("[SSA] No SceneCapture found on source actor!"));
				}
			}

			// DEBUG: Check all NDI conditions
			if (bDebugLogging)
			{
				UE_LOG(LogTemp, Warning, TEXT("[SSA] NDI CHECKS:"));
				UE_LOG(LogTemp, Warning, TEXT(" - bOutputNDI: %d"), bOutputNDI ? 1 : 0);
				UE_LOG(LogTemp, Warning, TEXT(" - bAutoStartNDICapture: %d"), bAutoStartNDICapture ? 1 : 0);
				UE_LOG(LogTemp, Warning, TEXT(" - NDIMediaOutputAsset: %s"),
					NDIMediaOutputAsset ? *NDIMediaOutputAsset->GetName() : TEXT("NULL"));
				UE_LOG(LogTemp, Warning, TEXT(" - RT Valid: %d"), RT ? 1 : 0);
			}

			// Start NDI
			if (bOutputNDI && bAutoStartNDICapture)
			{
				if (bDebugLogging)
				{
					UE_LOG(LogTemp, Warning, TEXT("[SSA] *** CALLING StartNDICaptureForSource ***"));
				}
				StartNDICaptureForSource(OutInfo, RT);
			}
			else
			{
				if (bDebugLogging)
				{
					UE_LOG(LogTemp, Error, TEXT("[SSA] *** NDI START SKIPPED - Condition failed! ***"));
				}
			}
		}
		else
		{
			if (bDebugLogging)
			{
				UE_LOG(LogTemp, Error, TEXT("[SSA] Failed to create render target!"));
			}
		}
	}
	else
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSA] bOutputRenderTarget is FALSE - skipping RT and NDI"));
		}
	}

	return true;
}

// ========================================================================
// Camera / Capture Helpers
// ========================================================================

UCameraComponent* AStreamingSourceAttacher::FindBestCameraOnActor(AActor* Actor) const
{
	if (!Actor) return nullptr;
	TArray<UCameraComponent*> Cams;
	Actor->GetComponents(Cams);

	for (UCameraComponent* Cam : Cams)
	{
		if (Cast<UCineCameraComponent>(Cam))
		{
			return Cam;
		}
	}

	return Cams.Num() > 0 ? Cams[0] : nullptr;
}

USceneCaptureComponent2D* AStreamingSourceAttacher::FindSceneCaptureOnActor(AActor* Actor) const
{
	if (!Actor) return nullptr;
	TArray<USceneCaptureComponent2D*> Captures;
	Actor->GetComponents(Captures);
	return Captures.Num() > 0 ? Captures[0] : nullptr;
}

void AStreamingSourceAttacher::UpdateSceneCaptureForSource(FAttachedSourceInfo& Info, float DeltaSeconds)
{
	AActor* Source = Info.Source.Get();
	AActor* Vehicle = Info.Vehicle.Get();
	if (!Source || !Vehicle) return;

	UCameraComponent* VehCam = Info.AttachedVehicleCamera;
	if (!VehCam) return;

	FTransform VehCamTransform = VehCam->GetComponentTransform();
	VehCamTransform.AddToTranslation(StreamCameraOffset);
	VehCamTransform.SetRotation((VehCamTransform.Rotator() + StreamCameraRotationOffset).Quaternion());
	Source->SetActorTransform(VehCamTransform);

	USceneCaptureComponent2D* Capture = FindSceneCaptureOnActor(Source);
	if (Capture)
	{
		Capture->FOVAngle = VehCam->FieldOfView;
	}
}

void AStreamingSourceAttacher::ForcePlayerViewToCamera(UCameraComponent* VehicleCamera)
{
	if (!VehicleCamera) return;
	UWorld* World = GetWorld();
	if (!World) return;

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return;

	PC->SetViewTarget(VehicleCamera->GetOwner());
}

// ========================================================================
// Asset / Naming Helpers
// ========================================================================

UTextureRenderTarget2D* AStreamingSourceAttacher::CreatePersistentRenderTarget(const FString& RTBaseName, FString& OutAssetPath)
{
	const int32 ScaledWidth = FMath::Max(1, FMath::RoundToInt(RenderTargetWidth * ResolutionScale));
	const int32 ScaledHeight = FMath::Max(1, FMath::RoundToInt(RenderTargetHeight * ResolutionScale));

	// Always create runtime transient RT
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, *RTBaseName);
	if (!RT) return nullptr;

	RT->InitAutoFormat(ScaledWidth, ScaledHeight);
	RT->UpdateResourceImmediate(true);

	OutAssetPath = FString::Printf(TEXT("Transient:%s"), *RTBaseName);

#if WITH_EDITOR
	// Only create persistent asset if user wants it
	if (bCreatePersistentRTAssets)
	{
		const FString PackageName = RenderTargetFolder + RTBaseName;
		UPackage* Package = CreatePackage(*PackageName);
		if (Package)
		{
			UTextureRenderTarget2D* EditorRT = NewObject<UTextureRenderTarget2D>(Package, *RTBaseName, RF_Public | RF_Standalone);
			if (EditorRT)
			{
				EditorRT->InitAutoFormat(ScaledWidth, ScaledHeight);
				EditorRT->UpdateResourceImmediate(true);
				RegisterAssetInEditor(EditorRT);
				OutAssetPath = PackageName;

				if (bDebugLogging)
				{
					UE_LOG(LogTemp, Log, TEXT("[SSA] Created persistent RT asset: %s"), *PackageName);
				}
			}
		}
	}
#endif

	return RT;
}

void AStreamingSourceAttacher::RegisterAssetInEditor(UObject* Asset)
{
#if WITH_EDITOR
	if (!Asset) return;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().AssetCreated(Asset);
	Asset->MarkPackageDirty();
#endif
}

FString AStreamingSourceAttacher::MakeStreamName(const AActor* Source, const AActor* Vehicle) const
{
	if (!Source || !Vehicle) return TEXT("Unknown");
	return FString::Printf(TEXT("%s_on_%s"), *Source->GetName(), *Vehicle->GetName());
}

FString AStreamingSourceAttacher::MakeRTName(const FString& StreamName) const
{
	return FString::Printf(TEXT("RT_%s"), *StreamName);
}

FString AStreamingSourceAttacher::MakeNDIName(const FString& StreamName) const
{
	return FString::Printf(TEXT("%s%s"), *NDIPrefix, *StreamName);
}

// ========================================================================
// Retargeting / Rotation (Refactored for km/h)
// ========================================================================

void AStreamingSourceAttacher::GatherRetargetCandidates(TArray<AActor*>& OutVehicles) const
{
	OutVehicles.Empty();
	UWorld* World = GetWorld();
	if (!World) return;

	const FString PatternToUse = RetargetNamePatternOverride.IsEmpty() ? VehicleNamePattern : RetargetNamePatternOverride;
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	for (AActor* A : AllActors)
	{
		if (!A) continue;
		const FString ActorName = A->GetName();
		if (!ActorName.Contains(PatternToUse)) continue;

		// Filter by candidate minimum speed (prevent stationary cars) - km/h
		if (!bIncludeStationaryOnRetarget && CandidateMinSpeedKmh > 0.f)
		{
			const float SpeedCmS = A->GetVelocity().Size();
			const float SpeedKmh = CmSToKmh(SpeedCmS);
			if (SpeedKmh < CandidateMinSpeedKmh) continue;
		}

		OutVehicles.Add(A);
	}
}

void AStreamingSourceAttacher::RetargetOne(FAttachedSourceInfo& Info, AActor* NewVehicle)
{
	if (!NewVehicle) return;
	AActor* Source = Info.Source.Get();
	if (!Source) return;

	UCameraComponent* NewCam = FindBestCameraOnActor(NewVehicle);
	if (!NewCam)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSA] No camera on new vehicle '%s'"), *NewVehicle->GetName());
		}
		return;
	}

	Source->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Source->AttachToActor(NewVehicle, FAttachmentTransformRules::KeepWorldTransform);

	FTransform NewCamTransform = NewCam->GetComponentTransform();
	NewCamTransform.AddToTranslation(StreamCameraOffset);
	NewCamTransform.SetRotation((NewCamTransform.Rotator() + StreamCameraRotationOffset).Quaternion());
	Source->SetActorTransform(NewCamTransform);

	Info.Vehicle = NewVehicle;
	Info.AttachedVehicleCamera = NewCam;
	Info.TimeAttached = 0.f;
	Info.StreamName = MakeStreamName(Source, NewVehicle);

	if (bKeepRenderTargetAcrossRetarget)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[SSA] Retarget: '%s' keeping RT, now on '%s'"),
				*Source->GetName(), *NewVehicle->GetName());
		}
	}
	else
	{
		StopNDICaptureForSource(Info);
		const FString RTName = MakeRTName(Info.StreamName);
		FString RTPath;
		UTextureRenderTarget2D* NewRT = CreatePersistentRenderTarget(RTName, RTPath);

		if (NewRT)
		{
			Info.DynamicRenderTarget = NewRT;
			Info.EditorRenderTarget = NewRT;
			Info.EditorRenderTargetPath = RTPath;

			USceneCaptureComponent2D* Capture = FindSceneCaptureOnActor(Source);
			if (Capture)
			{
				Capture->TextureTarget = NewRT;
			}

			if (bOutputNDI && bAutoStartNDICapture)
			{
				StartNDICaptureForSource(Info, NewRT);
			}
		}
	}
}

// ========================================================================
// NDI Media
// ========================================================================

void AStreamingSourceAttacher::StartNDICaptureForSource(FAttachedSourceInfo& Info, UTextureRenderTarget2D* InRT)
{
	if (!bOutputNDI || !NDIMediaOutputAsset || !InRT) return;

	// Stop any existing capture
	if (Info.NDIMediaCapture)
	{
		Info.NDIMediaCapture->StopCapture(false);
		Info.NDIMediaCapture = nullptr;
	}

	Info.DynamicRenderTarget = InRT;

	// Point SceneCapture at RT
	if (AActor* SourceActor = Info.Source.Get())
	{
		if (USceneCaptureComponent2D* Capture = FindSceneCaptureOnActor(SourceActor))
		{
			Capture->TextureTarget = InRT;
			Capture->PostProcessSettings = NDIPostProcessSettings;
			Capture->PostProcessBlendWeight = NDIPostProcessBlendWeight;
		}
	}

	// *** CREATE UNIQUE NDI OUTPUT FOR EACH SOURCE ***
	const FString UniqueNDIName = MakeNDIName(Info.StreamName);

	// Clone the NDI output asset to set unique stream name
	UNDIMediaOutput* ClonedOutput = DuplicateObject<UNDIMediaOutput>(NDIMediaOutputAsset, this);
	if (!ClonedOutput)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Error, TEXT("[SSA] Failed to clone NDI output asset"));
		}
		return;
	}

	// Set the unique stream name
	ClonedOutput->SourceName = UniqueNDIName;

	// Create media capture from cloned output
	UMediaCapture* NewCapture = ClonedOutput->CreateMediaCapture();
	if (!NewCapture)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSA] Failed to create NDIMediaCapture for stream '%s'."), *Info.StreamName);
		}
		return;
	}

	FMediaCaptureOptions CaptureOptions;
	CaptureOptions.bSkipFrameWhenRunningExpensiveTasks = false;

	const bool bStarted = NewCapture->CaptureTextureRenderTarget2D(InRT, CaptureOptions);
	if (!bStarted)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSA] Failed to start NDI capture for stream '%s'."), *Info.StreamName);
		}
		NewCapture->StopCapture(false);
		return;
	}

	Info.NDIMediaCapture = NewCapture;
	NDICaptureRenderTarget = InRT;

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[SSA] ✓ Started NDI capture '%s' on RT '%s' (%dx%d)."),
			*UniqueNDIName, *InRT->GetName(), InRT->SizeX, InRT->SizeY);
	}
}

void AStreamingSourceAttacher::StopNDICaptureForSource(FAttachedSourceInfo& Info)
{
	if (Info.NDIMediaCapture)
	{
		Info.NDIMediaCapture->StopCapture(false);
		Info.NDIMediaCapture = nullptr;
	}
}

// ========================================================================
// HUD Overlay
// ========================================================================

void AStreamingSourceAttacher::RenderHUDOverlay(float DeltaSeconds)
{
	if (!StreamHUDWidget || !StreamWidgetRenderer.IsValid()) return;
	if (!NDICaptureRenderTarget) return;

	const FVector2D DrawSize(RenderTargetWidth * ResolutionScale, RenderTargetHeight * ResolutionScale);
	TSharedRef<SWidget> SlateWidget = StreamHUDWidget->TakeWidget();

	StreamWidgetRenderer->DrawWidget(
		NDICaptureRenderTarget,
		SlateWidget,
		DrawSize,
		DeltaSeconds
	);
}