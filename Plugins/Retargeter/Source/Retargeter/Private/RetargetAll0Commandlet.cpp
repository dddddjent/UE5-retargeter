// Fill out your copyright notice in the Description page of Project Settings.

#include "RetargetAll0Commandlet.h"
#include "HAL/FileManager.h"
#include "Logging/LogVerbosity.h"
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

#include "HAL/PlatformProcess.h"

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
    const int32 NumWorkers = 2;
    TArray<FString> SubDirs = { TEXT("train"), TEXT("val"), TEXT("test") };

    for (const FString& SubDir : SubDirs) {
        // Clear Retarget directory first
        const FString SubDirPath = FPaths::Combine(BasePath, SubDir);
        if (!FPaths::DirectoryExists(SubDirPath)) {
            UE_LOG(RetargetAllCommandlet, Warning, TEXT("Directory does not exist, skipping: %s"), *SubDirPath);
            continue;
        }

        const FString RetargetPath = FPaths::Combine(SubDirPath, TEXT("Retarget"));
        if (FPaths::DirectoryExists(RetargetPath)) {
            UE_LOG(RetargetAllCommandlet, Log, TEXT("Clearing existing Retarget directory: %s"), *RetargetPath);
            IFileManager::Get().DeleteDirectory(*RetargetPath, false, true);
        }
        if (!IFileManager::Get().MakeDirectory(*RetargetPath, true)) {
            UE_LOG(RetargetAllCommandlet, Error, TEXT("Failed to create Retarget directory: %s"), *RetargetPath);
            continue; // Skip this subdir if we can't create the output folder
        }

        TArray<FProcHandle> WorkerProcesses;
        UE_LOG(RetargetAllCommandlet, Log, TEXT("Spawning %d workers for directory: %s"), NumWorkers, *SubDir);

        for (int32 i = 0; i < NumWorkers; ++i) {
            FString EditorExe = FPlatformProcess::GetApplicationName(FPlatformProcess::GetCurrentProcessId());
            FString ProjectPath = FPaths::GetProjectFilePath();
            FString LogFile = FString::Printf(TEXT("\"%sSaved/Logs/worker_%s_%d.log\""), *FPaths::ProjectDir(), *SubDir, i);
            FString Args = FString::Printf(TEXT("\"%s\" -run=RetargetWorker -input=\"%s\" -subdir=%s -workerindex=%d "
                                                "-numworkers=%d -LogCmds=\"global off, log RetargetAllCommandlet "
                                                "verbose\" -NoStdOut --stdout -NOCONSOLE -unattended"),
            // FString Args = FString::Printf(TEXT("\"%s\" -run=RetargetWorker -input=\"%s\" -subdir=%s -workerindex=%d "
            //                                     "-numworkers=%d "
            //                                     " -NoStdOut --stdout -NOCONSOLE -unattended"),
                *ProjectPath, *BasePath, *SubDir, i, NumWorkers);

            UE_LOG(RetargetAllCommandlet, Log, TEXT("Launching worker %d for %s with args: %s"), i, *SubDir, *Args);

            FProcHandle ProcHandle
                = FPlatformProcess::CreateProc(*EditorExe, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
            if (ProcHandle.IsValid()) {
                WorkerProcesses.Add(ProcHandle);
            } else {
                UE_LOG(RetargetAllCommandlet, Error, TEXT("Failed to launch worker process %d for %s"), i, *SubDir);
            }
        }

        UE_LOG(RetargetAllCommandlet, Log, TEXT("Waiting for %d worker processes for %s to complete..."), WorkerProcesses.Num(), *SubDir);

        for (FProcHandle& ProcHandle : WorkerProcesses) {
            FPlatformProcess::WaitForProc(ProcHandle);
            int32 ReturnCode;
            if (FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode)) {
                UE_LOG(RetargetAllCommandlet, Log, TEXT("Worker process for %s finished with exit code %d"), *SubDir, ReturnCode);
            } else {
                UE_LOG(RetargetAllCommandlet, Warning, TEXT("Could not get return code for a worker process for %s."), *SubDir);
            }
            FPlatformProcess::CloseProc(ProcHandle);
        }
        UE_LOG(RetargetAllCommandlet, Log, TEXT("All workers for %s finished."), *SubDir);
    }

    UE_LOG(RetargetAllCommandlet, Log, TEXT("All subdirectories processed."));
}
