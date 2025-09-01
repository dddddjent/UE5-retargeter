// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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
    TArray<UObject*> ImportFBX(const FString& FbxPath, const FString& DestinationPath);
    void ProcessImportedAssets(const TArray<UObject*>& ImportedAssets, bool bIsInput);
    void loadFBX(const FString& InputFbx, const FString& TargetFbx);

    void createIkRig();

    static FRetargeterModule* SingletonInstance;

    bool bPersistAssets = false;

    UAnimSequence* InputAnimation;
    USkeletalMesh* InputSkeleton;
    USkeletalMesh* TargetSkeleton;
    // Generated IKRig assets (transient or saved depending on persist flag)
    UIKRigDefinition* InputIKRig = nullptr;
    UIKRigDefinition* TargetIKRig = nullptr;
};
