// Renders a deterministic test signal through a VST2 plugin and dumps the
// raw float32 output, so the bridge can be null-tested against the original.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include "vst2.h"

#define SR      44100.0
#define BLK     512
#define BLOCKS  200

static intptr_t am(AEffect* f, int32_t o, int32_t i, intptr_t v, void* p, float t) {
    (void)f;(void)i;(void)v;(void)p;(void)t;
    switch (o) { case 1: return 2400; case 23: return (intptr_t)SR;
                 case 24: return BLK; default: return 0; }
}

/* Deterministic pseudo-random + tonal test signal (identical on both sides). */
static uint32_t rng = 0x12345678u;
static float nextNoise(void) {
    rng = rng * 1664525u + 1013904223u;
    return ((float)((rng >> 8) & 0xFFFF) / 32768.0f) - 1.0f;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: dumper <binary> <out.raw> [program]\n"); return 2; }
    int program = (argc > 3) ? atoi(argv[3]) : 0;

    void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    VstMainProc entry = (VstMainProc)dlsym(h, "VSTPluginMain");
    if (!entry) { fprintf(stderr, "no entry\n"); return 1; }
    AEffect* fx = entry(am);
    if (!fx || fx->magic != kEffectMagic) { fprintf(stderr, "bad effect\n"); return 1; }

    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, (float)SR);
    fx->dispatcher(fx, effSetBlockSize, 0, BLK, NULL, 0.f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0.f);
    fx->dispatcher(fx, effSetProgram, 0, program, NULL, 0.f);

    /* mode: 0 = pin params, 1 = use the preset as-is, 2 = automate while rendering */
    int mode = (argc > 4) ? atoi(argv[4]) : 0;

    if (mode == 0) {
        static const float pv[12] = {
            1.0f,   /* DistOn        */ 0.62f, /* Mech    */ 0.81f, /* Tube   */
            1.0f,   /* FilterOn      */ 0.44f, /* Cutoff  */ 0.73f, /* Reso   */
            1.0f,   /* CompOn        */ 0.55f, /* Amount  */ 1.0f,  /* Phat   */
            1.0f,   /* MasterOn      */ 0.90f, /* Mix     */ 0.66f  /* Volume */
        };
        for (int i = 0; i < 12; i++) fx->setParameter(fx, i, pv[i]);
    }

    float inL[BLK], inR[BLK], outL[BLK], outR[BLK];
    float* in[2]  = { inL, inR };
    float* out[2] = { outL, outR };

    FILE* f = fopen(argv[2], "wb");
    if (!f) { perror("fopen"); return 1; }

    long n = 0;
    for (int b = 0; b < BLOCKS; b++) {
        if (mode == 2) {                     /* sweep Tube + Cutoff per block */
            float ph = (float)b / (float)BLOCKS;
            fx->setParameter(fx, 2, ph);
            fx->setParameter(fx, 4, 1.0f - ph);
        }
        for (int i = 0; i < BLK; i++, n++) {
            double t = (double)n / SR;
            float tone = 0.35f * (float)(sin(2*M_PI*110.0*t) + 0.5*sin(2*M_PI*523.25*t));
            float nz   = 0.10f * nextNoise();
            inL[i] = tone + nz;
            inR[i] = tone - nz;
        }
        memset(outL, 0, sizeof(outL));
        memset(outR, 0, sizeof(outR));
        fx->processReplacing(fx, in, out, BLK);
        fwrite(outL, sizeof(float), BLK, f);
        fwrite(outR, sizeof(float), BLK, f);
    }
    fclose(f);

    fx->dispatcher(fx, effMainsChanged, 0, 0, NULL, 0.f);
    fx->dispatcher(fx, effClose, 0, 0, NULL, 0.f);
    fprintf(stderr, "wrote %s (%d blocks x %d frames x 2ch, program %d)\n",
            argv[2], BLOCKS, BLK, program);
    return 0;
}
