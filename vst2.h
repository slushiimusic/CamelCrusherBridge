// Minimal VST2.4 ABI declarations (hand-written interface description).
#ifndef VST2_H
#define VST2_H
#include <stdint.h>

typedef struct AEffect AEffect;
typedef intptr_t (*DispatcherProc)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
typedef void  (*ProcessProc)(AEffect*, float**, float**, int32_t);
typedef void  (*SetParamProc)(AEffect*, int32_t, float);
typedef float (*GetParamProc)(AEffect*, int32_t);
typedef void  (*ProcessDoubleProc)(AEffect*, double**, double**, int32_t);

struct AEffect {
    int32_t magic;
    DispatcherProc dispatcher;
    ProcessProc process;
    SetParamProc setParameter;
    GetParamProc getParameter;
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    ProcessProc processReplacing;
    ProcessDoubleProc processDoubleReplacing;
    char future[56];
};

typedef struct { int16_t top, left, bottom, right; } ERect;

typedef intptr_t (*AudioMasterProc)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
typedef AEffect* (*VstMainProc)(AudioMasterProc);

#define kEffectMagic 0x56737450

#define effOpen                 0
#define effClose                1
#define effSetProgram           2
#define effGetProgram           3
#define effSetProgramName       4
#define effGetProgramName       5
#define effGetParamLabel        6
#define effGetParamDisplay      7
#define effGetParamName         8
#define effSetSampleRate       10
#define effSetBlockSize        11
#define effMainsChanged        12
#define effEditGetRect         13
#define effEditOpen            14
#define effEditClose           15
#define effEditIdle            19
#define effGetChunk            23
#define effSetChunk            24
#define effProcessEvents       25
#define effGetProgramNameIndexed 29
#define effGetEffectName       45
#define effGetVendorString     47
#define effGetProductString    48
#define effGetVendorVersion    49
#define effCanDo               51
#define effGetTailSize         52
#define effGetPlugCategory     35
#define effGetVstVersion       58
#define effStartProcess        71
#define effStopProcess         72
#define effSetProcessPrecision 77

#define effFlagsHasEditor          (1 << 0)
#define effFlagsCanMono            (1 << 3)   /* deprecated, but the original sets it */
#define effFlagsCanReplacing       (1 << 4)
#define effFlagsProgramChunks      (1 << 5)
#define effFlagsIsSynth            (1 << 8)
#define effFlagsNoSoundInStop      (1 << 9)

#define audioMasterVersion              1
#define audioMasterAutomate             0
#define audioMasterCurrentId            2
#define audioMasterIdle                 3
#define audioMasterGetSampleRate       16
#define audioMasterGetBlockSize        17
#define audioMasterSizeWindow          15
#define audioMasterBeginEdit           43
#define audioMasterEndEdit             44

#endif
