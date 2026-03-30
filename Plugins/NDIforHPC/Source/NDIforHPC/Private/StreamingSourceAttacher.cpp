#include "Public/StreamingSourceAttacher.h"
#include "Public/NDIforHPCLog.h"

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

AStreamingSourceAttacher::AStreamingSourceAttacher()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.016f;
	bReplicates = false;
	SetReplicatingMovement(false);
	bIsActive = true;
}

void AStreamingSourceAttacher::BeginPlay()
{
	Super::BeginPlay();
	
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] Invalid world pointer!"));
		return;
	}

	PrimaryActorTick.TickInterval = FMath::Max(0.001f, TickIntervalSeconds);
	AttemptCount = 0;
	bAttachmentComplete = false;
	RotationAccumSeconds.Empty();
	BelowSpeedTimeSeconds.Empty();
	CachedVehicles.Empty();
	CachedSources.Empty();
	CacheRefreshTimer = 0.f;
	NDIFailureCount = 0;
	NDIRetryTimer = 0.f;

	if (!StreamingSourceClass)
	{
		UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] StreamingSourceClass not set!"));
		return;
	}

	if (bDebugLogging)
	{
		UE_LOG(LogNDIforHPC, Log,
			TEXT("[SSA] BeginPlay: Pattern='%s' SkipStationary=%d MinSpeedThreshold=%.2f km/h"),
			*VehicleNamePattern,
			bSkipStationaryVehicles ? 1 : 0,
			MinSpeedThresholdKmh);
	}

	if (StreamHUDClass)
	{
		StreamHUDWidget = CreateWidget(World, StreamHUDClass);
	}

	if (!StreamWidgetRenderer.IsValid())
	{
		StreamWidgetRenderer = MakeShareable(new FWidgetRenderer(true));
	}
}

