#! /bin/bash

UnrealEditor-Cmd "/home/ljl/Documents/Unreal Projects/Retarget/Retarget.uproject" \
    -run=Retargeter.RetargetAll0 \
    -input=~/Downloads/step6/step6 \
    -seed=42 \
    -workers=16 \
    -LogCmds="global off, log RetargetAllCommandlet verbose" \
    -NoStdOut \
    --stdout \
    -NOCONSOLE \
    -unattended