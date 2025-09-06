// Fill out your copyright notice in the Description page of Project Settings.

#include "RetargetWorkerCommandlet.h"
#include "HAL/FileManager.h"
#include "Logging/LogVerbosity.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RetargetCommandletShared.h"
#include "Retargeter.h"
#include "RetargeterLog.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "Misc/CoreMisc.h"

URetargetWorkerCommandlet::URetargetWorkerCommandlet() { LogToConsole = false; }

int32 URetargetWorkerCommandlet::Main(const FString& Params)
{
    FString BasePath, SubDir;
    int32 WorkerIndex = -1, NumWorkers = -1;

    if (!FParse::Value(*Params, TEXT("input="), BasePath) || BasePath.IsEmpty()) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Worker: Missing required argument: -input=<base folder path>"));
        return 1;
    }
    if (!FParse::Value(*Params, TEXT("subdir="), SubDir) || SubDir.IsEmpty()) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Worker: Missing required argument: -subdir=<train|val|test>"));
        return 1;
    }
    if (!FParse::Value(*Params, TEXT("workerindex="), WorkerIndex)) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Worker: Missing required argument: -workerindex=<index>"));
        return 1;
    }
    if (!FParse::Value(*Params, TEXT("numworkers="), NumWorkers)) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Worker: Missing required argument: -numworkers=<total>"));
        return 1;
    }

    const FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
    auto ExpandTilde = [&](FString& InOutPath) {
        if (HomeDir.IsEmpty()) return;
        if (InOutPath.StartsWith(TEXT("~"))) InOutPath = HomeDir / InOutPath.Mid(1);
        const FString SlashTilde = TEXT("/~/");
        const FString Replacement = FString::Printf(TEXT("/%s/"), *HomeDir);
        InOutPath = InOutPath.Replace(*SlashTilde, *Replacement);
    };
    ExpandTilde(BasePath);
    BasePath = FPaths::ConvertRelativePathToFull(BasePath);

    if (!FPaths::DirectoryExists(BasePath)) {
        UE_LOG(RetargetAllCommandlet, Error, TEXT("Worker: Base directory does not exist: %s"), *BasePath);
        return 2;
    }

    UE_LOG(RetargetAllCommandlet, Log, TEXT("Worker %d/%d processing %s in %s"), WorkerIndex, NumWorkers, *SubDir, *BasePath);

    ProcessDirectory(BasePath, SubDir, WorkerIndex, NumWorkers);

    return 0;
}

void URetargetWorkerCommandlet::ProcessDirectory(const FString& BasePath, const FString& SubDir, int32 WorkerIndex, int32 NumWorkers)
{
    LOG_SCOPE_VERBOSITY_OVERRIDE(Retargeter, ELogVerbosity::NoLogging);
    FScopedScriptExceptionHandler ScriptLogFilter(
        [](ELogVerbosity::Type Verbosity, const TCHAR* ExceptionMessage, const TCHAR* StackMessage) {
            if (Verbosity == ELogVerbosity::Display) return;
            FScriptExceptionHandler::LoggingExceptionHandler(Verbosity, ExceptionMessage, StackMessage);
        });

    const FString SubDirPath = FPaths::Combine(BasePath, SubDir);
    if (!FPaths::DirectoryExists(SubDirPath)) {
        UE_LOG(RetargetAllCommandlet, Warning, TEXT("Worker: Directory does not exist, skipping: %s"), *SubDirPath);
        return;
    }

    if (SubDir == TEXT("train")) {
        ProcessTrainDirectory(SubDirPath, WorkerIndex, NumWorkers);
    } else {
        ProcessTestValDirectory(SubDirPath, SubDir, WorkerIndex, NumWorkers);
    }
}