void AStreamingSourceAttacher::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Stop all NDI captures
	for (FAttachedSourceInfo& Info : AttachedSources)
	{
		StopNDICaptureForSource(Info);
		
		// Clean up render target if owned
		if (Info.DynamicRenderTarget && !Info.DynamicRenderTarget->IsSupportedForSaveGame())
		{
			Info.DynamicRenderTarget->ConditionalRemoveFromRoot();
		}
		
		if (bReturnToOriginalPosition && Info.Source.IsValid())
		{
			Info.Source->SetActorTransform(Info.OriginalTransform);
		}
	}
	AttachedSources.Empty();

	// Clean up cloned NDI outputs
	CleanupClonedOutputs();

	// Clean up HUD
	if (StreamWidgetRenderer.IsValid())
	{
		StreamWidgetRenderer.Reset();
	}

	if (StreamHUDWidget)
	{
		StreamHUDWidget->RemoveFromParent();
		StreamHUDWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AStreamingSourceAttacher::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Update debug properties
	CurrentAttachedCount = AttachedSources.Num();
	bNDIActive = AttachedSources.Num() > 0 && bOutputNDI;

	// Skip ticking if inactive and no sources
	if (!bIsActive && AttachedSources.Num() == 0)
	{
		return;
	}

	// Progressive discovery
	if (!bAttachmentComplete && AttemptCount < MaxCheckAttempts)
	{
		DiscoverAndAttach();
	}

	// Refresh cache periodically
	if (CacheRefreshTimer >= CacheRefreshInterval)
	{
		RefreshActorCache();
		CacheRefreshTimer = 0.f;
	}
	CacheRefreshTimer += DeltaSeconds;

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

	// Per-source rotation
	if (bRotationEnabled)
	{
		for (FAttachedSourceInfo& Info : AttachedSources)
		{
			AActor* CurrentVeh = Info.Vehicle.Get();
			if (!CurrentVeh) continue;

			float& Acc = RotationAccumSeconds.FindOrAdd(Info.Source);
			Acc += DeltaSeconds;

			const float TargetSec = 60.f * FMath::FRandRange(MinRotationMinutes, MaxRotationMinutes);

			const float CurrentSpeedCmS = CurrentVeh->GetVelocity().Size();
			const float CurrentSpeedKmh = CmSToKmh(CurrentSpeedCmS);

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

			bool bShouldRetargetNow = false;
			if (bBelowMinSpeed && bForceRetargetOnBelowMinSpeed)
			{
				const float BelowSpeedAcc = BelowSpeedTimeSeconds.FindOrAdd(Info.Vehicle);
				if (BelowSpeedAcc >= RetargetBelowSpeedGraceSeconds)
				{
					bShouldRetargetNow = true;
				}
			}

			if (!bShouldRetargetNow && Acc >= TargetSec)
			{
				if (bBelowMinSpeed && !bForceRetargetOnBelowMinSpeed)
				{
					Acc = 0.f;
					continue;
				}
				bShouldRetargetNow = true;
			}

			if (bShouldRetargetNow)
			{
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

	// NDI retry logic
	if (NDIFailureCount > 0 && NDIRetryTimer >= NDIRetryDelay)
	{
		if (NDIFailureCount <= MaxNDIRetries)
		{
			for (FAttachedSourceInfo& Info : AttachedSources)
			{
				if (!Info.NDIMediaCapture && Info.DynamicRenderTarget && bOutputNDI)
				{
					if (bDebugLogging)
					{
						UE_LOG(LogNDIforHPC, Verbose, TEXT("[SSA] Retrying NDI capture (attempt %d/%d)"), NDIFailureCount + 1, MaxNDIRetries);
					}
					StartNDICaptureForSource(Info, Info.DynamicRenderTarget);
				}
			}
			NDIFailureCount++;
			NDIRetryTimer = 0.f;
		}
		else
		{
			UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] NDI capture failed after %d retries. Giving up."), MaxNDIRetries);
			LastNDIError = FString::Printf(TEXT("NDI capture failed after %d retries"), MaxNDIRetries);
			NDIFailureCount = 0;
		}
	}
	else if (NDIFailureCount > 0)
	{
		NDIRetryTimer += DeltaSeconds;
	}

	RenderHUDOverlay(DeltaSeconds);
}

// ========================================================================
// Blueprint Functions
// ========================================================================

void AStreamingSourceAttacher::AttachSourceManually(AActor* SourceActor, AActor* VehicleActor)
{
	if (!SourceActor || !VehicleActor)
	{
		UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] AttachSourceManually: Invalid actor parameters"));
		return;
	}

	if (!SourceActor->IsA(StreamingSourceClass))
	{
		UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] AttachSourceManually: Source is not of StreamingSourceClass"));
		return;
	}

	FAttachedSourceInfo NewInfo;
	if (TryAttachSourceToVehicle(SourceActor, VehicleActor, NewInfo))
	{
		AttachedSources.Add(NewInfo);
		OnSourceAttached.Broadcast(SourceActor, NewInfo.StreamName);
		
		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Manually attached '%s' -> '%s' (Stream: %s)"),
				*SourceActor->GetName(),
				*VehicleActor->GetName(),
				*NewInfo.StreamName);
		}
	}
	else
	{
		UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] Failed to attach source manually"));
	}
}

void AStreamingSourceAttacher::DetachSource(AActor* SourceActor)
{
	if (!SourceActor) return;

	for (int32 i = 0; i < AttachedSources.Num(); i++)
	{
		if (AttachedSources[i].Source == SourceActor)
		{
			FString StreamName = AttachedSources[i].StreamName;
			DetachSourceInternal(AttachedSources[i]);
			AttachedSources.RemoveAt(i);
			OnSourceDetached.Broadcast(SourceActor, StreamName);
			return;
		}
	}
}

void AStreamingSourceAttacher::DetachAllSources()
{
	while (AttachedSources.Num() > 0)
	{
		DetachSourceInternal(AttachedSources[0]);
		AttachedSources.RemoveAt(0);
	}
}

