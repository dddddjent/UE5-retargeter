// Fill out your copyright notice in the Description page of Project Settings.

#include "RetargetAll0Commandlet.h"
#include "HAL/FileManager.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RetargetCommandletShared.h"
#include "Retargeter.h"
#include "RetargeterLog.h"
// Scoped verbosity override macro
#include "Logging/LogScopedVerbosityOverride.h"
// Log category declaration for LogScript
#include "CoreGlobals.h"
// Scope exit helper for restoring global verbosity
#include "Misc/ScopeExit.h"
// Script exception handler (to intercept LogScript "Script Msg" output)
#include "Misc/CoreMisc.h"

URetargetAll0Commandlet::URetargetAll0Commandlet() { LogToConsole = false; }

int32 URetargetAll0Commandlet::Main(const FString& Params)
{
    UE_LOG(RetargetAllCommandlet, Log, TEXT("---Batch Retargeting All Animations---"));

    // Parse required argument: -input=<base_folder_path>
    FString BasePath;
    if (!FParse::Value(*Params, TEXT("input="), BasePath) || BasePath.IsEmpty()) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Missing required argument: -input=<base folder path>"));
        return 1;
    }

    const FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
    auto ExpandTilde = [&](FString& InOutPath) {
        if (HomeDir.IsEmpty()) {
            return;
        }

        // If path starts with ~ or ~/
        if (InOutPath.StartsWith(TEXT("~"))) {
            InOutPath = HomeDir / InOutPath.Mid(1);
        }

        // Replace occurrences of '/~/' with '/<home>/'
        const FString SlashTilde = TEXT("/~/");
        const FString Replacement = FString::Printf(TEXT("/%s/"), *HomeDir);
        InOutPath = InOutPath.Replace(*SlashTilde, *Replacement);
    };
    ExpandTilde(BasePath);
    BasePath = FPaths::ConvertRelativePathToFull(BasePath);
    UE_LOG(RetargetAllCommandlet, Log, TEXT("Base Path: %s"), *BasePath);

    // Validate base path exists
    if (!FPaths::DirectoryExists(BasePath)) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Base directory does not exist: %s"), *BasePath);
        return 2;
    }

    UE_LOG(RetargetAllCommandlet, Log, TEXT("Arguments validated. Proceeding with batch retargeting..."));

    RetargetAllInDataset(BasePath);

    return 0;
}

void URetargetAll0Commandlet::RetargetAllInDataset(const FString& BasePath)
{
    // Temporarily suppress Retargeter logs during the entire dataset retargeting.
    LOG_SCOPE_VERBOSITY_OVERRIDE(Retargeter, ELogVerbosity::NoLogging);
    // Intercept Script Msg emissions and drop Display-level entries
    FScopedScriptExceptionHandler ScriptLogFilter([](ELogVerbosity::Type Verbosity, const TCHAR* ExceptionMessage, const TCHAR* StackMessage)
    {
        if (Verbosity == ELogVerbosity::Display)
        {
            return; // drop Display-level Script Msg
        }
        // Forward all other verbosities using the default logging handler
        FScriptExceptionHandler::LoggingExceptionHandler(Verbosity, ExceptionMessage, StackMessage);
    });

    // Process train, val, test directories
    TArray<FString> SubDirs = { TEXT("train"), TEXT("val"), TEXT("test") };

    for (const FString& SubDir : SubDirs) {
        const FString SubDirPath = FPaths::Combine(BasePath, SubDir);

        if (!FPaths::DirectoryExists(SubDirPath)) {
            UE_LOG(RetargetAllCommandlet, Warning, TEXT("Directory does not exist, skipping: %s"), *SubDirPath);
            continue;
        }

        UE_LOG(RetargetAllCommandlet, Log, TEXT("Processing directory: %s"), *SubDir);

        if (SubDir == TEXT("train")) {
            ProcessTrainDirectory(SubDirPath);
        } else {
            ProcessTestValDirectory(SubDirPath, SubDir);
        }
    }
}

