// Fill out your copyright notice in the Description page of Project Settings.

#include "RetargeterCommandlet.h"
#include "HAL/PlatformProcess.h"
#include "Retargeter.h"

URetargeterCommandlet::URetargeterCommandlet() { LogToConsole = true; }

int32 URetargeterCommandlet::Main(const FString& Params)
{
    UE_LOG(LogTemp, Display, TEXT("=== Retargeter Commandlet Started ==="));
    auto retargeter = FRetargeterModule::Get();
    return 0;
}