void AStreamingSourceAttacher::SetActive(bool bActive)
{
	bIsActive = bActive;
	SetActorTickEnabled(bActive || AttachedSources.Num() > 0);
}

void AStreamingSourceAttacher::ForceRetarget()
{
	if (!bRotationEnabled || AttachedSources.Num() == 0) return;

	for (FAttachedSourceInfo& Info : AttachedSources)
	{
		AActor* CurrentVeh = Info.Vehicle.Get();
		if (!CurrentVeh) continue;

		TArray<AActor*> Cands;
		GatherRetargetCandidates(Cands);
		
		for (AActor* C : Cands)
		{
			if (C && C != CurrentVeh)
			{
				RetargetOne(Info, C);
				break;
			}
		}
	}
}

bool AStreamingSourceAttacher::IsSourceAttached(AActor* SourceActor) const
{
	if (!SourceActor) return false;
	for (const FAttachedSourceInfo& Info : AttachedSources)
	{
		if (Info.Source == SourceActor) return true;
	}
	return false;
}

FString AStreamingSourceAttacher::GetStreamNameForSource(AActor* SourceActor) const
{
	if (!SourceActor) return FString();
	for (const FAttachedSourceInfo& Info : AttachedSources)
	{
		if (Info.Source == SourceActor) return Info.StreamName;
	}
	return FString();
}

// ========================================================================
// Discovery & Attach
// ========================================================================

void AStreamingSourceAttacher::DiscoverAndAttach()
{
	AttemptCount++;
	UWorld* World = GetWorld();
	if (!World) return;

	// Refresh cache if empty
	if (CachedSources.Num() == 0)
	{
		RefreshActorCache();
	}

	TArray<AActor*> FoundSources = CachedSources;

	if (bDebugLogging && FoundSources.Num() > 0)
	{
		UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Attempt %d: Found %d source(s)"), AttemptCount, FoundSources.Num());
	}

	// Use TActorIterator instead of GetAllActorsOfClass for better performance
	TArray<AActor*> Vehicles;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
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

	// Update cache
	CachedVehicles = Vehicles;

	if (bDebugLogging && Vehicles.Num() > 0)
	{
		UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Found %d candidate vehicle(s)"), Vehicles.Num());
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
				OnSourceAttached.Broadcast(Source, NewInfo.StreamName);
				
				if (bDebugLogging)
				{
					UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Attached '%s' -> '%s' (Stream: %s)"),
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

void AStreamingSourceAttacher::RefreshActorCache()
{
	UWorld* World = GetWorld();
	if (!World) return;

	CachedSources.Empty();
	for (TActorIterator<AActor> It(World, StreamingSourceClass); It; ++It)
	{
		AActor* Source = *It;
		if (Source)
		{
			CachedSources.Add(Source);
		}
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
			UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] No camera on vehicle '%s'"), *VehicleActor->GetName());
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

	if (bOutputRenderTarget)
	{
		const FString RTName = MakeRTName(OutInfo.StreamName);
		FString RTPath;
		UTextureRenderTarget2D* RT = CreatePersistentRenderTarget(RTName, RTPath);

		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Verbose, TEXT("[SSA] RT Created: %s (Valid=%d)"),
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
					UE_LOG(LogNDIforHPC, Verbose, TEXT("[SSA] SceneCapture configured"));
				}
			}
			else
			{
				if (bDebugLogging)
				{
					UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] No SceneCapture found on source actor!"));
				}
			}

			if (bDebugLogging)
			{
				UE_LOG(LogNDIforHPC, Verbose, TEXT("[SSA] NDI CHECKS:"));
				UE_LOG(LogNDIforHPC, Verbose, TEXT(" - bOutputNDI: %d"), bOutputNDI ? 1 : 0);
				UE_LOG(LogNDIforHPC, Verbose, TEXT(" - bAutoStartNDICapture: %d"), bAutoStartNDICapture ? 1 : 0);
				UE_LOG(LogNDIforHPC, Verbose, TEXT(" - NDIMediaOutputAsset: %s"),
					NDIMediaOutputAsset ? *NDIMediaOutputAsset->GetName() : TEXT("NULL"));
				UE_LOG(LogNDIforHPC, Verbose, TEXT(" - RT Valid: %d"), RT ? 1 : 0);
			}

			if (bOutputNDI && bAutoStartNDICapture)
			{
				if (bDebugLogging)
				{
					UE_LOG(LogNDIforHPC, Verbose, TEXT("[SSA] *** CALLING StartNDICaptureForSource ***"));
				}
				StartNDICaptureForSource(OutInfo, RT);
			}
			else
			{
				if (bDebugLogging)
				{
					UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] *** NDI START SKIPPED - Condition failed! ***"));
				}
			}
		}
		else
		{
			if (bDebugLogging)
			{
				UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] Failed to create render target!"));
			}
		}
	}
	else
	{
		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Verbose, TEXT("[SSA] bOutputRenderTarget is FALSE - skipping RT and NDI"));
		}
	}

	return true;
}