void URetargetAll0Commandlet::ProcessTrainDirectory(const FString& TrainPath)
{
    const FString CharacterPath = FPaths::Combine(TrainPath, TEXT("Character"));
    const FString AnimationPath = FPaths::Combine(TrainPath, TEXT("Animation"));
    const FString RetargetPath = FPaths::Combine(TrainPath, TEXT("Retarget"));

    if (!FPaths::DirectoryExists(CharacterPath) || !FPaths::DirectoryExists(AnimationPath)) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Missing Character or Animation directory in train"));
        return;
    }

    // Always clear any existing Retarget directory contents so we start fresh.
    if (FPaths::DirectoryExists(RetargetPath)) {
        UE_LOG(RetargetAllCommandlet, Log, TEXT("Clearing existing Retarget directory: %s"), *RetargetPath);
        // Delete all files inside RetargetPath (non-recursive) and subfolders recursively.
        IFileManager& FM = IFileManager::Get();
        FM.DeleteDirectory(*RetargetPath, false, true);
    }

    // Create Retarget directory if it doesn't exist (or recreate after deletion)
    if (!FPaths::DirectoryExists(RetargetPath)) {
        if (!IFileManager::Get().MakeDirectory(*RetargetPath, true)) {
            UE_LOG(RetargetAllCommandlet, Error, TEXT("Failed to create Retarget directory: %s"), *RetargetPath);
            return;
        }
    }

    // Get all skeleton and animation files
    TArray<FString> SkeletonFiles = GetFBXFiles(CharacterPath);
    TArray<FString> AnimationFiles = GetFBXFiles(AnimationPath);

    if (SkeletonFiles.Num() == 0) {
        UE_LOG(RetargetAllCommandlet, Warning, TEXT("No skeleton files found in Character directory"));
        return;
    }

    if (AnimationFiles.Num() == 0) {
        UE_LOG(RetargetAllCommandlet, Warning, TEXT("No animation files found in Animation directory"));
        return;
    }

    UE_LOG(RetargetAllCommandlet, Log, TEXT("Found %d skeletons and %d animations in train"), SkeletonFiles.Num(),
        AnimationFiles.Num());

    FRetargeterModule& Retargeter = FRetargeterModule::Get();
    Retargeter.SetPersistAssets(false);

    // For each skeleton, get 100 random animations (or all if less than 100)
    for (int32 SkeletonIdx = 0; SkeletonIdx < SkeletonFiles.Num(); ++SkeletonIdx) {
        const FString& SkeletonFile = SkeletonFiles[SkeletonIdx];
        const FString SkeletonName = FPaths::GetBaseFilename(SkeletonFile);

        UE_LOG(RetargetAllCommandlet, Log, TEXT("Processing skeleton %d/%d: %s"), SkeletonIdx + 1, SkeletonFiles.Num(),
            *SkeletonName);

        const int32 MaxAnimations = FMath::Min(100, AnimationFiles.Num());
        TArray<FString> RandomAnimations = GetRandomSubset(AnimationFiles, MaxAnimations);
        UE_LOG(RetargetAllCommandlet, Log, TEXT("Retargeting %d animations for skeleton: %s"), RandomAnimations.Num(),
            *SkeletonName);

        // Retarget each selected animation
        for (int32 AnimIdx = 0; AnimIdx < RandomAnimations.Num(); ++AnimIdx) {
            const FString& AnimationFile = RandomAnimations[AnimIdx];
            const FString AnimationName = FPaths::GetBaseFilename(AnimationFile);
            const FString PrefixedName = SkeletonName + TEXT("__") + AnimationName + TEXT(".fbx");
            const FString OutputFile = FPaths::Combine(RetargetPath, PrefixedName);

            UE_LOG(RetargetAllCommandlet, Verbose, TEXT("  Retargeting animation %d/%d: %s -> %s"), AnimIdx + 1,
                RandomAnimations.Num(), *AnimationName, *SkeletonName);

            Retargeter.RetargetAPair(AnimationFile, SkeletonFile, OutputFile);
        }
    }
}

