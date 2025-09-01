#include "RetargetAPairCommandlet.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Retargeter.h"

// Define a dedicated log category for this plugin/commandlet
DEFINE_LOG_CATEGORY_STATIC(Retargeter, Log, All);

URetargetAPairCommandlet::URetargetAPairCommandlet() { LogToConsole = false; }
// Helper: parse and validate command-line args. Returns 0 on success or an error code (>0).
static int32 ParseArgs(const FString& Params, FString& OutInputFbx, FString& OutTargetFbx, FString& OutOutputPath, bool& bOutPersist)
{
    // Parse required arguments: -input=<path> -target=<path> -output=<path>
    if (!FParse::Value(*Params, TEXT("input="), OutInputFbx) || OutInputFbx.IsEmpty()) {
        UE_LOG(Retargeter, Error, TEXT("Missing required argument: -input=<path to input fbx>"));
        return 1;
    }
    if (!FParse::Value(*Params, TEXT("target="), OutTargetFbx) || OutTargetFbx.IsEmpty()) {
        UE_LOG(Retargeter, Error, TEXT("Missing required argument: -target=<path to target fbx>"));
        return 2;
    }
    if (!FParse::Value(*Params, TEXT("output="), OutOutputPath) || OutOutputPath.IsEmpty()) {
        UE_LOG(Retargeter, Error, TEXT("Missing required argument: -output=<output path>"));
        return 3;
    }

    // Expand ~ to the user's HOME directory if present, then normalize
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

    ExpandTilde(OutInputFbx);
    ExpandTilde(OutTargetFbx);
    ExpandTilde(OutOutputPath);

    OutInputFbx = FPaths::ConvertRelativePathToFull(OutInputFbx);
    OutTargetFbx = FPaths::ConvertRelativePathToFull(OutTargetFbx);
    OutOutputPath = FPaths::ConvertRelativePathToFull(OutOutputPath);

    UE_LOG(Retargeter, Display, TEXT("Input: %s"), *OutInputFbx);
    UE_LOG(Retargeter, Display, TEXT("Target: %s"), *OutTargetFbx);
    UE_LOG(Retargeter, Display, TEXT("Output: %s"), *OutOutputPath);

    // Basic existence checks for inputs
    if (!FPaths::FileExists(OutInputFbx)) {
        UE_LOG(Retargeter, Error, TEXT("Input file not found: %s"), *OutInputFbx);
        return 4;
    }
    if (!FPaths::FileExists(OutTargetFbx)) {
        UE_LOG(Retargeter, Error, TEXT("Target file not found: %s"), *OutTargetFbx);
        return 5;
    }

    // Ensure output directory exists (create if necessary)
    const FString OutputDir = FPaths::GetPath(OutOutputPath);
    if (!OutputDir.IsEmpty() && !FPaths::DirectoryExists(OutputDir)) {
        if (!IFileManager::Get().MakeDirectory(*OutputDir, true)) {
            UE_LOG(Retargeter, Error, TEXT("Failed to create output directory: %s"), *OutputDir);
            return 6;
        }
    }

    // Optional: -persist=true|false controls clearing/saving to disk. Default false.
    FString PersistStr;
    bOutPersist = false;
    if (FParse::Value(*Params, TEXT("persist="), PersistStr)) {
        PersistStr = PersistStr.ToLower();
        bOutPersist = (PersistStr == TEXT("1") || PersistStr == TEXT("true") || PersistStr == TEXT("yes"));
    }

    return 0;
}

int32 URetargetAPairCommandlet::Main(const FString& Params)
{
    UE_LOG(Retargeter, Display, TEXT("---Retargeting a pair of animations---"));

    FString InputFbx, TargetFbx, OutputPath;
    bool bPersist = false;
    const int32 ParseResult = ParseArgs(Params, InputFbx, TargetFbx, OutputPath, bPersist);
    if (ParseResult != 0) {
        return ParseResult;
    }

    UE_LOG(Retargeter, Display, TEXT("Arguments validated. Proceeding with retargeting..."));

    auto retargeter = FRetargeterModule::Get();
    retargeter.SetPersistAssets(bPersist);
    retargeter.RetargetAPair(InputFbx, TargetFbx, OutputPath);

    return 0;
}
