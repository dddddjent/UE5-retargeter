# AI Coding Assistant Instructions for Retarget Project

# REMEMBER: KEEP IT SIMPLE, KEEP IT NAIVE
# REMEMBER: DON'T OVERCOMPLICATE SOLUTIONS
# REMEMBER: DON'T ADD TOO MANY COMMENTS

## Project Overview
This is an Unreal Engine 5 project focused on animation retargeting functionality. The project consists of:
- **Main Game Module**: `Retarget` - Minimal game module serving as the project container
- **Retargeter Plugin**: Core functionality for importing and retargeting FBX animations
- **Dependencies**: IKRig, EditorScriptingUtilities, AssetTools for animation processing

## Architecture Patterns

### Module Structure
```
Retarget.uproject (Game Project)
©À©¤©¤ Source/Retarget/ (Main Game Module - minimal)
©¦   ©À©¤©¤ Retarget.Build.cs (Core, Engine, EnhancedInput deps)
©¦   ©¸©¤©¤ Retarget.cpp/h (Basic IMPLEMENT_PRIMARY_GAME_MODULE)
©¸©¤©¤ Plugins/Retargeter/ (Core Plugin)
    ©À©¤©¤ Retargeter.uplugin (IKRig, EditorScriptingUtilities plugins)
    ©¸©¤©¤ Source/Retargeter/
        ©À©¤©¤ Retargeter.Build.cs (AssetTools, UnrealEd, ToolMenus deps)
        ©¸©¤©¤ Public/Private/ (Module implementation)
```

### Singleton Pattern
```cpp
class FRetargeterModule : public IModuleInterface {
private:
    static FRetargeterModule* SingletonInstance;
public:
    static FRetargeterModule& Get() {
        check(SingletonInstance);
        return *SingletonInstance;
    }
};
```

### Editor Integration
```cpp
#if WITH_EDITOR
// Menu registration in StartupModule
if (UToolMenus::IsToolMenuUIEnabled()) {
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FRetargeterModule::RegisterMenus));
}
#endif
```

## Critical Workflows

### Building
```bash
# From project root
./build.sh  # Builds RetargetEditor Linux Development

# Or manually
/home/ljl/Documents/Applications/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
    RetargetEditor \
    Linux \
    Development \
    "/home/ljl/Documents/Unreal Projects/Retarget/Retarget.uproject" \
    -WaitMutex
```

### Compile Commands Generation
```bash
# Generate clangd compile_commands.json for IntelliSense
./compile_commands.sh
```

### Asset Import Pattern
```cpp
void FRetargeterModule::RetargetAPair(const FString& InputFbx, const FString& TargetFbx, const FString& OutputPath) {
    // File validation
    if (!FPaths::FileExists(InputFbx)) {
        UE_LOG(LogTemp, Error, TEXT("Input FBX file does not exist: %s"), *InputFbx);
        return;
    }

    // Automated import using AssetTools
    FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->Filenames.Add(InputFbx);
    ImportData->DestinationPath = OutputPath;
    ImportData->bReplaceExisting = true;

    TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);

    // Find animation sequence
    for (UObject* Asset : ImportedAssets) {
        if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset)) {
            // Process animation
            break;
        }
    }
}
```

## Project-Specific Conventions

### Include Guards and Namespaces
```cpp
#define LOCTEXT_NAMESPACE "FRetargeterModule"
// ... code ...
#undef LOCTEXT_NAMESPACE
```

### Logging
```cpp
UE_LOG(LogTemp, Log, TEXT("Message: %s"), *FStringVar);
UE_LOG(LogTemp, Error, TEXT("Error: %s"), *FStringVar);
```

### Build Configuration
- **PCH Usage**: `PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs`
- **Editor Dependencies**: Wrapped in `if (Target.bBuildEditor)` blocks
- **Module Loading**: `LoadingPhase = "Default"`

### File Organization
- **Public headers**: Class declarations, public APIs
- **Private sources**: Implementation details
- **Build files**: ModuleRules with dependency management

## Integration Points

### External Dependencies
- **IKRig**: Animation retargeting framework
- **EditorScriptingUtilities**: Editor automation tools
- **AssetTools**: Programmatic asset import/export

### Python Integration
```python
# Content/Python/test.py pattern
import unreal
unreal.log("Hello, Unreal Engine!")
```

## Development Environment

### VS Code Setup
- **IntelliSense**: Uses generated `compile_commands.json`
- **Settings**: `"C_Cpp.intelliSenseEngine": "disabled"` (relies on clangd)
- **Build Tasks**: Integrated with Unreal Build Tool

### Key Directories
- `/Engine/Build/BatchFiles/Linux/`: Build scripts
- `/Engine/Source/`: Engine source for reference
- `/Plugins/Retargeter/Source/`: Plugin implementation
- `/Content/Python/`: Python scripting

## Common Patterns

### Error Handling
```cpp
if (!FPaths::FileExists(FilePath)) {
    UE_LOG(LogTemp, Error, TEXT("File does not exist: %s"), *FilePath);
    return;
}
```

### Memory Management
```cpp
UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
// Object will be garbage collected by Unreal
```

### String Handling
```cpp
FString Path = FPaths::Combine(OutputPath, TEXT("Animation"));
// Use TEXT() macro for string literals
```

## Debugging Tips
- Use `UE_LOG` for debugging (appears in Output Log window)
- Check `Saved/Logs/` for detailed logs
- Use `ensure()` for assertions that continue execution
- Use `check()` for critical errors that should crash in development

## Performance Considerations
- Asset import is asynchronous when possible
- Use `TArray` for collections
- Minimize string operations in hot paths
- Cache frequently accessed objects
