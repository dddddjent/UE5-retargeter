#!/bin/bash

~/Documents/Applications/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
    -mode=GenerateClangDatabase \
    -OutputDir="/home/ljl/Documents/Unreal Projects/Retarget" \
    RetargetEditor \
    Linux \
    Development \
    "/home/ljl/Documents/Unreal Projects/Retarget/Retarget.uproject" \
    -Progress