void URetargetWorkerCommandlet::ProcessTrainDirectory(const FString& TrainPath, int32 WorkerIndex, int32 NumWorkers)
{
    const FString CharacterPath = FPaths::Combine(TrainPath, TEXT("Character"));
    const FString AnimationPath = FPaths::Combine(TrainPath, TEXT("Animation"));
    const FString RetargetPath = FPaths::Combine(TrainPath, TEXT("Retarget"));

    TArray<FString> SkeletonFiles = GetFBXFiles(CharacterPath);
    TArray<FString> AnimationFiles = GetFBXFiles(AnimationPath);

    if (SkeletonFiles.Num() == 0 || AnimationFiles.Num() == 0) return;

    FRetargeterModule& Retargeter = FRetargeterModule::Get();
    Retargeter.SetPersistAssets(false);

    for (int32 SkeletonIdx = WorkerIndex; SkeletonIdx < SkeletonFiles.Num(); SkeletonIdx += NumWorkers) {
        const FString& SkeletonFile = SkeletonFiles[SkeletonIdx];
        const FString SkeletonName = FPaths::GetBaseFilename(SkeletonFile);

        UE_LOG(RetargetAllCommandlet, Display, TEXT("Worker %d: Processing skeleton %d/%d: %s"), WorkerIndex, SkeletonIdx + 1, SkeletonFiles.Num(), *SkeletonName);

        const int32 MaxAnimations = FMath::Min(100, AnimationFiles.Num());
        TArray<FString> RandomAnimations = GetRandomSubset(AnimationFiles, MaxAnimations);

        for (const FString& AnimationFile : RandomAnimations) {
            const FString AnimationName = FPaths::GetBaseFilename(AnimationFile);
            const FString PrefixedName = SkeletonName + TEXT("__") + AnimationName + TEXT(".fbx");
            const FString OutputFile = FPaths::Combine(RetargetPath, PrefixedName);
            Retargeter.RetargetAPair(AnimationFile, SkeletonFile, OutputFile);
        }
    }
}

void URetargetWorkerCommandlet::ProcessTestValDirectory(const FString& DirPath, const FString& DirName, int32 WorkerIndex, int32 NumWorkers)
{
    const FString CharacterPath = FPaths::Combine(DirPath, TEXT("Character"));
    const FString AnimationPath = FPaths::Combine(DirPath, TEXT("Animation"));
    const FString RetargetPath = FPaths::Combine(DirPath, TEXT("Retarget"));

    TArray<FString> SkeletonFiles = GetFBXFiles(CharacterPath);
    TArray<FString> AnimationFiles = GetFBXFiles(AnimationPath);

    if (SkeletonFiles.Num() == 0 || AnimationFiles.Num() == 0) return;

    FRetargeterModule& Retargeter = FRetargeterModule::Get();
    Retargeter.SetPersistAssets(false);

    for (int32 SkeletonIdx = WorkerIndex; SkeletonIdx < SkeletonFiles.Num(); SkeletonIdx += NumWorkers) {
        const FString& SkeletonFile = SkeletonFiles[SkeletonIdx];
        const FString SkeletonName = FPaths::GetBaseFilename(SkeletonFile);

        UE_LOG(RetargetAllCommandlet, Display, TEXT("Worker %d: Processing skeleton %d/%d: %s"), WorkerIndex, SkeletonIdx + 1, SkeletonFiles.Num(), *SkeletonName);

        for (const FString& AnimationFile : AnimationFiles) {
            const FString AnimationName = FPaths::GetBaseFilename(AnimationFile);
            const FString PrefixedName = SkeletonName + TEXT("__") + AnimationName + TEXT(".fbx");
            const FString OutputFile = FPaths::Combine(RetargetPath, PrefixedName);
            Retargeter.RetargetAPair(AnimationFile, SkeletonFile, OutputFile);
        }
    }
}

TArray<FString> URetargetWorkerCommandlet::GetFBXFiles(const FString& DirectoryPath)
{
    TArray<FString> FbxFiles;
    IFileManager::Get().FindFiles(FbxFiles, *FPaths::Combine(DirectoryPath, TEXT("*.fbx")), true, false);
    for (FString& File : FbxFiles) {
        File = FPaths::Combine(DirectoryPath, File);
    }
    return FbxFiles;
}

TArray<FString> URetargetWorkerCommandlet::GetRandomSubset(const TArray<FString>& InputArray, int32 Count)
{
    TArray<FString> Result = InputArray;
    if (Count < InputArray.Num()) {
        for (int32 i = Result.Num() - 1; i > 0; --i) {
            const int32 j = FMath::RandRange(0, i);
            Result.Swap(i, j);
        }
        Result.SetNum(Count);
    }
    return Result;
}
