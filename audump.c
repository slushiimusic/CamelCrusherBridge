// Renders the same deterministic signal through the Audio Unit so its output
// can be null-tested against the original Intel VST.
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR      44100.0
#define BLK     512
#define BLOCKS  200

static uint32_t rng = 0x12345678u;
static float nextNoise(void) {
    rng = rng * 1664525u + 1013904223u;
    return ((float)((rng >> 8) & 0xFFFF) / 32768.0f) - 1.0f;
}

static long gN = 0;
static OSStatus feed(void* ref, AudioUnitRenderActionFlags* flags,
                     const AudioTimeStamp* ts, UInt32 bus, UInt32 frames,
                     AudioBufferList* io) {
    (void)ref; (void)flags; (void)ts; (void)bus;
    float* L = (float*)io->mBuffers[0].mData;
    float* R = (io->mNumberBuffers > 1) ? (float*)io->mBuffers[1].mData : L;
    for (UInt32 i = 0; i < frames; i++, gN++) {
        double t = (double)gN / SR;
        float tone = 0.35f * (float)(sin(2*M_PI*110.0*t) + 0.5*sin(2*M_PI*523.25*t));
        float nz   = 0.10f * nextNoise();
        L[i] = tone + nz;
        R[i] = tone - nz;
    }
    return noErr;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: audump <out.raw> [program] [mode]\n"); return 2; }
    int program = (argc > 2) ? atoi(argv[2]) : 0;
    int mode    = (argc > 3) ? atoi(argv[3]) : 0;

    AudioComponentDescription d = { 'aumf', 'CaCr', 'CamA', 0, 0 };
    AudioComponent comp = AudioComponentFindNext(NULL, &d);
    if (!comp) { fprintf(stderr, "component not found\n"); return 1; }
    AudioUnit au;
    if (AudioComponentInstanceNew(comp, &au) != noErr) { fprintf(stderr, "new failed\n"); return 1; }

    AudioStreamBasicDescription f = {0};
    f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
                   | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
    f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Input, 0, &f, sizeof(f));
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Output, 0, &f, sizeof(f));
    UInt32 mf = BLK;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice,
                         kAudioUnitScope_Global, 0, &mf, sizeof(mf));
    AURenderCallbackStruct cb = { feed, NULL };
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback,
                         kAudioUnitScope_Input, 0, &cb, sizeof(cb));

    if (AudioUnitInitialize(au) != noErr) { fprintf(stderr, "init failed\n"); return 1; }

    AUPreset p = { program, NULL };
    AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset,
                         kAudioUnitScope_Global, 0, &p, sizeof(p));

    if (mode == 0) {
        static const float pv[12] = { 1.0f,0.62f,0.81f, 1.0f,0.44f,0.73f,
                                      1.0f,0.55f,1.0f,  1.0f,0.90f,0.66f };
        for (int i = 0; i < 12; i++)
            AudioUnitSetParameter(au, i, kAudioUnitScope_Global, 0, pv[i], 0);
    }

    float L[BLK], R[BLK];
    char ablmem[sizeof(AudioBufferList) + sizeof(AudioBuffer)];
    AudioBufferList* abl = (AudioBufferList*)ablmem;

    FILE* out = fopen(argv[1], "wb");
    if (!out) { perror("fopen"); return 1; }

    AudioTimeStamp ts = {0};
    ts.mFlags = kAudioTimeStampSampleTimeValid;

    for (int b = 0; b < BLOCKS; b++) {
        if (mode == 2) {
            float ph = (float)b / (float)BLOCKS;
            AudioUnitSetParameter(au, 2, kAudioUnitScope_Global, 0, ph, 0);
            AudioUnitSetParameter(au, 4, kAudioUnitScope_Global, 0, 1.0f - ph, 0);
        }
        abl->mNumberBuffers = 2;
        abl->mBuffers[0].mNumberChannels = 1;
        abl->mBuffers[0].mDataByteSize = BLK * sizeof(float);
        abl->mBuffers[0].mData = L;
        abl->mBuffers[1].mNumberChannels = 1;
        abl->mBuffers[1].mDataByteSize = BLK * sizeof(float);
        abl->mBuffers[1].mData = R;
        AudioUnitRenderActionFlags fl = 0;
        ts.mSampleTime = (Float64)(b * BLK);
        OSStatus st = AudioUnitRender(au, &fl, &ts, 0, BLK, abl);
        if (st != noErr) { fprintf(stderr, "render err %d at block %d\n", (int)st, b); return 1; }
        fwrite(abl->mBuffers[0].mData, sizeof(float), BLK, out);
        fwrite(abl->mBuffers[1].mData, sizeof(float), BLK, out);
    }
    fclose(out);
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    fprintf(stderr, "wrote %s\n", argv[1]);
    return 0;
}
