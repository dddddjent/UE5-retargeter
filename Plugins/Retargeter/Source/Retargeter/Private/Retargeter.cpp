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
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "ObjectTools.h"

#if WITH_EDITOR
// IKRig editor/public headers
#include "IKRig/Public/Rig/IKRigDefinition.h"
#include "IKRigEditor/Public/RigEditor/IKRigAutoCharacterizer.h"
#include "IKRigEditor/Public/RigEditor/IKRigAutoFBIK.h"
#include "IKRigEditor/Public/RigEditor/IKRigController.h"
#include "UObject/SavePackage.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "Retargeter/IKRetargeter.h"
#include "IKRigEditor/Public/RetargetEditor/IKRetargeterController.h"
#endif

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

bool FRetargeterModule::GetPersistAssets() const { return bPersistAssets; }

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
    FAssetRegistryModule& AssetRegistryModule
        = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
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
                UE_LOG(Retargeter, Warning, TEXT("Force deleting %d remaining assets under %s"),
                    ObjectsToForceDelete.Num(), *Path);
                const int32 NumForceDeleted
                    = ObjectTools::ForceDeleteObjects(ObjectsToForceDelete, /*ShowConfirmation=*/false);
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
                bIsInput ? TEXT("input") : TEXT("target"), bSaved ? TEXT("true") : TEXT("false"));
        }
    } else {
        UE_LOG(Retargeter, Verbose, TEXT("Skipping save of %s assets (in-memory mode)"),
            bIsInput ? TEXT("input") : TEXT("target"));
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
            if (!bIsInput)
                break; // For target, only need one
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
	createIkRig();
	createRTG();
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

void FRetargeterModule::createIkRig()
{
    UE_LOG(Retargeter, Log, TEXT("createIkRig called"));

    // Clear any existing generated rigs
    InputIKRig = nullptr;
    TargetIKRig = nullptr;

    // Need skeletons to operate
    if (!InputSkeleton && !TargetSkeleton) {
        UE_LOG(Retargeter, Warning, TEXT("No input or target skeleton available for IK rig generation"));
        return;
    }

    // Helper lambda to create and optionally save an IKRigDefinition for a skeletal mesh
    auto GenerateForMesh = [&](USkeletalMesh* Mesh, UIKRigDefinition*& OutIKRig, const FString& PackagePath) -> void {
        if (!Mesh)
            return;

        // Create transient IKRig asset
        FName AssetName = MakeUniqueObjectName(GetTransientPackage(), UIKRigDefinition::StaticClass(),
            FName("AutoIKRig"), EUniqueObjectNameOptions::GloballyUnique);
        OutIKRig = NewObject<UIKRigDefinition>(GetTransientPackage(), AssetName, RF_Public | RF_Standalone);

        const UIKRigController* Controller = UIKRigController::GetController(OutIKRig);
        if (!Controller) {
            UE_LOG(Retargeter, Error, TEXT("Failed to get UIKRigController"));
            return;
        }

        // Assign the skeletal mesh as preview/working mesh
        Controller->SetSkeletalMesh(Mesh);

        // Auto-generate retarget chains and FBIK
        FAutoCharacterizeResults CharacterizationResults;
        Controller->AutoGenerateRetargetDefinition(CharacterizationResults);
        Controller->SetRetargetDefinition(CharacterizationResults.AutoRetargetDefinition.RetargetDefinition);
        FAutoFBIKResults IKResults;
        Controller->AutoGenerateFBIK(IKResults);

        // Set preview mesh on the asset so editor shows it
        OutIKRig->SetPreviewMesh(Mesh);

        if (bPersistAssets) {
#if WITH_EDITOR
            // Create package and save asset under the requested path (/Game/...)
            FString LongPackageName = PackagePath + TEXT("/") + OutIKRig->GetName();
            FString PackageFileName
                = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());

            UPackage* Package = CreatePackage(*LongPackageName);
            if (Package) {
                OutIKRig->Rename(*OutIKRig->GetName(), Package);
                OutIKRig->SetFlags(RF_Public | RF_Standalone);
                OutIKRig->MarkPackageDirty();

                FSavePackageArgs SaveArgs;
                SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
                SaveArgs.SaveFlags = SAVE_None;
                SaveArgs.Error = GError;
                SaveArgs.bForceByteSwapping = false;
                SaveArgs.bWarnOfLongFilename = false;

                if (UPackage::SavePackage(Package, OutIKRig, *PackageFileName, SaveArgs)) {
                    UE_LOG(Retargeter, Log, TEXT("Saved IKRig asset to %s"), *PackageFileName);
                    // Update asset registry
                    FAssetRegistryModule::AssetCreated(OutIKRig);
                } else {
                    UE_LOG(Retargeter, Error, TEXT("Failed to save IKRig asset: %s"), *PackageFileName);
                }
            }
#else
            UE_LOG(Retargeter, Warning, TEXT("Persist requested but editor-only save not available in this build"));
#endif
        }
    };

    // Generate for both skeletons
    GenerateForMesh(InputSkeleton, InputIKRig, TEXT("/Game/Animations/tmp/input"));
    GenerateForMesh(TargetSkeleton, TargetIKRig, TEXT("/Game/Animations/tmp/target"));

    // Generated rigs are stored in InputIKRig and TargetIKRig members for later use
}

