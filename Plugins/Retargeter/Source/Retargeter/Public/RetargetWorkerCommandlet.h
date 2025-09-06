// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RetargetWorkerCommandlet.generated.h"

/**
 * 
 */
UCLASS()
class RETARGETER_API URetargetWorkerCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
    URetargetWorkerCommandlet();
	virtual int32 Main(const FString& Params) override;

private:
    void ProcessDirectory(const FString& BasePath, const FString& SubDir, int32 WorkerIndex, int32 NumWorkers);
    void ProcessTrainDirectory(const FString& TrainPath, int32 WorkerIndex, int32 NumWorkers);
    void ProcessTestValDirectory(const FString& DirPath, const FString& DirName, int32 WorkerIndex, int32 NumWorkers);
    TArray<FString> GetFBXFiles(const FString& DirectoryPath);
    TArray<FString> GetRandomSubset(const TArray<FString>& InputArray, int32 Count);
};
