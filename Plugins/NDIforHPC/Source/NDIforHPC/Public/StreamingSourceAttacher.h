#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraComponent.h"
#include "Blueprint/UserWidget.h"

// NDIMedia
#include "MediaOutput.h"
#include "MediaCapture.h"
#include "NDIMediaOutput.h"
#include "NDIMediaCapture.h"

#include "StreamingSourceAttacher.generated.h"

class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UCameraComponent;
class FWidgetRenderer;

// Delegates for Blueprint events
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSourceAttachedSignature, AActor*, Source, FString, StreamName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSourceDetachedSignature, AActor*, Source, FString, StreamName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRetargetSignature, AActor*, Source, FString, OldVehicle, FString, NewVehicle);

// Per-attached source state
USTRUCT(BlueprintType)
struct FAttachedSourceInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Source;

	UPROPERTY()
	TWeakObjectPtr<AActor> Vehicle;

	UPROPERTY()
	FString StreamName;

	UPROPERTY()
	FTransform OriginalTransform;

	UPROPERTY()
	float TimeAttached = 0.f;

	// Runtime RT that the SceneCapture writes to (and NDI captures from)
	UPROPERTY()
	UTextureRenderTarget2D* DynamicRenderTarget = nullptr;

	// Editor persistence (WITH_EDITOR)
	UPROPERTY()
	UTextureRenderTarget2D* EditorRenderTarget = nullptr;

	UPROPERTY()
	FString EditorRenderTargetPath;

	// Cached camera driving the capture
	UPROPERTY()
	UCameraComponent* AttachedVehicleCamera = nullptr;

	// Per-source NDIMedia capture instance (created when NDI is enabled)
	UPROPERTY()
	UMediaCapture* NDIMediaCapture = nullptr;
};

UCLASS(BlueprintType, Blueprintable)
class AStreamingSourceAttacher : public AActor
{
	GENERATED_BODY()

public:
	AStreamingSourceAttacher();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ------------- Blueprint Control Functions -------------
	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Control")
	void AttachSourceManually(AActor* SourceActor, AActor* VehicleActor);

	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Control")
	void DetachSource(AActor* SourceActor);

	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Control")
	void DetachAllSources();

	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Control")
	void SetActive(bool bActive);

	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Control")
	void ForceRetarget();

	// ------------- Blueprint Query Functions -------------
	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Query")
	int32 GetAttachedSourceCount() const { return AttachedSources.Num(); }

	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Query")
	bool IsSourceAttached(AActor* SourceActor) const;

	UFUNCTION(BlueprintCallable, Category = "NDIforHPC|Query")
	FString GetStreamNameForSource(AActor* SourceActor) const;

	UFUNCTION(BlueprintPure, Category = "NDIforHPC|Query")
	bool IsActive() const { return bIsActive; }

	// ------------- Events -------------
	UPROPERTY(BlueprintAssignable, Category = "NDIforHPC|Events")
	FOnSourceAttachedSignature OnSourceAttached;

	UPROPERTY(BlueprintAssignable, Category = "NDIforHPC|Events")
	FOnSourceDetachedSignature OnSourceDetached;

	UPROPERTY(BlueprintAssignable, Category = "NDIforHPC|Events")
	FOnRetargetSignature OnRetarget;

	// ------------- Discovery -------------
	UPROPERTY(EditAnywhere, Category = "SSA|Discovery")
	FString VehicleNamePattern = TEXT("_Sandbox");

	UPROPERTY(EditAnywhere, Category = "SSA|Discovery")
	TSubclassOf<AActor> StreamingSourceClass;

	UPROPERTY(EditAnywhere, Category = "SSA|Discovery")
	bool bSkipStationaryVehicles = true;

	UPROPERTY(EditAnywhere, Category = "SSA|Discovery", meta = (ClampMin = "0.0"))
	float MinSpeedThresholdKmh = 1.8f;