void FRetargeterModule::createRTG()
{
    UE_LOG(Retargeter, Log, TEXT("createRTG called"));

#if WITH_EDITOR
    if (!InputIKRig || !TargetIKRig) {
        UE_LOG(Retargeter, Warning, TEXT("createRTG: missing InputIKRig or TargetIKRig"));
        return;
    }

    // Build base name from input IKRig
    FString BaseName = InputIKRig->GetName();
    if (BaseName.StartsWith(TEXT("IK_"))) {
        BaseName = BaseName.RightChop(3);
    }

    FString UniqueAssetName = FString::Printf(TEXT("RTG_%s"), *BaseName);

    // Decide package path - keep consistent with IKRig saving path
    FString PackagePath = TEXT("/Game/Animations/tmp");
    FString DesiredPackage = PackagePath / UniqueAssetName;

    const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    FString UniquePackageName;
    AssetToolsModule.Get().CreateUniqueAssetName(DesiredPackage, TEXT(""), UniquePackageName, UniqueAssetName);
    if (UniquePackageName.EndsWith(UniqueAssetName)) {
        UniquePackageName = UniquePackageName.LeftChop(UniqueAssetName.Len() + 1);
    }

    UIKRetargeter* RetargetAsset = nullptr;

    if (bPersistAssets) {
        // Create via factory so it's a normal saved asset
        UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
        UObject* NewAsset = AssetToolsModule.Get().CreateAsset(*UniqueAssetName, *UniquePackageName, nullptr, Factory);
        RetargetAsset = Cast<UIKRetargeter>(NewAsset);
    } else {
        // Create transient retargeter
        FName AssetFName = MakeUniqueObjectName(GetTransientPackage(), UIKRetargeter::StaticClass(), FName(*UniqueAssetName));
        RetargetAsset = NewObject<UIKRetargeter>(GetTransientPackage(), AssetFName, RF_Public | RF_Standalone);
    }

    if (!RetargetAsset) {
        UE_LOG(Retargeter, Error, TEXT("Failed to create UIKRetargeter asset"));
        return;
    }

    // Use controller to assign IKRigs and setup default ops
    const UIKRetargeterController* Controller = UIKRetargeterController::GetController(RetargetAsset);
    if (!Controller) {
        UE_LOG(Retargeter, Error, TEXT("Failed to get UIKRetargeterController"));
        return;
    }

    Controller->SetIKRig(ERetargetSourceOrTarget::Source, InputIKRig);
    Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetIKRig);
    Controller->AddDefaultOps();

    // Set preview meshes via controller if available
    if (InputSkeleton) {
        Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, InputSkeleton);
    }
    if (TargetSkeleton) {
        Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, TargetSkeleton);
    }

    // Optionally set the input animation as a preview (if appropriate API exists)
    // UIKRetargeter doesn't directly store an animation reference for previewing in this code path.

    if (bPersistAssets) {
        // Save the package explicitly similar to createIkRig
        FString LongPackageName = UniquePackageName / UniqueAssetName;
        FString PackageFileName = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());

        UPackage* Package = CreatePackage(*LongPackageName);
        if (Package) {
            RetargetAsset->Rename(*RetargetAsset->GetName(), Package);
            RetargetAsset->SetFlags(RF_Public | RF_Standalone);
            RetargetAsset->MarkPackageDirty();

            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.SaveFlags = SAVE_None;
            SaveArgs.Error = GError;
            SaveArgs.bForceByteSwapping = false;
            SaveArgs.bWarnOfLongFilename = false;

            if (UPackage::SavePackage(Package, RetargetAsset, *PackageFileName, SaveArgs)) {
                UE_LOG(Retargeter, Log, TEXT("Saved IK Retargeter asset to %s"), *PackageFileName);
                FAssetRegistryModule::AssetCreated(RetargetAsset);
            } else {
                UE_LOG(Retargeter, Error, TEXT("Failed to save IK Retargeter asset: %s"), *PackageFileName);
            }
        }
    } else {
        UE_LOG(Retargeter, Verbose, TEXT("Created transient IK Retargeter: %s"), *RetargetAsset->GetName());
    }
#else
    UE_LOG(Retargeter, Warning, TEXT("createRTG is editor-only and not available in this build"));
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRetargeterModule, Retargeter)