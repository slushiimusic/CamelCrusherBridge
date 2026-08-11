// Native arm64 VST2 plugin. Presents CamelCrusher to the host natively and
// forwards DSP to an x86_64 helper process over shared memory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include "bridge.h"

/* Impersonate the original plugin's VST2 identity so existing host projects
   re-link to this bridge instead of reporting a missing plugin. */
#define CC_UNIQUE_ID 0x43614372u   /* 'CaCr' - same as Camel Audio's build */

/* ---------------- VST2 surface ---------------- */

static void ccProcessReplacing(AEffect* e, float** in, float** out, int32_t n) {
    bridge_process((Bridge*)e->object, in, out, n);
}

static void ccSetParameter(AEffect* e, int32_t i, float v) {
    Bridge* b = (Bridge*)e->object;
    bridge_set_param(b, i, v);
    if (b->editor) cc_editor_refresh(b->editor);
}
static float ccGetParameter(AEffect* e, int32_t i) {
    return bridge_get_param((Bridge*)e->object, i);
}

static intptr_t ccDispatcher(AEffect* e, int32_t op, int32_t idx,
                             intptr_t val, void* ptr, float opt) {
    Bridge* b = (Bridge*)e->object;
    switch (op) {
    case effOpen:  return 0;
    case effClose:
        if (b) {
            if (b->editor) { cc_editor_destroy(b->editor); b->editor = NULL; }
            bridge_stop(b);
            free(b);
        }
        return 0;

    case effSetSampleRate:
        if (b && b->ok) { pthread_mutex_lock(&b->lock);
            b->shm->darg = opt; bridge_call(b, CMD_SETSR, 500000);
            pthread_mutex_unlock(&b->lock); }
        return 0;
    case effSetBlockSize:
        if (b && b->ok) { pthread_mutex_lock(&b->lock);
            b->shm->iarg = (int32_t)val; bridge_call(b, CMD_SETBLOCK, 500000);
            pthread_mutex_unlock(&b->lock); }
        return 0;
    case effMainsChanged:
        if (b && b->ok) { pthread_mutex_lock(&b->lock);
            b->shm->iarg = (int32_t)val; bridge_call(b, CMD_MAINS, 500000);
            pthread_mutex_unlock(&b->lock); }
        return 0;

    case effSetProgram:
        if (b && b->ok) { pthread_mutex_lock(&b->lock);
            b->shm->iarg = (int32_t)val; bridge_call(b, CMD_SETPROGRAM, 500000);
            pthread_mutex_unlock(&b->lock);
            b->curProgram = (int)val;
            if (b->editor) cc_editor_refresh(b->editor); }
        return 0;
    case effGetProgram: return b ? b->curProgram : 0;

    case effGetProgramNameIndexed:
    case effGetProgramName: {
        if (!b || !b->ok || !ptr) return 0;
        pthread_mutex_lock(&b->lock);
        b->shm->iarg = (op == effGetProgramName) ? 0 : idx;
        int ok = bridge_call(b, CMD_PROGNAME, 500000);
        if (ok) { strncpy((char*)ptr, b->shm->str, 24); ((char*)ptr)[23] = 0; }
        pthread_mutex_unlock(&b->lock);
        return ok;
    }

    case effGetParamName:
        if (ptr && idx >= 0 && idx < 17) {
            strncpy((char*)ptr, cc_param_name(idx), 24); ((char*)ptr)[23] = 0; return 1;
        }
        return 0;
    case effGetParamDisplay:
    case effGetParamLabel: {
        if (!b || !b->ok || !ptr) return 0;
        pthread_mutex_lock(&b->lock);
        b->shm->iarg = idx;
        int ok = bridge_call(b, CMD_PARAMTEXT, 200000);
        if (ok) {
            const char* src = (op == effGetParamDisplay) ? b->shm->str : b->shm->str2;
            strncpy((char*)ptr, src, 24); ((char*)ptr)[23] = 0;
        }
        pthread_mutex_unlock(&b->lock);
        return ok;
    }

    case effGetChunk: {
        if (!b || !b->ok || !ptr) return 0;
        pthread_mutex_lock(&b->lock);
        b->shm->iarg = idx;
        intptr_t sz = 0;
        if (bridge_call(b, CMD_GETCHUNK, 2000000)) {
            sz = b->shm->chunkSize;
            *(void**)ptr = b->shm->chunk;   /* stable until next call */
        }
        pthread_mutex_unlock(&b->lock);
        return sz;
    }
    case effSetChunk: {
        if (!b || !b->ok || !ptr || val <= 0) return 0;
        if (val > BRIDGE_MAX_CHUNK) return 0;
        pthread_mutex_lock(&b->lock);
        memcpy(b->shm->chunk, ptr, (size_t)val);
        b->shm->chunkSize = (int32_t)val;
        b->shm->iarg = idx;
        int ok = bridge_call(b, CMD_SETCHUNK, 2000000);
        pthread_mutex_unlock(&b->lock);
        if (ok && b->editor) cc_editor_refresh(b->editor);
        return ok;
    }

    case effEditGetRect: {
        static ERect r = { 0, 0, CC_UI_H, CC_UI_W };
        if (ptr) *(ERect**)ptr = &r;
        return 1;
    }
    case effEditOpen:
        if (b && !b->editor) b->editor = cc_editor_create(b, ptr);
        return b && b->editor ? 1 : 0;
    case effEditClose:
        if (b && b->editor) { cc_editor_destroy(b->editor); b->editor = NULL; }
        return 0;
    case effEditIdle:
        return 0;

    case effGetEffectName:
        if (ptr) { strncpy((char*)ptr, "CamelCrusher", 32); return 1; } return 0;
    case effGetVendorString:
        if (ptr) { strncpy((char*)ptr, "Camel Audio", 64); return 1; } return 0;
    case effGetProductString:
        if (ptr) { strncpy((char*)ptr, "CamelCrusher", 64); return 1; } return 0;
    case effGetVendorVersion: return 1;
    case effGetVstVersion:    return 2400;
    case effGetPlugCategory:  return 0;
    case effGetTailSize:      return 0;
    case effCanDo:
        if (ptr && (!strcmp((char*)ptr, "receiveVstEvents") ||
                    !strcmp((char*)ptr, "receiveVstMidiEvent"))) return 1;
        return -1;
    case effSetProcessPrecision: return 1;
    default: return 0;
    }
}

