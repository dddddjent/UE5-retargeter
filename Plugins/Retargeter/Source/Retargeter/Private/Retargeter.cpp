// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Misc/CoreMiscDefines.h"
#include "ReferenceSkeleton.h"
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
#include "AnimPose.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetExportTask.h"
#include "Exporters/AnimSequenceExporterFBX.h"
#include "Exporters/FbxExportOption.h"
#include "HAL/FileManager.h"
#include "IKRig/Public/Rig/IKRigDefinition.h"
#include "IKRigEditor/Public/RetargetEditor/IKRetargeterController.h"
#include "IKRigEditor/Public/RigEditor/IKRigAutoFBIK.h"
#include "IKRigEditor/Public/RigEditor/IKRigController.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"
#include "UObject/SavePackage.h"
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

void FRetargeterModule::RetargetWithRTG()
{
    // Validate inputs
    if (!InputAnimation || !InputSkeleton || !TargetSkeleton || !IKRetargeter) {
        UE_LOG(Retargeter, Warning, TEXT("retargetWithRTG: missing input(s). Anim=%p InMesh=%p TgtMesh=%p RTG=%p"),
            InputAnimation, InputSkeleton, TargetSkeleton, IKRetargeter);
        return;
    }

#if WITH_EDITOR
    // Initialize processor with chain retargeting profile from asset
    FIKRetargetProcessor Processor;
    FRetargetProfile RetargetProfile;
    RetargetProfile.FillProfileWithAssetSettings(IKRetargeter);

    Processor.Initialize(InputSkeleton, TargetSkeleton, IKRetargeter, RetargetProfile);
    if (!Processor.IsInitialized()) {
        UE_LOG(Retargeter, Error, TEXT("retargetWithRTG: Failed to initialize IK Retargeter processor"));
        return;
    }

    // Gather skeleton info
    const FRetargetSkeleton& TargetRig = Processor.GetSkeleton(ERetargetSourceOrTarget::Target);
    const TArray<FName>& TargetBoneNames = TargetRig.BoneNames;
    const int32 NumTargetBones = TargetBoneNames.Num();

    const FRetargetSkeleton& SourceRig = Processor.GetSkeleton(ERetargetSourceOrTarget::Source);
    const TArray<FName>& SourceBoneNames = SourceRig.BoneNames;
    const int32 NumSourceBones = SourceBoneNames.Num();

    // Allocate source pose buffer
    TArray<FTransform> SourceComponentPose;
    SourceComponentPose.SetNum(NumSourceBones);

    // Create output sequence by duplicating the source sequence (keeps data model intact like editor duplication)
    UAnimSequence* TargetSequence = nullptr;
    {
        FString OutName = FString::Printf(TEXT("%s_RTG"), *InputAnimation->GetName());
        if (bPersistAssets && InputAnimation->GetOutermost()) {
            // Duplicate into /Game/Animations/tmp
            const FString DesiredPath = TEXT("/Game/Animations/tmp");
            FString UniquePkgName, UniqueAssetName;
            const FAssetToolsModule& AssetToolsModule
                = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
            AssetToolsModule.Get().CreateUniqueAssetName(
                DesiredPath / OutName, TEXT(""), UniquePkgName, UniqueAssetName);

            UPackage* Package = CreatePackage(*UniquePkgName);
            TargetSequence = DuplicateObject<UAnimSequence>(InputAnimation, Package, *UniqueAssetName);
            if (TargetSequence) {
                TargetSequence->SetFlags(RF_Public | RF_Standalone);
                TargetSequence->SetSkeleton(TargetSkeleton->GetSkeleton());
                TargetSequence->SetPreviewMesh(TargetSkeleton);
                TargetSequence->MarkPackageDirty();
            }
        } else {
            // Transient duplicate
            FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), *OutName);
            TargetSequence = DuplicateObject<UAnimSequence>(InputAnimation, GetTransientPackage(), UniqueName);
            if (TargetSequence) {
                TargetSequence->SetSkeleton(TargetSkeleton->GetSkeleton());
                TargetSequence->SetPreviewMesh(TargetSkeleton);
            }
        }
    }

    if (!TargetSequence) {
        UE_LOG(Retargeter, Error, TEXT("retargetWithRTG: Failed to create output UAnimSequence"));
        return;
    }
    // Do not assign to outputAnimation yet; we'll create a copy after baking data

    // Prepare controller for writing keys
    IAnimationDataController& Ctrl = TargetSequence->GetController();
    constexpr bool bTransact = false;
    Ctrl.OpenBracket(FText::FromString("Generating Retargeted Animation Data"), bTransact);
    Ctrl.NotifyPopulated();
    Ctrl.UpdateWithSkeleton(TargetSkeleton->GetSkeleton(), bTransact);
    // For duplicated sequences, the model is already initialized; still ensure frame rate and length are consistent
    const IAnimationDataModel* SrcModel = InputAnimation->GetDataModel();
    const FFrameRate SrcFrameRate = SrcModel->GetFrameRate();
    Ctrl.SetFrameRate(SrcFrameRate, bTransact);
    const int32 NumFrames = SrcModel->GetNumberOfFrames();
    Ctrl.SetNumberOfFrames(NumFrames, bTransact);

    // Determine number of frames and pre-allocate tracks
    TArray<FRawAnimSequenceTrack> BoneTracks;
    BoneTracks.SetNumZeroed(NumTargetBones);
    for (int32 BoneIndex = 0; BoneIndex < NumTargetBones; ++BoneIndex) {
        BoneTracks[BoneIndex].PosKeys.SetNum(NumFrames);
        BoneTracks[BoneIndex].RotKeys.SetNum(NumFrames);
        BoneTracks[BoneIndex].ScaleKeys.SetNum(NumFrames);
    }

    // Evaluate options to match editor behavior
    FAnimPoseEvaluationOptions EvalOptions;
    EvalOptions.OptionalSkeletalMesh = const_cast<USkeletalMesh*>(SourceRig.SkeletalMesh);
    EvalOptions.bExtractRootMotion = false;
    EvalOptions.bIncorporateRootMotionIntoPose = true;

    // Reset playback of ops
    Processor.OnPlaybackReset();

    // Iterate frames and retarget
    for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex) {
        // Source pose at this frame
        FAnimPose SourcePose;
        UAnimPoseExtensions::GetAnimPoseAtFrame(InputAnimation, FrameIndex, EvalOptions, SourcePose);

        for (int32 SIndex = 0; SIndex < NumSourceBones; ++SIndex) {
            const FName& BoneName = SourceBoneNames[SIndex];
            SourceComponentPose[SIndex]
                = UAnimPoseExtensions::GetBonePose(SourcePose, BoneName, EAnimPoseSpaces::World);
        }
        // Headless-safe uniform scale: apply translation scale and reset scale to identity
        if (!FMath::IsNearlyEqual(UniformScale, 1.0f)) {
            for (FTransform& Xform : SourceComponentPose) {
                Xform.SetLocation(Xform.GetLocation() * UniformScale);
                Xform.SetScale3D(FVector::OneVector);
            }
        } else {
            for (FTransform& Xform : SourceComponentPose) {
                Xform.SetScale3D(FVector::OneVector);
            }
        }

        const float TimeAtFrame = InputAnimation->GetTimeAtFrame(FrameIndex);
        float DeltaTime = (FrameIndex > 0) ? TimeAtFrame - InputAnimation->GetTimeAtFrame(FrameIndex - 1) : TimeAtFrame;

        // Settings profile per frame
        FRetargetProfile SettingsProfile;
        SettingsProfile.FillProfileWithAssetSettings(IKRetargeter);

        // Allow processor to scale if needed
        Processor.ScaleSourcePose(SourceComponentPose);

        // Run retargeter (chain retargeting)
        const TArray<FTransform>& TargetComponentPose
            = Processor.RunRetargeter(SourceComponentPose, SettingsProfile, DeltaTime);

        // Convert to local
        TArray<FTransform> TargetLocalPose = TargetComponentPose;
        TargetRig.UpdateLocalTransformsBelowBone(0, TargetLocalPose, TargetComponentPose);

        // Write keys for each bone
        for (int32 TBoneIndex = 0; TBoneIndex < NumTargetBones; ++TBoneIndex) {
            const FTransform& Local = TargetLocalPose[TBoneIndex];
            FRawAnimSequenceTrack& Track = BoneTracks[TBoneIndex];
            FVector3f Pos = FVector3f(Local.GetLocation());
            if (!FMath::IsNearlyEqual(UniformScale, 1.0f)) {
                Pos *= static_cast<float>(UniformScale);
            }
            Track.PosKeys[FrameIndex] = Pos;
            Track.RotKeys[FrameIndex] = FQuat4f(Local.GetRotation().GetNormalized());
            Track.ScaleKeys[FrameIndex] = FVector3f(Local.GetScale3D());
        }
    }

    // Commit tracks: add only if missing to avoid "track already exists" warnings
    {
        TArray<FName> ExistingTrackNames;
        if (const IAnimationDataModel* Model = Ctrl.GetModel()) {
            Model->GetBoneTrackNames(ExistingTrackNames);
        }
        TSet<FName> ExistingTrackSet(ExistingTrackNames);

        for (int32 TBoneIndex = 0; TBoneIndex < NumTargetBones; ++TBoneIndex) {
            const FName& BoneName = TargetBoneNames[TBoneIndex];
            const FRawAnimSequenceTrack& Raw = BoneTracks[TBoneIndex];
            if (!ExistingTrackSet.Contains(BoneName)) {
                Ctrl.AddBoneCurve(BoneName, bTransact);
            }
            Ctrl.SetBoneTrackKeys(BoneName, Raw.PosKeys, Raw.RotKeys, Raw.ScaleKeys, bTransact);
        }
    }

    Ctrl.CloseBracket(bTransact);

    // Mark and save if requested
    TargetSequence->PostEditChange();
    TargetSequence->MarkPackageDirty();

    if (bPersistAssets) {
        if (UPackage* Pkg = TargetSequence->GetOutermost()) {
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.SaveFlags = SAVE_None;
            SaveArgs.Error = GError;
            FString PackageFilename
                = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
            UPackage::SavePackage(Pkg, TargetSequence, *PackageFilename, SaveArgs);
        }
    }

    // Always create a transient copy and store in outputAnimation
    {
        const FString CopyBaseName = FString::Printf(TEXT("%s_OutputCopy"), *TargetSequence->GetName());
        const FName CopyName
            = MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), FName(*CopyBaseName));
        UAnimSequence* CopySeq = DuplicateObject<UAnimSequence>(TargetSequence, GetTransientPackage(), CopyName);
        if (CopySeq) {
            CopySeq->SetSkeleton(TargetSkeleton->GetSkeleton());
            CopySeq->SetPreviewMesh(TargetSkeleton);
            outputAnimation = CopySeq;
        } else {
            // Fallback to original if duplication failed
            outputAnimation = TargetSequence;
        }
    }

    UE_LOG(Retargeter, Log, TEXT("retargetWithRTG: Completed retargeting to output sequence %s"),
        *TargetSequence->GetName());
