// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RetargeterCommandlet.generated.h"

/**
 *
 */
UCLASS()
class URetargeterCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URetargeterCommandlet();

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface
// 
};
