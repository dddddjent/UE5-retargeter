// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Main retargeter module class
 */
class FRetargeterModule : public IModuleInterface {
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static FRetargeterModule& Get();

private:
    void RegisterMenus();
    void PluginButtonClicked();

    static FRetargeterModule* SingletonInstance;
};