	// ------------- Attach policy -------------
	UPROPERTY(EditAnywhere, Category = "SSA|Attach")
	bool bAttachToFirstVehicleOnly = false;

	UPROPERTY(EditAnywhere, Category = "SSA|Attach")
	bool bReturnToOriginalPosition = true;

	UPROPERTY(EditAnywhere, Category = "SSA|Attach", meta = (ClampMin = "1", ClampMax = "300"))
	int32 MaxCheckAttempts = 60;

	// ------------- Render target / resolution -------------
	UPROPERTY(EditAnywhere, Category = "SSA|RenderTarget")
	bool bOutputRenderTarget = true;

	UPROPERTY(EditAnywhere, Category = "SSA|RenderTarget", meta = (EditCondition = "bOutputRenderTarget"))
	bool bCreatePersistentRTAssets = false;

	UPROPERTY(EditAnywhere, Category = "SSA|RenderTarget", meta = (ClampMin = "16", ClampMax = "8192"))
	int32 RenderTargetWidth = 1280;

	UPROPERTY(EditAnywhere, Category = "SSA|RenderTarget", meta = (ClampMin = "16", ClampMax = "8192"))
	int32 RenderTargetHeight = 720;

	UPROPERTY(EditAnywhere, Category = "SSA|RenderTarget", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float ResolutionScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "SSA|RenderTarget")
	FString RenderTargetFolder = TEXT("/Game/Streaming/RenderTargets/");

	// ------------- NDI via NDIMedia -------------
	UPROPERTY(EditAnywhere, Category = "SSA|NDI")
	bool bOutputNDI = false;

	UPROPERTY(EditAnywhere, Category = "SSA|NDI")
	bool bAutoStartNDICapture = true;

	UPROPERTY(EditAnywhere, Category = "SSA|NDI")
	TObjectPtr<UNDIMediaOutput> NDIMediaOutputAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = "SSA|NDI")
	FString NDIPrefix = TEXT("UE_");

	// ------------- NDI-only PostProcess -------------
	UPROPERTY(EditAnywhere, Category = "SSA|NDI")
	FPostProcessSettings NDIPostProcessSettings;

	UPROPERTY(EditAnywhere, Category = "SSA|NDI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NDIPostProcessBlendWeight = 0.0f;

	// ------------- Camera / View -------------
	UPROPERTY(EditAnywhere, Category = "SSA|Camera")
	bool bOverridePlayerViewToVehicle = true;

	UPROPERTY(EditAnywhere, Category = "SSA|Camera")
	FVector StreamCameraOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "SSA|Camera")
	FRotator StreamCameraRotationOffset = FRotator::ZeroRotator;

	// ------------- Performance / Debug -------------
	UPROPERTY(EditAnywhere, Category = "SSA|Perf", meta = (ClampMin = "0.001"))
	float TickIntervalSeconds = 0.016f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SSA|Debug")
	bool bDebugLogging = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SSA|Debug")
	bool bShowDebugText = false;

	UPROPERTY(ReadOnly, Category = "SSA|Debug|Status")
	int32 CurrentAttachedCount = 0;

	UPROPERTY(ReadOnly, Category = "SSA|Debug|Status")
	bool bNDIActive = false;

	UPROPERTY(ReadOnly, Category = "SSA|Debug|Status")
	FString LastNDIError;

	// ------------- Rotation / Retargeting -------------
	UPROPERTY(EditAnywhere, Category = "SSA|Retarget")
	bool bRotationEnabled = false;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget", meta = (ClampMin = "0.1"))
	float MinRotationMinutes = 1.0f;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget", meta = (ClampMin = "0.1"))
	float MaxRotationMinutes = 3.0f;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget")
	FString RetargetNamePatternOverride;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget")
	bool bIncludeStationaryOnRetarget = false;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget", meta = (ClampMin = "0.0"))
	float CurrentVehicleMinSpeedForRetargetKmh = 1.8f;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget", meta = (ClampMin = "0.0"))
	float CandidateMinSpeedKmh = 10.0f;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget", meta = (ClampMin = "0.0"))
	float RetargetBelowSpeedGraceSeconds = 30.0f;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget")
	bool bForceRetargetOnBelowMinSpeed = true;

	UPROPERTY(EditAnywhere, Category = "SSA|Retarget")
	bool bKeepRenderTargetAcrossRetarget = true;

	// ------------- NDI HUD -------------
	UPROPERTY(EditAnywhere, Category = "SSA|NDI HUD")
	TSubclassOf<UUserWidget> StreamHUDClass;

