// Shared-memory protocol between the native arm64 plugin and the x86_64 helper.
// Layout must be byte-identical when compiled for arm64 and x86_64 (both LP64 LE).
#ifndef BRIDGE_COMMON_H
#define BRIDGE_COMMON_H

#include <stdint.h>
#include <stdatomic.h>

#define BRIDGE_MAGIC     0x43424731u   /* 'CBG1' */
#define BRIDGE_MAX_BLOCK 8192
#define BRIDGE_MAX_CH    2
#define BRIDGE_MAX_PARAM 32
#define BRIDGE_MAX_CHUNK (256*1024)
#define BRIDGE_STRLEN    128

enum {
    CMD_NONE = 0,
    CMD_PROCESS,
    CMD_SETPARAM,
    CMD_GETPARAM,
    CMD_SETSR,
    CMD_SETBLOCK,
    CMD_MAINS,
    CMD_SETPROGRAM,
    CMD_PARAMTEXT,     /* iarg=index -> str=display, str2=label */
    CMD_PROGNAME,      /* iarg=index -> str */
    CMD_GETCHUNK,      /* iarg=isPreset */
    CMD_SETCHUNK,      /* iarg=isPreset, chunkSize */
    CMD_QUIT
};

typedef struct {
    uint32_t magic;
    uint32_t structSize;

    _Atomic uint32_t reqSeq;      /* bumped by plugin to submit work   */
    _Atomic uint32_t ackSeq;      /* set by helper when work completed */
    _Atomic uint32_t helperReady; /* 1 once plugin instantiated ok     */
    _Atomic uint32_t helperFailed;

    int32_t cmd;
    int32_t nframes;
    int32_t iarg;
    int32_t iarg2;
    float   farg;
    double  darg;
    int32_t status;

    /* plugin description, filled by helper at startup */
    int32_t numInputs, numOutputs, numParams, numPrograms;
    int32_t initialDelay, effFlags, uniqueID;

    float   params[BRIDGE_MAX_PARAM];
    /* Bit i set => params[i] changed and must be pushed into the hosted
       plugin before the next render. Lets setParameter() stay lock-free. */
    _Atomic uint32_t paramDirty;
    char    str[BRIDGE_STRLEN];
    char    str2[BRIDGE_STRLEN];

    int32_t chunkSize;
    uint8_t chunk[BRIDGE_MAX_CHUNK];

    /* [0..1] = input channels, [2..3] = output channels */
    float   audio[BRIDGE_MAX_CH * 2][BRIDGE_MAX_BLOCK];
} BridgeSHM;

#endif
