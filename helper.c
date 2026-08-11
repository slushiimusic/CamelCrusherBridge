// x86_64 helper: hosts the original Intel CamelCrusher VST2 headlessly and
// services DSP/parameter requests posted by the native arm64 plugin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/mman.h>
#include "vst2.h"
#include "bridge_common.h"

static BridgeSHM* g_shm = NULL;
static AEffect*   g_fx  = NULL;
static double     g_sr  = 44100.0;
static int32_t    g_bs  = 512;

static intptr_t audioMaster(AEffect* fx, int32_t op, int32_t idx,
                            intptr_t val, void* ptr, float opt) {
    (void)fx; (void)idx; (void)val; (void)ptr; (void)opt;
    switch (op) {
        case audioMasterVersion:       return 2400;
        case audioMasterCurrentId:     return 0;
        case audioMasterGetSampleRate: return (intptr_t)g_sr;
        case audioMasterGetBlockSize:  return g_bs;
        case 6:  return 1;   /* wantMidi   */
        case 33: return 0;   /* processLevel */
        default: return 0;
    }
}

static void nap(long us) {
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

/* Apply any parameter values the plugin side updated lock-free. */
static void flush_params(void) {
    BridgeSHM* s = g_shm;
    uint32_t dirty = atomic_exchange(&s->paramDirty, 0);
    if (!dirty) return;
    for (int i = 0; i < BRIDGE_MAX_PARAM && i < g_fx->numParams; i++)
        if (dirty & (1u << i)) g_fx->setParameter(g_fx, i, s->params[i]);
}

static void handle(int cmd) {
    BridgeSHM* s = g_shm;
    AEffect* fx  = g_fx;
    flush_params();
    switch (cmd) {
    case CMD_PROCESS: {
        int n = s->nframes;
        if (n < 0) n = 0;
        if (n > BRIDGE_MAX_BLOCK) n = BRIDGE_MAX_BLOCK;
        float* in[BRIDGE_MAX_CH]  = { s->audio[0], s->audio[1] };
        float* out[BRIDGE_MAX_CH] = { s->audio[2], s->audio[3] };
        memset(out[0], 0, sizeof(float) * (size_t)n);
        memset(out[1], 0, sizeof(float) * (size_t)n);
        fx->processReplacing(fx, in, out, n);
        break;
    }
    case CMD_SETPARAM:
        fx->setParameter(fx, s->iarg, s->farg);
        break;
    case CMD_GETPARAM:
        s->farg = fx->getParameter(fx, s->iarg);
        break;
    case CMD_SETSR:
        g_sr = s->darg;
        fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, (float)s->darg);
        break;
    case CMD_SETBLOCK:
        g_bs = s->iarg;
        fx->dispatcher(fx, effSetBlockSize, 0, s->iarg, NULL, 0.f);
        break;
    case CMD_MAINS:
        fx->dispatcher(fx, effMainsChanged, 0, s->iarg, NULL, 0.f);
        fx->dispatcher(fx, s->iarg ? effStartProcess : effStopProcess, 0, 0, NULL, 0.f);
        break;
    case CMD_SETPROGRAM:
        fx->dispatcher(fx, effSetProgram, 0, s->iarg, NULL, 0.f);
        for (int i = 0; i < fx->numParams && i < BRIDGE_MAX_PARAM; i++)
            s->params[i] = fx->getParameter(fx, i);
        break;
    case CMD_PARAMTEXT:
        memset(s->str, 0, BRIDGE_STRLEN);
        memset(s->str2, 0, BRIDGE_STRLEN);
        fx->dispatcher(fx, effGetParamDisplay, s->iarg, 0, s->str, 0.f);
        fx->dispatcher(fx, effGetParamLabel,   s->iarg, 0, s->str2, 0.f);
        s->str[BRIDGE_STRLEN-1] = s->str2[BRIDGE_STRLEN-1] = 0;
        break;
    case CMD_PROGNAME:
        memset(s->str, 0, BRIDGE_STRLEN);
        fx->dispatcher(fx, effGetProgramNameIndexed, s->iarg, 0, s->str, 0.f);
        s->str[BRIDGE_STRLEN-1] = 0;
        break;
    case CMD_GETCHUNK: {
        void* p = NULL;
        intptr_t sz = fx->dispatcher(fx, effGetChunk, s->iarg, 0, &p, 0.f);
        if (sz > BRIDGE_MAX_CHUNK) sz = BRIDGE_MAX_CHUNK;
        if (p && sz > 0) memcpy(s->chunk, p, (size_t)sz);
        s->chunkSize = (int32_t)(sz > 0 ? sz : 0);
        break;
    }
    case CMD_SETCHUNK: {
        int32_t sz = s->chunkSize;
        if (sz > 0 && sz <= BRIDGE_MAX_CHUNK)
            fx->dispatcher(fx, effSetChunk, s->iarg, sz, s->chunk, 0.f);
        for (int i = 0; i < fx->numParams && i < BRIDGE_MAX_PARAM; i++)
            s->params[i] = fx->getParameter(fx, i);
        break;
    }
    default: break;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: helper <shmname> <vstpath>\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);

    int fd = shm_open(argv[1], O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 3; }
    g_shm = mmap(NULL, sizeof(BridgeSHM), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_shm == MAP_FAILED) { perror("mmap"); return 4; }
    BridgeSHM* s = g_shm;

    void* h = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror());
              atomic_store(&s->helperFailed, 1); return 5; }
    VstMainProc entry = (VstMainProc)dlsym(h, "VSTPluginMain");
    if (!entry) entry = (VstMainProc)dlsym(h, "main_macho");
    if (!entry) { atomic_store(&s->helperFailed, 1); return 6; }

    g_fx = entry(audioMaster);
    if (!g_fx || g_fx->magic != kEffectMagic) {
        atomic_store(&s->helperFailed, 1); return 7;
    }
    AEffect* fx = g_fx;

    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, (float)g_sr);
    fx->dispatcher(fx, effSetBlockSize, 0, g_bs, NULL, 0.f);

    s->numInputs   = fx->numInputs;
    s->numOutputs  = fx->numOutputs;
    s->numParams   = fx->numParams;
    s->numPrograms = fx->numPrograms;
    s->initialDelay= fx->initialDelay;
    s->effFlags    = fx->flags;
    s->uniqueID    = fx->uniqueID;
    for (int i = 0; i < fx->numParams && i < BRIDGE_MAX_PARAM; i++)
        s->params[i] = fx->getParameter(fx, i);

    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0.f);
    fx->dispatcher(fx, effStartProcess, 0, 0, NULL, 0.f);
    atomic_store(&s->helperReady, 1);

    uint32_t last = atomic_load(&s->reqSeq);
    long idle = 0;
    for (;;) {
        uint32_t r = atomic_load(&s->reqSeq);
        if (r != last) {
            int cmd = s->cmd;
            if (cmd == CMD_QUIT) { atomic_store(&s->ackSeq, r); break; }
            handle(cmd);
            last = r;
            atomic_store(&s->ackSeq, r);
            idle = 0;
        } else {
            /* Busy-wait briefly for low latency, then back off to stay cool. */
            if (idle < 40000) { idle++; __asm__ __volatile__("pause"); }
            else { flush_params(); nap(200); }
        }
        if (getppid() == 1) break;   /* parent (host) died */
    }

    fx->dispatcher(fx, effStopProcess, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effMainsChanged, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effClose, 0, 0, NULL, 0.f);
    return 0;
}