private:
	UPROPERTY()
	TArray<FAttachedSourceInfo> AttachedSources;

	int32 AttemptCount = 0;
	bool bAttachmentComplete = false;
	bool bIsActive = true;

	// Rotation state
	TMap<TWeakObjectPtr<AActor>, float> RotationAccumSeconds;
	TMap<TWeakObjectPtr<AActor>, float> BelowSpeedTimeSeconds;

	// HUD
	UPROPERTY()
	UUserWidget* StreamHUDWidget = nullptr;

	TSharedPtr<FWidgetRenderer> StreamWidgetRenderer;

	UPROPERTY()
	UTextureRenderTarget2D* NDICaptureRenderTarget = nullptr;

	// Memory management - track cloned outputs for cleanup
	TArray<UNDIMediaOutput*> ClonedNDIOutputs;

	// Performance - caching
	TArray<AActor*> CachedVehicles;
	TArray<AActor*> CachedSources;
	float CacheRefreshTimer = 0.f;
	static const float CacheRefreshInterval = 1.0f;

	// NDI retry logic
	int32 NDIFailureCount = 0;
	static const int32 MaxNDIRetries = 3;
	float NDIRetryTimer = 0.f;
	static const float NDIRetryDelay = 2.0f;

	// Core flows
	void DiscoverAndAttach();
	bool TryAttachSourceToVehicle(AActor* SourceActor, AActor* VehicleActor, FAttachedSourceInfo& OutInfo);
	void DetachSourceInternal(FAttachedSourceInfo& Info);

	// Camera / capture helpers
	UCameraComponent* FindBestCameraOnActor(AActor* Actor) const;
	USceneCaptureComponent2D* FindSceneCaptureOnActor(AActor* Actor) const;
	void UpdateSceneCaptureForSource(FAttachedSourceInfo& Info, float DeltaSeconds);
	void ForcePlayerViewToCamera(UCameraComponent* VehicleCamera);

	// Naming / assets
	UTextureRenderTarget2D* CreatePersistentRenderTarget(const FString& RTBaseName, FString& OutAssetPath);
	void RegisterAssetInEditor(UObject* Asset);
	FString MakeStreamName(const AActor* Source, const AActor* Vehicle) const;
	FString MakeRTName(const FString& StreamName) const;
	FString MakeNDIName(const FString& StreamName) const;
	FString SanitizeStreamName(const FString& Name) const;

	// Retargeting helpers
	void GatherRetargetCandidates(TArray<AActor*>& OutVehicles) const;
	void RetargetOne(FAttachedSourceInfo& Info, AActor* NewVehicle);

	// NDIMedia helpers
	void StartNDICaptureForSource(FAttachedSourceInfo& Info, UTextureRenderTarget2D* InRT);
	void StopNDICaptureForSource(FAttachedSourceInfo& Info);
	void CleanupClonedOutputs();

	// HUD overlay
	void RenderHUDOverlay(float DeltaSeconds);

	// Performance
	void RefreshActorCache();

	// Utility: Convert cm/s to km/h
	FORCEINLINE float CmSToKmh(float SpeedInCmS) const
	{
		return SpeedInCmS * 0.036f;
	}

	// Utility: Convert km/h to cm/s
	FORCEINLINE float KmhToCmS(float SpeedInKmh) const
	{
		return SpeedInKmh * 27.7778f;
	}
};