#else
    UE_LOG(Retargeter, Warning, TEXT("retargetWithRTG: Editor-only retargeting is not available in this build"));
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
                UE_LOG(Retargeter, Log, TEXT("Force deleting %d remaining assets under %s"), ObjectsToForceDelete.Num(),
                    *Path);
                const int32 NumForceDeleted
                    = ObjectTools::ForceDeleteObjects(ObjectsToForceDelete, /*ShowConfirmation=*/false);
                UE_LOG(Retargeter, Log, TEXT("ForceDeleteObjects removed %d assets from %s"), NumForceDeleted, *Path);
            }
        }
    }
}

void FRetargeterModule::CleanPreviousOutputs()
{
    // Clean previously generated transient/persistent outputs under our temp folder.
    // Keep input/target subfolders intact; only clear assets directly under /Game/Animations/tmp.
    const FString RootOutputPath = TEXT("/Game/Animations/tmp");

    FAssetRegistryModule& AssetRegistryModule
        = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    // Force a rescan so we see latest on-disk state
    AssetRegistryModule.Get().ScanPathsSynchronous({ RootOutputPath }, /*bForceRescan*/ true);

    TArray<FAssetData> AllAssetsUnderRoot;
    AssetRegistryModule.Get().GetAssetsByPath(FName(*RootOutputPath), AllAssetsUnderRoot, /*bRecursive*/ true);

    if (AllAssetsUnderRoot.Num() == 0) {
        UE_LOG(Retargeter, Verbose, TEXT("CleanPreviousOutputs: nothing to delete under %s"), *RootOutputPath);
        return;
    }

    TArray<FAssetData> AssetsToDelete;
    AssetsToDelete.Reserve(AllAssetsUnderRoot.Num());

    // Only delete assets whose package path is exactly the root (exclude input/target subfolders)
    const FName RootPathFName(*RootOutputPath);
    for (const FAssetData& AssetData : AllAssetsUnderRoot) {
        if (AssetData.PackagePath == RootPathFName) {
            AssetsToDelete.Add(AssetData);
        }
    }

    if (AssetsToDelete.Num() == 0) {
        UE_LOG(Retargeter, Verbose, TEXT("CleanPreviousOutputs: no top-level outputs under %s"), *RootOutputPath);
        return;
    }

    UE_LOG(Retargeter, Log, TEXT("CleanPreviousOutputs: deleting %d assets under %s"), AssetsToDelete.Num(),
        *RootOutputPath);
    const int32 NumDeleted = ObjectTools::DeleteAssets(AssetsToDelete, /*bShowConfirmation=*/false);
    UE_LOG(Retargeter, Log, TEXT("CleanPreviousOutputs: DeleteAssets removed %d assets"), NumDeleted);

    // In commandlet mode, try to force-delete any remaining assets
    if (IsRunningCommandlet()) {
        TArray<FAssetData> Remaining;
        AssetRegistryModule.Get().ScanPathsSynchronous({ RootOutputPath }, /*bForceRescan*/ true);
        AssetRegistryModule.Get().GetAssetsByPath(RootPathFName, Remaining, /*bRecursive*/ false);
        if (Remaining.Num() > 0) {
            TArray<UObject*> ObjectsToForceDelete;
            for (const FAssetData& AssetData : Remaining) {
                if (UObject* Obj = AssetData.GetAsset({ ULevel::LoadAllExternalObjectsTag })) {
                    ObjectsToForceDelete.Add(Obj);
                }
            }
            if (ObjectsToForceDelete.Num() > 0) {
                const int32 NumForceDeleted
                    = ObjectTools::ForceDeleteObjects(ObjectsToForceDelete, /*ShowConfirmation=*/false);
                UE_LOG(Retargeter, Log, TEXT("CleanPreviousOutputs: ForceDeleteObjects removed %d assets"),
                    NumForceDeleted);
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
    // Delete any previous retargeted outputs first to avoid dangling references
    // to assets from a prior target skeleton when switching FBX files.
    CleanPreviousOutputs();

    // Apply default uniform scale in commandlet mode (headless-safe), matches 0.01 import offset
    if (IsRunningCommandlet()) {
        UniformScale = 0.01f;
    }

    LoadFBX(InputFbx, TargetFbx);
    CreateIkRig();
    CreateRTG();
    RetargetWithRTG();
    ExportOutputAnimationFBX(OutputPath);

    // Release references to created/imported assets so they can be garbage collected
    // Clearing member pointers avoids holding onto transient or editor-only assets
    InputAnimation = nullptr;
    InputSkeleton = nullptr;
    TargetSkeleton = nullptr;

    InputIKRig = nullptr;
    TargetIKRig = nullptr;

    IKRetargeter = nullptr;
    outputAnimation = nullptr;

    // In commandlet/batch mode, run a GC pass to free transient assets immediately.
    if (IsRunningCommandlet()) {
        UE_LOG(Retargeter, Log, TEXT("RetargetAPair: running garbage collection to free transient assets"));
        CollectGarbage(RF_NoFlags);
    }
}

void FRetargeterModule::LoadFBX(const FString& InputFbx, const FString& TargetFbx)
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

TMap<FName, TPair<FName, FName>> FRetargeterModule::GenerateRetargetChains(USkeletalMesh* Mesh)
{
    // Chain name, start bone, end bone
    TMap<FName, TPair<FName, FName>> Chains;
    const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
    const int32 NumBones = RefSkeleton.GetNum();

    TMap<FString, int32> BoneNameIndexMap;
    TMap<int32, FString> IndexChainNameMap;
    for (int32 i = 0; i < NumBones; ++i) {
        const FName BoneFName = RefSkeleton.GetBoneName(i);
        FString BoneLower = BoneFName.ToString().ToLower();
        if (BoneLower.Contains(TEXT("_added")))
            continue;

        BoneNameIndexMap.Add(BoneLower, i);
    }

    // For those that are not in the map, add them with INDEX_NONE
    auto FindOrAddBoneIdx = [&BoneNameIndexMap, &IndexChainNameMap](const TCHAR* InName) -> int32 {
        const FString Key = FString(InName).ToLower();
        if (int32* Ptr = BoneNameIndexMap.Find(Key)) {
            IndexChainNameMap.Add(*Ptr, Key);
            return *Ptr;
        }
        BoneNameIndexMap.Add(Key, INDEX_NONE);
        return INDEX_NONE;
    };

    int32 HipsIdx = BoneNameIndexMap.FindRef(TEXT("hips"));
    int32 Spine2Idx = BoneNameIndexMap.FindRef(TEXT("spine2"));

    int32 SpineIdx = FindOrAddBoneIdx(TEXT("spine"));
    int32 NeckIdx = FindOrAddBoneIdx(TEXT("neck"));
    int32 HeadIdx = FindOrAddBoneIdx(TEXT("head"));
    int32 LeftShoulderIdx = FindOrAddBoneIdx(TEXT("leftshoulder"));
    int32 RightShoulderIdx = FindOrAddBoneIdx(TEXT("rightshoulder"));
    int32 LeftArmIdx = FindOrAddBoneIdx(TEXT("leftarm"));
    int32 LeftForeArmIdx = FindOrAddBoneIdx(TEXT("leftforearm"));
    int32 RightArmIdx = FindOrAddBoneIdx(TEXT("rightarm"));
    int32 RightForeArmIdx = FindOrAddBoneIdx(TEXT("rightforearm"));
    int32 LeftUpLegIdx = FindOrAddBoneIdx(TEXT("leftupleg"));
    int32 LeftLegIdx = FindOrAddBoneIdx(TEXT("leftleg"));
    int32 RightUpLegIdx = FindOrAddBoneIdx(TEXT("rightupleg"));
    int32 RightLegIdx = FindOrAddBoneIdx(TEXT("rightleg"));

    TArray<int32> Ends, Tmp;
    Ends.Reset(6);
    Ends.Add(Spine2Idx);
    for (int32 i = 0; i < NumBones; ++i) {
        RefSkeleton.GetDirectChildBones(i, Tmp);
        if (Tmp.Num() == 0)
            Ends.Add(i);
    }
    check(Ends.Num() == 6);

    for (int32 i = 0; i < Ends.Num(); ++i) {
        int32 CurrIdx = Ends[i];
        int32 ChainStartIdx = Ends[i];
        while (CurrIdx != INDEX_NONE && CurrIdx != HipsIdx) {
            if (IndexChainNameMap.Contains(CurrIdx)) {
                UE_LOG(Retargeter, Log, TEXT("CurrIdx: %d, ChainStartIdx: %d"), CurrIdx, ChainStartIdx);
                UE_LOG(Retargeter, Log, TEXT("CurrName: %s, ChainStartName: %s"),
                    *RefSkeleton.GetBoneName(CurrIdx).ToString(), *RefSkeleton.GetBoneName(ChainStartIdx).ToString());
                Chains.Add(FName(*IndexChainNameMap[CurrIdx]),
                    TPair<FName, FName>(*RefSkeleton.GetBoneName(CurrIdx).ToString(),
                        *RefSkeleton.GetBoneName(ChainStartIdx).ToString()));
                ChainStartIdx = RefSkeleton.GetParentIndex(CurrIdx);
            }
            CurrIdx = RefSkeleton.GetParentIndex(CurrIdx);
            if (CurrIdx == Spine2Idx)
                break;
        }
    }

    return Chains;
}

void FRetargeterModule::CreateIkRig()
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
        // FAutoCharacterizeResults CharacterizationResults;
        // Controller->AutoGenerateRetargetDefinition(CharacterizationResults);
        // Controller->SetRetargetDefinition(CharacterizationResults.AutoRetargetDefinition.RetargetDefinition);

        FRetargetDefinition RetargetDef;
        const auto Chains = GenerateRetargetChains(Mesh);
        for (auto& Pair : Chains) {
            RetargetDef.AddBoneChain(Pair.Key, Pair.Value.Key, Pair.Value.Value);
        }
        RetargetDef.RootBone = FName("Hips");
        Controller->SetRetargetDefinition(RetargetDef);
        Controller->SetRetargetRoot(FName("Hips"));

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

void FRetargeterModule::CreateRTG()
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
        FName AssetFName
            = MakeUniqueObjectName(GetTransientPackage(), UIKRetargeter::StaticClass(), FName(*UniqueAssetName));
        RetargetAsset = NewObject<UIKRetargeter>(GetTransientPackage(), AssetFName, RF_Public | RF_Standalone);
    }

    if (!RetargetAsset) {
        UE_LOG(Retargeter, Error, TEXT("Failed to create UIKRetargeter asset"));
        return;
    }
    IKRetargeter = RetargetAsset;

    // Use controller to assign IKRigs and setup default ops
    const UIKRetargeterController* Controller = UIKRetargeterController::GetController(RetargetAsset);
    if (!Controller) {
        UE_LOG(Retargeter, Error, TEXT("Failed to get UIKRetargeterController"));
        return;
    }

    Controller->SetIKRig(ERetargetSourceOrTarget::Source, InputIKRig);
    Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetIKRig);
    Controller->AddDefaultOps();

    // Disable ops not desired: Run IK Rig and Retarget IK Goals
    if (FIKRetargetRunIKRigOp* RunIKOp = RetargetAsset->GetFirstRetargetOpOfType<FIKRetargetRunIKRigOp>()) {
        RunIKOp->SetEnabled(false);
    }
    if (FIKRetargetIKChainsOp* IKGoalsOp = RetargetAsset->GetFirstRetargetOpOfType<FIKRetargetIKChainsOp>()) {
        IKGoalsOp->SetEnabled(false);
    }

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
        FString PackageFileName
            = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());

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

