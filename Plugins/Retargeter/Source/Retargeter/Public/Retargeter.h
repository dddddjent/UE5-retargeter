// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

private:
    void RegisterMenus();
    void PluginButtonClicked();

    void ClearAssetsInPath(const FString& Path);
    void CleanPreviousOutputs();
    TArray<UObject*> ImportFBX(const FString& FbxPath, const FString& DestinationPath);
    void ProcessImportedAssets(const TArray<UObject*>& ImportedAssets, bool bIsInput);
    void LoadFBX(const FString& InputFbx, const FString& TargetFbx);

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
};
