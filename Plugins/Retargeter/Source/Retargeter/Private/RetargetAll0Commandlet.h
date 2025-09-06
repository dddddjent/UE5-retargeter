// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RetargetAll0Commandlet.generated.h"

/**
 * Commandlet that batch retargets animations to multiple skeletons in train/val/test directories
 */
UCLASS()
class URetargetAll0Commandlet : public UCommandlet
{
	GENERATED_BODY()
	
public:
	URetargetAll0Commandlet();

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	void RetargetAllInDataset(const FString& BasePath, int32 MainSeed, int32 NumWorkers);
	void ProcessTrainDirectory(const FString& TrainPath);
	void ProcessTestValDirectory(const FString& DirPath, const FString& DirName);
	TArray<FString> GetFBXFiles(const FString& DirectoryPath);
	TArray<FString> GetRandomSubset(const TArray<FString>& InputArray, int32 Count);
};
