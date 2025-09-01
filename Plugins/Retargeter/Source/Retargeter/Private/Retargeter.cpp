// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter.h"
// Editor-only includes
#if WITH_EDITOR
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#endif
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FRetargeterModule"

// Static singleton instance
FRetargeterModule* FRetargeterModule::SingletonInstance = nullptr;

FRetargeterModule& FRetargeterModule::Get()
{
    check(SingletonInstance);
    return *SingletonInstance;
}

void FRetargeterModule::StartupModule()
{
    // Set singleton instance
    SingletonInstance = this;

#if WITH_EDITOR
    // Register editor menus on startup (editor only)
    if (UToolMenus::IsToolMenuUIEnabled()) {
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FRetargeterModule::RegisterMenus));
    }
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
            FExecuteAction::CreateLambda([this]() { this->PluginButtonClicked(); }),
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRetargeterModule, Retargeter)