void AStreamingSourceAttacher::DetachSourceInternal(FAttachedSourceInfo& Info)
{
	StopNDICaptureForSource(Info);
	
	if (Info.DynamicRenderTarget && !Info.DynamicRenderTarget->IsSupportedForSaveGame())
	{
		Info.DynamicRenderTarget->ConditionalRemoveFromRoot();
	}
	
	if (Info.Source.IsValid() && bReturnToOriginalPosition)
	{
		Info.Source->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Info.Source->SetActorTransform(Info.OriginalTransform);
	}
	
	OnSourceDetached.Broadcast(Info.Source.Get(), Info.StreamName);
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
	const int32 ScaledWidth = FMath::Clamp(FMath::RoundToInt(RenderTargetWidth * ResolutionScale), 16, 8192);
	const int32 ScaledHeight = FMath::Clamp(FMath::RoundToInt(RenderTargetHeight * ResolutionScale), 16, 8192);

	if (ScaledWidth <= 0 || ScaledHeight <= 0)
	{
		UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] Invalid render target dimensions: %d x %d"), 
			RenderTargetWidth, RenderTargetHeight);
		return nullptr;
	}

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, *RTBaseName);
	if (!RT) return nullptr;

	RT->InitAutoFormat(ScaledWidth, ScaledHeight);
	RT->UpdateResourceImmediate(true);

	OutAssetPath = FString::Printf(TEXT("Transient:%s"), *RTBaseName);

#if WITH_EDITOR
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
					UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Created persistent RT asset: %s"), *PackageName);
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
	return SanitizeStreamName(FString::Printf(TEXT("%s_on_%s"), *Source->GetName(), *Vehicle->GetName()));
}

FString AStreamingSourceAttacher::MakeRTName(const FString& StreamName) const
{
	return FString::Printf(TEXT("RT_%s"), *StreamName);
}

FString AStreamingSourceAttacher::MakeNDIName(const FString& StreamName) const
{
	return FString::Printf(TEXT("%s%s"), *NDIPrefix, *SanitizeStreamName(StreamName));
}

FString AStreamingSourceAttacher::SanitizeStreamName(const FString& Name) const
{
	FString Sanitized = Name;
	Sanitized.ReplaceInline(TEXT(" "), TEXT("_"));
	Sanitized.ReplaceInline(TEXT(":"), TEXT("_"));
	Sanitized.ReplaceInline(TEXT("/"), TEXT("_"));
	Sanitized.ReplaceInline(TEXT("\\"), TEXT("_"));
	return Sanitized;
}

// ========================================================================
// Retargeting
// ========================================================================