void URetargetAll0Commandlet::ProcessTestValDirectory(const FString& DirPath, const FString& DirName)
{
    const FString CharacterPath = FPaths::Combine(DirPath, TEXT("Character"));
    const FString AnimationPath = FPaths::Combine(DirPath, TEXT("Animation"));
    const FString RetargetPath = FPaths::Combine(DirPath, TEXT("Retarget"));

    if (!FPaths::DirectoryExists(CharacterPath) || !FPaths::DirectoryExists(AnimationPath)) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Missing Character or Animation directory in %s"), *DirName);
        return;
    }

    // Always clear any existing Retarget directory contents so we start fresh.
    if (FPaths::DirectoryExists(RetargetPath)) {
        UE_LOG(RetargetAllCommandlet, Log, TEXT("Clearing existing Retarget directory: %s"), *RetargetPath);
        IFileManager::Get().DeleteDirectory(*RetargetPath, false, true);
    }

    // Create Retarget directory if it doesn't exist (or recreate after deletion)
    if (!FPaths::DirectoryExists(RetargetPath)) {
        if (!IFileManager::Get().MakeDirectory(*RetargetPath, true)) {
            UE_LOG(RetargetAllCommandlet, Error, TEXT("Failed to create Retarget directory: %s"), *RetargetPath);
            return;
        }
    }

    // Get all skeleton and animation files
    TArray<FString> SkeletonFiles = GetFBXFiles(CharacterPath);
    TArray<FString> AnimationFiles = GetFBXFiles(AnimationPath);

    if (SkeletonFiles.Num() == 0) {
        UE_LOG(RetargetAllCommandlet, Warning, TEXT("No skeleton files found in %s Character directory"), *DirName);
        return;
    }

    if (AnimationFiles.Num() == 0) {
        UE_LOG(RetargetAllCommandlet, Warning, TEXT("No animation files found in %s Animation directory"), *DirName);
        return;
    }

    UE_LOG(RetargetAllCommandlet, Display, TEXT("Found %d skeletons and %d animations in %s"), SkeletonFiles.Num(),
        AnimationFiles.Num(), *DirName);

    FRetargeterModule& Retargeter = FRetargeterModule::Get();
    Retargeter.SetPersistAssets(false);

    // For each skeleton, retarget ALL animations
    for (int32 SkeletonIdx = 0; SkeletonIdx < SkeletonFiles.Num(); ++SkeletonIdx) {
        const FString& SkeletonFile = SkeletonFiles[SkeletonIdx];
        const FString SkeletonName = FPaths::GetBaseFilename(SkeletonFile);

        UE_LOG(RetargetAllCommandlet, Display, TEXT("Processing skeleton %d/%d: %s"), SkeletonIdx + 1,
            SkeletonFiles.Num(), *SkeletonName);
        UE_LOG(RetargetAllCommandlet, Display, TEXT("Retargeting %d animations for skeleton: %s"), AnimationFiles.Num(),
            *SkeletonName);

        // Retarget each animation
        for (int32 AnimIdx = 0; AnimIdx < AnimationFiles.Num(); ++AnimIdx) {
            const FString& AnimationFile = AnimationFiles[AnimIdx];
            const FString AnimationName = FPaths::GetBaseFilename(AnimationFile);
            const FString PrefixedName = SkeletonName + TEXT("__") + AnimationName + TEXT(".fbx");
            const FString OutputFile = FPaths::Combine(RetargetPath, PrefixedName);

            UE_LOG(RetargetAllCommandlet, Verbose, TEXT("  Retargeting animation %d/%d: %s -> %s"), AnimIdx + 1,
                AnimationFiles.Num(), *AnimationName, *SkeletonName);

            Retargeter.RetargetAPair(AnimationFile, SkeletonFile, OutputFile);
        }
    }
}

TArray<FString> URetargetAll0Commandlet::GetFBXFiles(const FString& DirectoryPath)
{
    TArray<FString> FbxFiles;

    IFileManager& FileManager = IFileManager::Get();
    const FString SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.fbx"));

    FileManager.FindFiles(FbxFiles, *SearchPattern, true, false);

    // Convert relative paths to full paths
    for (FString& File : FbxFiles) {
        File = FPaths::Combine(DirectoryPath, File);
    }

    return FbxFiles;
}

TArray<FString> URetargetAll0Commandlet::GetRandomSubset(const TArray<FString>& InputArray, int32 Count)
{
    TArray<FString> Result = InputArray;

    // If we need fewer items than available, randomly shuffle and take the first Count items
    if (Count < InputArray.Num()) {
        // Shuffle the array
        for (int32 i = Result.Num() - 1; i > 0; --i) {
            const int32 j = FMath::RandRange(0, i);
            Result.Swap(i, j);
        }

        // Take only the first Count items
        Result.SetNum(Count);
    }

    return Result;
}
