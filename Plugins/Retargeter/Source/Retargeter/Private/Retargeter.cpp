// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter.h"
#include "Logging/LogMacros.h"
// Editor-only includes
#if WITH_EDITOR
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#endif
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Asset import includes
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Level.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "FRetargeterModule"

DEFINE_LOG_CATEGORY_STATIC(Retargeter, Log, All);

// Static singleton instance
FRetargeterModule* FRetargeterModule::SingletonInstance = nullptr;

FRetargeterModule& FRetargeterModule::Get()
{
    check(SingletonInstance);
    return *SingletonInstance;
}

void FRetargeterModule::SetPersistAssets(bool bInPersist)
{
    bPersistAssets = bInPersist;
    UE_LOG(Retargeter, Log, TEXT("PersistAssets=%s"), bPersistAssets ? TEXT("true") : TEXT("false"));
}

bool FRetargeterModule::GetPersistAssets() const
{
    return bPersistAssets;
}

void FRetargeterModule::StartupModule()
{
    SingletonInstance = this;

    InputAnimation = nullptr;
    InputSkeleton = nullptr;
    TargetSkeleton = nullptr;

#if WITH_EDITOR
    // Register editor menus on startup (editor only)
    if (UToolMenus::IsToolMenuUIEnabled()) {
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FRetargeterModule::RegisterMenus));
    }
#endif
}

void FRetargeterModule::ShutdownModule()
{
#if WITH_EDITOR
    if (UToolMenus::IsToolMenuUIEnabled()) {
        UToolMenus::UnRegisterStartupCallback(this);
    }
#endif

    // Clear singleton instance
    SingletonInstance = nullptr;
}

#if WITH_EDITOR
void FRetargeterModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    // Add to main menu under Window->Retargeter
    if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window")) {
        FToolMenuSection& Section = Menu->AddSection("RetargeterSection", LOCTEXT("RetargeterHeading", "Retargeter"));
        Section.AddMenuEntry("Retargeter_Run", LOCTEXT("RetargeterMenuLabel", "Run Retargeter"),
            LOCTEXT("RetargeterMenuTooltip", "Run the Retargeter plugin action."), FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FRetargeterModule::PluginButtonClicked)));
    }

    // Add to Level Editor toolbar - Using the working approach
    if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.ModesToolBar")) {
        FToolMenuSection& Section = Toolbar->FindOrAddSection("File");
        Section.AddEntry(FToolMenuEntry::InitToolBarButton("Retargeter_ToolbarButton",
            FExecuteAction::CreateRaw(this, &FRetargeterModule::PluginButtonClicked),
            LOCTEXT("RetargeterToolbarLabel", "Retarget"),
            LOCTEXT("RetargeterToolbarTooltip", "Run the Retargeter plugin action."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Play")));
    }
}

void FRetargeterModule::PluginButtonClicked()
{
    FText Title = LOCTEXT("RetargeterActionTitle", "Retargeter");
    FText Message
        = LOCTEXT("RetargeterActionMessage", "Retargeter button clicked. Use commandlet for batch processing.");
    FMessageDialog::Open(EAppMsgType::Ok, Message, Title);
}
#endif // WITH_EDITOR

void FRetargeterModule::ClearAssetsInPath(const FString& Path)
{
    if (!bPersistAssets) {
        UE_LOG(Retargeter, Verbose, TEXT("Skipping clear of %s (in-memory mode)"), *Path);
        return;
    }
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> AssetsToDelete;
    AssetRegistryModule.Get().ScanPathsSynchronous({ Path }, /*bForceRescan*/ true);
    AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), AssetsToDelete, true);
    if (AssetsToDelete.Num() > 0) {
        UE_LOG(Retargeter, Log, TEXT("Deleting %d assets under %s (non-interactive)"), AssetsToDelete.Num(), *Path);
        const int32 NumDeleted = ObjectTools::DeleteAssets(AssetsToDelete, /*bShowConfirmation=*/false);
        UE_LOG(Retargeter, Log, TEXT("DeleteAssets removed %d assets from %s"), NumDeleted, *Path);

        // Fallback: some assets may remain (e.g., references prevent standard delete). Try force delete in commandlets.
        AssetsToDelete.Reset();
        AssetRegistryModule.Get().ScanPathsSynchronous({ Path }, /*bForceRescan*/ true);
        AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), AssetsToDelete, true);
        if (AssetsToDelete.Num() > 0 && IsRunningCommandlet()) {
            TArray<UObject*> ObjectsToForceDelete;
            ObjectsToForceDelete.Reserve(AssetsToDelete.Num());
            for (const FAssetData& AssetData : AssetsToDelete) {
                if (UObject* Obj = AssetData.GetAsset({ ULevel::LoadAllExternalObjectsTag })) {
                    ObjectsToForceDelete.Add(Obj);
                }
            }
            if (ObjectsToForceDelete.Num() > 0) {
                UE_LOG(Retargeter, Warning, TEXT("Force deleting %d remaining assets under %s"), ObjectsToForceDelete.Num(), *Path);
                const int32 NumForceDeleted = ObjectTools::ForceDeleteObjects(ObjectsToForceDelete, /*ShowConfirmation=*/false);
                UE_LOG(Retargeter, Log, TEXT("ForceDeleteObjects removed %d assets from %s"), NumForceDeleted, *Path);
            }
        }
    }
}