void FRetargeterModule::ExportOutputAnimationFBX(const FString& OutputPath)
{
#if WITH_EDITOR
    if (!outputAnimation || !TargetSkeleton) {
        UE_LOG(Retargeter, Warning, TEXT("ExportOutputAnimationFBX: Missing outputAnimation or TargetSkeleton"));
        return;
    }

    // Prepare automated export task and options
    UAnimSequenceExporterFBX* Exporter = NewObject<UAnimSequenceExporterFBX>();
    Exporter->SetBatchMode(true);
    Exporter->SetShowExportOption(false);

    FString CleanOutputPath = OutputPath;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(CleanOutputPath), /*Tree*/ true);

    UFbxExportOption* ExportOptions = NewObject<UFbxExportOption>();
    ExportOptions->bASCII = false;
    ExportOptions->BakeMaterialInputs = EFbxMaterialBakeMode::Disabled;
    // Exporting the preview mesh in commandlet can assert inside CPU skinning/material baking paths.
    // Only include the mesh when not running as a commandlet.
    ExportOptions->bExportPreviewMesh = !IsRunningCommandlet();

    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    Task->Object = outputAnimation;
    Task->Exporter = Exporter;
    Task->Filename = CleanOutputPath;
    Task->bSelected = false;
    Task->bReplaceIdentical = true;
    Task->bPrompt = false;
    Task->bAutomated = true;
    Task->bUseFileArchive = false;
    Task->bWriteEmptyFiles = false;
    Task->Options = ExportOptions;

    bool bOk = UExporter::RunAssetExportTask(Task);
    UE_LOG(Retargeter, Log, TEXT("Export FBX %s: %s"), bOk ? TEXT("succeeded") : TEXT("failed"), *CleanOutputPath);
#else
    UE_LOG(Retargeter, Warning, TEXT("ExportOutputAnimationFBX is editor-only and not available in this build"));
#endif
}