void AStreamingSourceAttacher::GatherRetargetCandidates(TArray<AActor*>& OutVehicles) const
{
	OutVehicles.Empty();
	UWorld* World = GetWorld();
	if (!World) return;

	const FString PatternToUse = RetargetNamePatternOverride.IsEmpty() ? VehicleNamePattern : RetargetNamePatternOverride;
	
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		const FString ActorName = A->GetName();
		if (!ActorName.Contains(PatternToUse)) continue;

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

	FString OldVehicleName = Info.Vehicle.IsValid() ? Info.Vehicle->GetName() : TEXT("Unknown");

	UCameraComponent* NewCam = FindBestCameraOnActor(NewVehicle);
	if (!NewCam)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] No camera on new vehicle '%s'"), *NewVehicle->GetName());
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
			UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Retarget: '%s' keeping RT, now on '%s'"),
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

	OnRetarget.Broadcast(Source, OldVehicleName, NewVehicle->GetName());
}

// ========================================================================
// NDI Media
// ========================================================================

void AStreamingSourceAttacher::StartNDICaptureForSource(FAttachedSourceInfo& Info, UTextureRenderTarget2D* InRT)
{
	if (!bOutputNDI || !NDIMediaOutputAsset || !InRT) 
	{
		if (!NDIMediaOutputAsset)
		{
			UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] NDIMediaOutputAsset is not set!"));
			LastNDIError = TEXT("NDIMediaOutputAsset is not set");
		}
		return;
	}

	if (Info.NDIMediaCapture)
	{
		Info.NDIMediaCapture->StopCapture(false);
		Info.NDIMediaCapture = nullptr;
	}

	Info.DynamicRenderTarget = InRT;

	if (AActor* SourceActor = Info.Source.Get())
	{
		if (USceneCaptureComponent2D* Capture = FindSceneCaptureOnActor(SourceActor))
		{
			Capture->TextureTarget = InRT;
			Capture->PostProcessSettings = NDIPostProcessSettings;
			Capture->PostProcessBlendWeight = NDIPostProcessBlendWeight;
		}
	}

	const FString UniqueNDIName = MakeNDIName(Info.StreamName);

	UNDIMediaOutput* ClonedOutput = DuplicateObject<UNDIMediaOutput>(NDIMediaOutputAsset, this);
	if (!ClonedOutput)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Error, TEXT("[SSA] Failed to clone NDI output asset"));
		}
		LastNDIError = TEXT("Failed to clone NDI output asset");
		NDIFailureCount++;
		return;
	}

	ClonedOutput->SourceName = UniqueNDIName;
	ClonedNDIOutputs.Add(ClonedOutput);

	UMediaCapture* NewCapture = ClonedOutput->CreateMediaCapture();
	if (!NewCapture)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] Failed to create NDIMediaCapture for stream '%s'."), *Info.StreamName);
		}
		LastNDIError = FString::Printf(TEXT("Failed to create NDIMediaCapture for stream '%s'"), *Info.StreamName);
		NDIFailureCount++;
		return;
	}

	FMediaCaptureOptions CaptureOptions;
	CaptureOptions.bSkipFrameWhenRunningExpensiveTasks = false;

	const bool bStarted = NewCapture->CaptureTextureRenderTarget2D(InRT, CaptureOptions);
	if (!bStarted)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogNDIforHPC, Warning, TEXT("[SSA] Failed to start NDI capture for stream '%s'."), *Info.StreamName);
		}
		LastNDIError = FString::Printf(TEXT("Failed to start NDI capture for stream '%s'"), *Info.StreamName);
		NewCapture->StopCapture(false);
		NDIFailureCount++;
		return;
	}

	Info.NDIMediaCapture = NewCapture;
	NDICaptureRenderTarget = InRT;
	NDIFailureCount = 0; // Reset on success

	if (bDebugLogging)
	{
		UE_LOG(LogNDIforHPC, Log, TEXT("[SSA] Started NDI capture '%s' on RT '%s' (%dx%d)."),
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

void AStreamingSourceAttacher::CleanupClonedOutputs()
{
	for (UNDIMediaOutput* Output : ClonedNDIOutputs)
	{
		if (Output)
		{
			Output->ConditionalRemoveFromRoot();
		}
	}
	ClonedNDIOutputs.Empty();
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