__attribute__((visibility("default")))
AEffect* VSTPluginMain(AudioMasterProc master) {
    Bridge* b = (Bridge*)calloc(1, sizeof(Bridge));
    if (!b) return NULL;
    b->master = master;

    AEffect* e = &b->eff;
    e->magic = kEffectMagic;
    e->object = b;
    e->dispatcher = ccDispatcher;
    e->setParameter = ccSetParameter;
    e->getParameter = ccGetParameter;
    e->processReplacing = ccProcessReplacing;
    e->numPrograms = 20;
    e->numParams   = 17;
    e->numInputs   = 2;
    e->numOutputs  = 2;
    e->uniqueID    = CC_UNIQUE_ID;
    e->version     = 1;          /* matches Camel Audio's original */
    /* Exactly the original's flag word (0x39) so hosts treat it identically. */
    e->flags       = effFlagsCanReplacing | effFlagsProgramChunks
                   | effFlagsHasEditor | effFlagsCanMono;

    if (bridge_start(b)) {
        if (b->shm->numParams   > 0) e->numParams   = b->shm->numParams;
        if (b->shm->numPrograms > 0) e->numPrograms = b->shm->numPrograms;
        e->initialDelay = b->shm->initialDelay;
    }
    return e;
}

/* Some hosts still look for the legacy entry symbol. */
__attribute__((visibility("default")))
AEffect* main_macho(AudioMasterProc master) { return VSTPluginMain(master); }
