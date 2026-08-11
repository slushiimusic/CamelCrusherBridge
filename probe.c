#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <dlfcn.h>
#include "vst2.h"

static intptr_t audioMaster(AEffect* fx, int32_t op, int32_t idx,
                            intptr_t val, void* ptr, float opt) {
    (void)fx;(void)idx;(void)val;(void)ptr;(void)opt;
    switch (op) {
        case 1: return 2400;
        case 23: return 44100;
        case 24: return 512;
        default: return 0;
    }
}

int main(int argc, char** argv) {
    void* h = dlopen(argc>1?argv[1]:"/Library/Audio/Plug-Ins/VST/CamelCrusher.vst/Contents/MacOS/CamelCrusher", RTLD_NOW);
    if (!h) { printf("dlopen fail: %s\n", dlerror()); return 1; }
    VstMainProc entry = (VstMainProc)dlsym(h, "VSTPluginMain");
    AEffect* fx = entry(audioMaster);
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, 44100.f);
    fx->dispatcher(fx, effSetBlockSize, 0, 512, NULL, 0.f);

    printf("uniqueID   = 0x%08x\n", fx->uniqueID);
    printf("version    = %d\n", fx->version);
    printf("numParams  = %d\n", fx->numParams);
    printf("numPrograms= %d\n", fx->numPrograms);
    printf("in/out     = %d/%d\n", fx->numInputs, fx->numOutputs);
    printf("initialDelay=%d\n", fx->initialDelay);
    printf("flags      = 0x%08x :", fx->flags);
    if (fx->flags & 1)       printf(" hasEditor");
    if (fx->flags & (1<<4))  printf(" canReplacing");
    if (fx->flags & (1<<5))  printf(" programChunks");
    if (fx->flags & (1<<8))  printf(" isSynth");
    if (fx->flags & (1<<9))  printf(" noSoundInStop");
    if (fx->flags & (1<<12)) printf(" canDoubleReplacing");
    printf("\n");
    printf("vstVersion = %ld\n", (long)fx->dispatcher(fx, effGetVstVersion,0,0,NULL,0.f));
    printf("category   = %ld\n", (long)fx->dispatcher(fx, effGetPlugCategory,0,0,NULL,0.f));

    ERect* r = NULL;
    fx->dispatcher(fx, effEditGetRect, 0, 0, &r, 0.f);
    if (r) printf("editor rect= %d x %d\n", r->right - r->left, r->bottom - r->top);
    else   printf("editor rect= (none reported)\n");

    printf("\n-- parameters --\n");
    for (int i=0;i<fx->numParams;i++){
        char n[128]={0}, l[128]={0}, d[128]={0};
        fx->dispatcher(fx, effGetParamName, i,0,n,0.f);
        fx->dispatcher(fx, effGetParamLabel, i,0,l,0.f);
        fx->dispatcher(fx, effGetParamDisplay, i,0,d,0.f);
        printf("  %2d  %-16s = %-10s %-8s (raw %.4f)\n", i, n, d, l, fx->getParameter(fx,i));
    }

    printf("\n-- programs --\n");
    for (int i=0;i<fx->numPrograms && i<24;i++){
        char pn[128]={0};
        if (!fx->dispatcher(fx, effGetProgramNameIndexed, i, 0, pn, 0.f))
            snprintf(pn,sizeof(pn),"(indexed unsupported)");
        printf("  %2d  %s\n", i, pn);
    }

    void* chunk=NULL;
    intptr_t cs = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0.f);
    printf("\nbank chunk size  = %ld\n", (long)cs);
    cs = fx->dispatcher(fx, effGetChunk, 1, 0, &chunk, 0.f);
    printf("prog chunk size  = %ld\n", (long)cs);

    const char* cds[] = {"receiveVstEvents","receiveVstMidiEvent","sendVstEvents",
                         "sendVstMidiEvent","bypass","2in2out",NULL};
    printf("\n-- canDo --\n");
    for (int i=0;cds[i];i++)
        printf("  %-22s %ld\n", cds[i], (long)fx->dispatcher(fx, effCanDo,0,0,(void*)cds[i],0.f));

    fx->dispatcher(fx, effClose,0,0,NULL,0.f);
    return 0;
}
