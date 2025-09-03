// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Retargeter/IKRetargeter.h"

class UObject;
class UAnimSequence;
class USkeletalMesh;
class UIKRigDefinition;

/**
 * Main retargeter module class
 */
class FRetargeterModule : public IModuleInterface {
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static FRetargeterModule& Get();

    void SetPersistAssets(bool bInPersist);
    bool GetPersistAssets() const;

    void RetargetAPair(const FString& InputFbx, const FString& TargetFbx, const FString& OutputPath);
    
    // Optional: configure a uniform scale applied to bone translations during processing (headless-safe)
    void SetUniformScale(float InScale) { UniformScale = InScale; }

private:
    void RegisterMenus();
    void PluginButtonClicked();

    void ClearAssetsInPath(const FString& Path);
    void CleanPreviousOutputs();
    TArray<UObject*> ImportFBX(const FString& FbxPath, const FString& DestinationPath);
    void ProcessImportedAssets(const TArray<UObject*>& ImportedAssets, bool bIsInput);
    void LoadFBX(const FString& InputFbx, const FString& TargetFbx);

    // This requires the input skeleton:
    // - Has very standard names (no prefix/suffix)
    // - added names have "_added" 
    // - added are at the parent positions (hips -> spine_added_0 -> spine)
    // - Spine2 and Hips must exist
    TMap<FName, TPair<FName, FName>> GenerateRetargetChains(USkeletalMesh* Mesh);
    void CreateIkRig();

    void CreateRTG();

    void RetargetWithRTG();

    void ExportOutputAnimationFBX(const FString& OutputPath);

    static FRetargeterModule* SingletonInstance;

    bool bPersistAssets = false;

    UAnimSequence* InputAnimation;
    USkeletalMesh* InputSkeleton;
    USkeletalMesh* TargetSkeleton;

    UIKRigDefinition* InputIKRig = nullptr;
    UIKRigDefinition* TargetIKRig = nullptr;

    UIKRetargeter* IKRetargeter = nullptr;
    UAnimSequence* outputAnimation = nullptr;

    // Applied to bone translation values when generating poses (commandlet-safe alternative to FBX ImportUniformScale)
    float UniformScale = 1.0f;
};