TArray<UObject*> FRetargeterModule::ImportFBX(const FString& FbxPath, const FString& DestinationPath)
{
    FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->Filenames.Add(FbxPath);
    ImportData->DestinationPath = DestinationPath;
    ImportData->bReplaceExisting = true;

    TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);
    UE_LOG(Retargeter, Log, TEXT("Imported assets: %d to %s"), ImportedAssets.Num(), *DestinationPath);
    return ImportedAssets;
}

void FRetargeterModule::ProcessImportedAssets(const TArray<UObject*>& ImportedAssets, bool bIsInput)
{
    // Mark packages as dirty so they get saved
    for (UObject* Asset : ImportedAssets) {
        if (Asset && Asset->GetPackage()) {
            Asset->GetPackage()->MarkPackageDirty();
        }
    }

#if WITH_EDITOR
    if (bPersistAssets) {
        // Collect packages to save and save them non-interactively
        TSet<UPackage*> UniquePackagesToSave;
        for (UObject* Asset : ImportedAssets) {
            if (UPackage* Package = Asset->GetOutermost())
                UniquePackagesToSave.Add(Package);
        }

        if (UniquePackagesToSave.Num() > 0) {
            TArray<UPackage*> PackagesToSave = UniquePackagesToSave.Array();
            const bool bOnlyDirty = true;
            const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, bOnlyDirty);
            UE_LOG(Retargeter, Log, TEXT("Saved %d %s packages (result=%s)"), PackagesToSave.Num(),
                bIsInput ? TEXT("input") : TEXT("target"),
                bSaved ? TEXT("true") : TEXT("false"));
        }
    } else {
        UE_LOG(Retargeter, Verbose, TEXT("Skipping save of %s assets (in-memory mode)"), bIsInput ? TEXT("input") : TEXT("target"));
    }
#endif

    // Find the assets
    for (UObject* Asset : ImportedAssets) {
        if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset)) {
            if (bIsInput) {
                InputSkeleton = SkeletalMesh;
                UE_LOG(Retargeter, Log, TEXT("Imported input skeletal mesh: %s"), *SkeletalMesh->GetName());
            } else {
                TargetSkeleton = SkeletalMesh;
                UE_LOG(Retargeter, Log, TEXT("Imported target skeletal mesh: %s"), *SkeletalMesh->GetName());
            }
            if (!bIsInput) break; // For target, only need one
        } else if (bIsInput && Cast<UAnimSequence>(Asset)) {
            UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset);
            InputAnimation = AnimSeq;
            UE_LOG(Retargeter, Log, TEXT("Imported input animation: %s"), *AnimSeq->GetName());
        }
    }
}

void FRetargeterModule::RetargetAPair(const FString& InputFbx, const FString& TargetFbx, const FString& OutputPath)
{
    loadFBX(InputFbx, TargetFbx);
}

void FRetargeterModule::loadFBX(const FString& InputFbx, const FString& TargetFbx)
{
    UE_LOG(Retargeter, Log, TEXT("loadFBX called with Input: %s, Target: %s"), *InputFbx, *TargetFbx);

    // Clear existing assets
    ClearAssetsInPath(TEXT("/Game/Animations/tmp/input"));
    ClearAssetsInPath(TEXT("/Game/Animations/tmp/target"));

    // Import input FBX
    TArray<UObject*> InputAssets = ImportFBX(InputFbx, TEXT("/Game/Animations/tmp/input"));
    ProcessImportedAssets(InputAssets, true);

    // Import target FBX
    TArray<UObject*> TargetAssets = ImportFBX(TargetFbx, TEXT("/Game/Animations/tmp/target"));
    ProcessImportedAssets(TargetAssets, false);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRetargeterModule, Retargeter)