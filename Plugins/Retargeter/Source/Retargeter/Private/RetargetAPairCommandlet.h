#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RetargetAPairCommandlet.generated.h"

/**
 * Commandlet that runs the Retarget-A-Pair operation.
 */
UCLASS()
class URetargetAPairCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    URetargetAPairCommandlet();

    //~ Begin UCommandlet Interface
    virtual int32 Main(const FString& Params) override;
    //~ End UCommandlet Interface
    
};

