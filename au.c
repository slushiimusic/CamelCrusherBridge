// Native arm64 Audio Unit (AUv2) front-end for the CamelCrusher bridge.
// Declares the original component identity (aumf / CaCr / CamA) so existing
// host projects that reference Camel Audio's AU re-link to this build.
#include <AudioUnit/AudioUnit.h>
#include <AudioUnit/AudioComponent.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>
#include <stdlib.h>
#include "bridge.h"

#define CC_NUM_PARAMS   12      /* the 12 meaningful params; 12..16 are "Unused" */
#define CC_MAX_LISTEN   32
#define CC_MAX_NOTIFY   16
#define CC_NUM_PRESETS  20

/* Private property used by the Cocoa view factory to recover the Bridge*. */
#define kCCProperty_Bridge 0x43436272 /* 'CCbr' */

extern const char* cc_param_name(int i);   /* from plugin.c's shared table */

typedef struct {
    AudioUnitPropertyID          pid;
    AudioUnitPropertyListenerProc proc;
    void*                        data;
} Listener;

typedef struct {
    AURenderCallbackStruct cb;
} Notify;

typedef struct {
    AudioComponentPlugInInterface iface;   /* MUST be first */
    AudioComponentInstance ci;
    Bridge   br;
    int      started;

    Float64  sampleRate;
    UInt32   maxFrames;
    int      initialized;
    OSStatus lastRenderError;
    UInt32   bypass;

    AURenderCallbackStruct inputCB;
    int      haveInputCB;
    AudioUnitConnection conn;      /* upstream AU feeding our input, if any */
    int      haveConn;

    AudioStreamBasicDescription fmtIn, fmtOut;

    Listener listeners[CC_MAX_LISTEN];
    int      nListeners;
    Notify   notifies[CC_MAX_NOTIFY];
    int      nNotifies;

    AUPreset  preset;
    CFStringRef presetNames[CC_NUM_PRESETS];

    /* scratch for pulling input and for hosts that pass null output buffers */
    float*   inBuf[2];
    float*   outBuf[2];
    UInt32   inBufFrames;
    AudioBufferList* pullABL;
} CCAU;

/* ------------------------------------------------------------------ utils */

static void asbd_init(AudioStreamBasicDescription* a, Float64 sr, UInt32 ch) {
    memset(a, 0, sizeof(*a));
    a->mSampleRate       = sr;
    a->mFormatID         = kAudioFormatLinearPCM;
    a->mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
                         | kAudioFormatFlagIsNonInterleaved;
    a->mBytesPerPacket   = 4;
    a->mFramesPerPacket  = 1;
    a->mBytesPerFrame    = 4;
    a->mChannelsPerFrame = ch;
    a->mBitsPerChannel   = 32;
}

static void notify_listeners(CCAU* u, AudioUnitPropertyID pid,
                             AudioUnitScope scope, AudioUnitElement elem) {
    for (int i = 0; i < u->nListeners; i++)
        if (u->listeners[i].pid == pid && u->listeners[i].proc)
            u->listeners[i].proc(u->listeners[i].data, u->ci, pid, scope, elem);
}

static int ensure_scratch(CCAU* u, UInt32 frames) {
    if (u->inBufFrames >= frames && u->inBuf[0]) return 1;
    free(u->inBuf[0]);  free(u->inBuf[1]);
    free(u->outBuf[0]); free(u->outBuf[1]); free(u->pullABL);
    u->inBuf[0]  = (float*)calloc(frames, sizeof(float));
    u->inBuf[1]  = (float*)calloc(frames, sizeof(float));
    u->outBuf[0] = (float*)calloc(frames, sizeof(float));
    u->outBuf[1] = (float*)calloc(frames, sizeof(float));
    u->pullABL   = (AudioBufferList*)calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer));
    if (!u->inBuf[0] || !u->inBuf[1] || !u->outBuf[0] || !u->outBuf[1] || !u->pullABL) {
        u->inBufFrames = 0; return 0;
    }
    u->inBufFrames = frames;
    return 1;
}

/* ------------------------------------------------------------ properties */

static OSStatus CCGetPropertyInfo(void* self, AudioUnitPropertyID pid,
                                  AudioUnitScope scope, AudioUnitElement elem,
                                  UInt32* outSize, Boolean* outWritable) {
    CCAU* u = (CCAU*)self;
    UInt32 size = 0; Boolean w = false;

    /* These exist only in the global scope; reporting them elsewhere makes
       hosts believe in per-bus copies that do not exist. */
    switch (pid) {
    case kAudioUnitProperty_ClassInfo:
    case kAudioUnitProperty_Latency:
    case kAudioUnitProperty_TailTime:
    case kAudioUnitProperty_BypassEffect:
    case kAudioUnitProperty_MaximumFramesPerSlice:
    case kAudioUnitProperty_LastRenderError:
    case kAudioUnitProperty_SupportedNumChannels:
    case kAudioUnitProperty_FactoryPresets:
    case kAudioUnitProperty_PresentPreset:
    case kAudioUnitProperty_CocoaUI:
    case kAudioUnitProperty_InPlaceProcessing:
    case kCCProperty_Bridge:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        break;
    default: break;
    }

    switch (pid) {
    case kAudioUnitProperty_ClassInfo:          size = sizeof(CFPropertyListRef); w = true; break;
    case kAudioUnitProperty_SampleRate:         size = sizeof(Float64);  w = true; break;
    /* Parameters live only in the global scope. Advertising a non-zero size
       in other scopes makes hosts probe for note/group parameters we do not
       publish, which then fail with kAudioUnitErr_InvalidParameter. */
    case kAudioUnitProperty_ParameterList:
        size = (scope == kAudioUnitScope_Global)
             ? sizeof(AudioUnitParameterID) * CC_NUM_PARAMS : 0;
        break;
    case kAudioUnitProperty_ParameterInfo:
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        size = sizeof(AudioUnitParameterInfo);
        break;
    case kAudioUnitProperty_StreamFormat:       size = sizeof(AudioStreamBasicDescription); w = true; break;
    case kAudioUnitProperty_ElementCount:       size = sizeof(UInt32); break;
    case kAudioUnitProperty_Latency:            size = sizeof(Float64); break;
    case kAudioUnitProperty_TailTime:           size = sizeof(Float64); break;
    case kAudioUnitProperty_SupportedNumChannels: size = sizeof(AUChannelInfo); break;
    case kAudioUnitProperty_MaximumFramesPerSlice: size = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_LastRenderError:    size = sizeof(OSStatus); break;
    case kAudioUnitProperty_SetRenderCallback:  size = sizeof(AURenderCallbackStruct); w = true; break;
    case kAudioUnitProperty_MakeConnection:
        if (scope != kAudioUnitScope_Input) return kAudioUnitErr_InvalidScope;
        size = sizeof(AudioUnitConnection); w = true; break;
    case kAudioUnitProperty_FactoryPresets:     size = sizeof(CFArrayRef); break;
    case kAudioUnitProperty_PresentPreset:      size = sizeof(AUPreset); w = true; break;
    case kAudioUnitProperty_BypassEffect:       size = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_InPlaceProcessing:  size = sizeof(UInt32); w = true; break;
    case kAudioUnitProperty_CocoaUI:            size = sizeof(AudioUnitCocoaViewInfo); break;
    case kCCProperty_Bridge:                    size = sizeof(void*); break;
    default: return kAudioUnitErr_InvalidProperty;
    }
    (void)scope; (void)elem;
    if (outSize)     *outSize = size;
    if (outWritable) *outWritable = w;
    return noErr;
}

static OSStatus CCGetProperty(void* self, AudioUnitPropertyID pid,
                              AudioUnitScope scope, AudioUnitElement elem,
                              void* outData, UInt32* ioSize) {
    CCAU* u = (CCAU*)self;
    UInt32 want = 0; Boolean w;
    OSStatus st = CCGetPropertyInfo(self, pid, scope, elem, &want, &w);
    if (st != noErr) return st;
    if (!outData || !ioSize) return kAudio_ParamError;
    if (*ioSize < want) return kAudioUnitErr_InvalidPropertyValue;
    *ioSize = want;

    switch (pid) {
    case kAudioUnitProperty_SampleRate:
        *(Float64*)outData = u->sampleRate; return noErr;

    case kAudioUnitProperty_ParameterList: {
        AudioUnitParameterID* ids = (AudioUnitParameterID*)outData;
        if (scope != kAudioUnitScope_Global) { *ioSize = 0; return noErr; }
        for (int i = 0; i < CC_NUM_PARAMS; i++) ids[i] = (AudioUnitParameterID)i;
        return noErr;
    }
    case kAudioUnitProperty_ParameterInfo: {
        if (scope != kAudioUnitScope_Global || elem >= CC_NUM_PARAMS)
            return kAudioUnitErr_InvalidParameter;
        AudioUnitParameterInfo* pi = (AudioUnitParameterInfo*)outData;
        memset(pi, 0, sizeof(*pi));
        pi->cfNameString = CFStringCreateWithCString(NULL, cc_param_name((int)elem),
                                                     kCFStringEncodingUTF8);
        pi->flags = kAudioUnitParameterFlag_HasCFNameString
                  | kAudioUnitParameterFlag_CFNameRelease
                  | kAudioUnitParameterFlag_IsReadable
                  | kAudioUnitParameterFlag_IsWritable;
        /* The hosted plugin exposes everything as normalised 0..1. Toggles are
           surfaced as real booleans so hosts draw switches, not sliders. */
        int isToggle = (elem == 0 || elem == 3 || elem == 6 || elem == 8 || elem == 9);
        pi->unit         = isToggle ? kAudioUnitParameterUnit_Boolean
                                    : kAudioUnitParameterUnit_Generic;
        pi->minValue     = 0.0f;
        pi->maxValue     = 1.0f;
        pi->defaultValue = bridge_get_param(&u->br, (int)elem);
        return noErr;
    }
    case kAudioUnitProperty_StreamFormat:
        *(AudioStreamBasicDescription*)outData =
            (scope == kAudioUnitScope_Input) ? u->fmtIn : u->fmtOut;
        return noErr;

    case kAudioUnitProperty_ElementCount:
        *(UInt32*)outData = (scope == kAudioUnitScope_Global) ? 1 : 1;
        return noErr;

    case kAudioUnitProperty_Latency:  *(Float64*)outData = 0.0; return noErr;
    case kAudioUnitProperty_TailTime: *(Float64*)outData = 0.0; return noErr;

    case kAudioUnitProperty_SupportedNumChannels: {
        AUChannelInfo* ci = (AUChannelInfo*)outData;
        ci->inChannels = 2; ci->outChannels = 2;
        return noErr;
    }
    case kAudioUnitProperty_MaximumFramesPerSlice:
        *(UInt32*)outData = u->maxFrames; return noErr;
    case kAudioUnitProperty_LastRenderError:
        *(OSStatus*)outData = u->lastRenderError; u->lastRenderError = noErr; return noErr;
    case kAudioUnitProperty_SetRenderCallback:
        *(AURenderCallbackStruct*)outData = u->inputCB; return noErr;
    case kAudioUnitProperty_MakeConnection:
        *(AudioUnitConnection*)outData = u->conn; return noErr;
    case kAudioUnitProperty_BypassEffect:
        *(UInt32*)outData = u->bypass; return noErr;
    case kAudioUnitProperty_InPlaceProcessing:
        *(UInt32*)outData = 0; return noErr;

    case kAudioUnitProperty_FactoryPresets: {
        CFMutableArrayRef arr = CFArrayCreateMutable(NULL, CC_NUM_PRESETS, NULL);
        static AUPreset presets[CC_NUM_PRESETS];
        for (int i = 0; i < CC_NUM_PRESETS; i++) {
            presets[i].presetNumber = i;
            presets[i].presetName   = u->presetNames[i];
            CFArrayAppendValue(arr, &presets[i]);
        }
        *(CFArrayRef*)outData = arr;      /* caller releases */
        return noErr;
    }
    case kAudioUnitProperty_PresentPreset:
        *(AUPreset*)outData = u->preset;
        if (u->preset.presetName) CFRetain(u->preset.presetName);
        return noErr;

    case kAudioUnitProperty_ClassInfo: {
        CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        SInt32 ver = 0, type = 'aumf', sub = 'CaCr', man = 'CamA';
        #define PUTNUM(k, v) do { CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &(v)); \
                                  CFDictionarySetValue(d, CFSTR(k), n); CFRelease(n); } while (0)
        PUTNUM(kAUPresetVersionKey, ver);
        PUTNUM(kAUPresetTypeKey, type);
        PUTNUM(kAUPresetSubtypeKey, sub);
        PUTNUM(kAUPresetManufacturerKey, man);
        #undef PUTNUM
        CFDictionarySetValue(d, CFSTR(kAUPresetNameKey),
                             u->preset.presetName ? u->preset.presetName : CFSTR("Untitled"));
        /* Opaque state straight from the hosted plugin. */
        int n = 0;
        const void* blob = bridge_get_chunk(&u->br, &n);
        if (blob && n > 0) {
            CFDataRef data = CFDataCreate(NULL, (const UInt8*)blob, n);
            CFDictionarySetValue(d, CFSTR("vstdata"), data);
            CFRelease(data);
        }
        *(CFPropertyListRef*)outData = d;   /* caller releases */
        return noErr;
    }
    case kAudioUnitProperty_CocoaUI: {
        AudioUnitCocoaViewInfo* vi = (AudioUnitCocoaViewInfo*)outData;
        CFBundleRef b = CFBundleGetBundleWithIdentifier(CFSTR("com.camelaudio.au.CamelCrusher"));
        if (!b) return kAudioUnitErr_InvalidProperty;
        vi->mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(b);
        vi->mCocoaAUViewClass[0] = CFStringCreateCopy(NULL, CFSTR("CCAUViewFactory"));
        return noErr;
    }
    case kCCProperty_Bridge:
        *(void**)outData = &u->br; return noErr;
    }
    return kAudioUnitErr_InvalidProperty;
}

static OSStatus CCSetProperty(void* self, AudioUnitPropertyID pid,
                              AudioUnitScope scope, AudioUnitElement elem,
                              const void* inData, UInt32 inSize) {
    CCAU* u = (CCAU*)self;
    (void)elem;
    switch (pid) {
    case kAudioUnitProperty_SampleRate: {
        if (inSize < sizeof(Float64)) return kAudioUnitErr_InvalidPropertyValue;
        Float64 sr = *(const Float64*)inData;
        if (sr <= 0) return kAudioUnitErr_InvalidPropertyValue;
        u->sampleRate = sr;
        u->fmtIn.mSampleRate = u->fmtOut.mSampleRate = sr;
        bridge_set_samplerate(&u->br, sr);
        notify_listeners(u, kAudioUnitProperty_SampleRate, scope, 0);
        return noErr;
    }
    case kAudioUnitProperty_StreamFormat: {
        if (inSize < sizeof(AudioStreamBasicDescription)) return kAudioUnitErr_InvalidPropertyValue;
        const AudioStreamBasicDescription* a = (const AudioStreamBasicDescription*)inData;
        if (a->mFormatID != kAudioFormatLinearPCM) return kAudioUnitErr_FormatNotSupported;
        if (!(a->mFormatFlags & kAudioFormatFlagIsFloat) || a->mBitsPerChannel != 32)
            return kAudioUnitErr_FormatNotSupported;
        if (a->mChannelsPerFrame != 2) return kAudioUnitErr_FormatNotSupported;
        if (u->initialized) return kAudioUnitErr_Initialized;
        if (scope == kAudioUnitScope_Input) u->fmtIn = *a; else u->fmtOut = *a;
        u->sampleRate = a->mSampleRate;
        bridge_set_samplerate(&u->br, a->mSampleRate);
        notify_listeners(u, kAudioUnitProperty_StreamFormat, scope, 0);
        return noErr;
    }
    case kAudioUnitProperty_MaximumFramesPerSlice: {
        if (inSize < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
        UInt32 mf = *(const UInt32*)inData;
        if (mf > BRIDGE_MAX_BLOCK) return kAudioUnitErr_InvalidPropertyValue;
        u->maxFrames = mf;
        ensure_scratch(u, mf);
        bridge_set_blocksize(&u->br, (int)mf);
        notify_listeners(u, kAudioUnitProperty_MaximumFramesPerSlice,
                         kAudioUnitScope_Global, 0);
        return noErr;
    }
    case kAudioUnitProperty_SetRenderCallback:
        if (inSize < sizeof(AURenderCallbackStruct)) return kAudioUnitErr_InvalidPropertyValue;
        u->inputCB = *(const AURenderCallbackStruct*)inData;
        u->haveInputCB = 1;
        u->haveConn = 0;               /* a callback supersedes any connection */
        return noErr;

    case kAudioUnitProperty_MakeConnection: {
        if (inSize < sizeof(AudioUnitConnection)) return kAudioUnitErr_InvalidPropertyValue;
        const AudioUnitConnection* c = (const AudioUnitConnection*)inData;
        u->conn = *c;
        u->haveConn = (c->sourceAudioUnit != NULL);
        if (u->haveConn) u->haveInputCB = 0;
        return noErr;
    }

    case kAudioUnitProperty_BypassEffect:
        if (inSize < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
        u->bypass = *(const UInt32*)inData;
        return noErr;

    case kAudioUnitProperty_InPlaceProcessing:
        return noErr;      /* accepted, but we always render out-of-place */

    case kAudioUnitProperty_PresentPreset: {
        if (inSize < sizeof(AUPreset)) return kAudioUnitErr_InvalidPropertyValue;
        const AUPreset* p = (const AUPreset*)inData;
        if (p->presetNumber >= 0 && p->presetNumber < CC_NUM_PRESETS) {
            bridge_set_program(&u->br, p->presetNumber);
            if (u->preset.presetName) CFRelease(u->preset.presetName);
            u->preset.presetNumber = p->presetNumber;
            u->preset.presetName   = CFRetain(u->presetNames[p->presetNumber]);
            notify_listeners(u, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
            notify_listeners(u, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0);
        } else {
            if (u->preset.presetName) CFRelease(u->preset.presetName);
            u->preset.presetNumber = -1;
            u->preset.presetName   = p->presetName ? CFRetain(p->presetName) : CFSTR("Untitled");
        }
        return noErr;
    }
    case kAudioUnitProperty_ClassInfo: {
        if (inSize < sizeof(CFPropertyListRef)) return kAudioUnitErr_InvalidPropertyValue;
        CFDictionaryRef d = *(const CFDictionaryRef*)inData;
        if (!d || CFGetTypeID(d) != CFDictionaryGetTypeID()) return kAudioUnitErr_InvalidPropertyValue;
        CFDataRef data = (CFDataRef)CFDictionaryGetValue(d, CFSTR("vstdata"));
        if (data && CFGetTypeID(data) == CFDataGetTypeID())
            bridge_set_chunk(&u->br, CFDataGetBytePtr(data), (int)CFDataGetLength(data));
        notify_listeners(u, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0);
        return noErr;
    }
    }
    return kAudioUnitErr_InvalidProperty;
}

/* -------------------------------------------------------------- lifecycle */

static OSStatus CCInitialize(void* self) {
    CCAU* u = (CCAU*)self;
    if (u->fmtIn.mChannelsPerFrame != 2 || u->fmtOut.mChannelsPerFrame != 2)
        return kAudioUnitErr_FormatNotSupported;
    if (!ensure_scratch(u, u->maxFrames)) return kAudio_MemFullError;
    bridge_set_samplerate(&u->br, u->sampleRate);
    bridge_set_blocksize(&u->br, (int)u->maxFrames);
    bridge_set_mains(&u->br, 1);
    u->initialized = 1;
    return noErr;
}

static OSStatus CCUninitialize(void* self) {
    CCAU* u = (CCAU*)self;
    if (u->initialized) { bridge_set_mains(&u->br, 0); u->initialized = 0; }
    return noErr;
}

static OSStatus CCReset(void* self, AudioUnitScope s, AudioUnitElement e) {
    CCAU* u = (CCAU*)self; (void)s; (void)e;
    bridge_set_mains(&u->br, 0);
    bridge_set_mains(&u->br, 1);
    return noErr;
}

/* ------------------------------------------------------------- parameters */

static OSStatus CCGetParameter(void* self, AudioUnitParameterID pid,
                               AudioUnitScope scope, AudioUnitElement elem,
                               AudioUnitParameterValue* outValue) {
    CCAU* u = (CCAU*)self; (void)elem;
    if (scope != kAudioUnitScope_Global || pid >= CC_NUM_PARAMS)
        return kAudioUnitErr_InvalidParameter;
    if (!outValue) return kAudio_ParamError;
    *outValue = bridge_get_param(&u->br, (int)pid);
    return noErr;
}

static OSStatus CCSetParameter(void* self, AudioUnitParameterID pid,
                               AudioUnitScope scope, AudioUnitElement elem,
                               AudioUnitParameterValue value, UInt32 offset) {
    CCAU* u = (CCAU*)self; (void)elem; (void)offset;
    if (scope != kAudioUnitScope_Global || pid >= CC_NUM_PARAMS)
        return kAudioUnitErr_InvalidParameter;
    if (value < 0.f) value = 0.f;
    if (value > 1.f) value = 1.f;
    bridge_set_param(&u->br, (int)pid, value);
    return noErr;
}

static OSStatus CCScheduleParameters(void* self, const AudioUnitParameterEvent* ev, UInt32 n) {
    CCAU* u = (CCAU*)self;
    for (UInt32 i = 0; i < n; i++) {
        if (ev[i].scope != kAudioUnitScope_Global) continue;
        if (ev[i].parameter >= CC_NUM_PARAMS) continue;
        /* Ramps are flattened to their end value: the hosted DSP takes one
           value per render call, matching the original VST behaviour. */
        float v = (ev[i].eventType == kParameterEvent_Immediate)
                ? ev[i].eventValues.immediate.value
                : ev[i].eventValues.ramp.endValue;
        bridge_set_param(&u->br, (int)ev[i].parameter, v);
    }
    return noErr;
}

/* ----------------------------------------------------------------- render */

static OSStatus CCRender(void* self, AudioUnitRenderActionFlags* ioFlags,
                         const AudioTimeStamp* ts, UInt32 bus,
                         UInt32 frames, AudioBufferList* io) {
    CCAU* u = (CCAU*)self;
    AudioUnitRenderActionFlags flags = ioFlags ? *ioFlags : 0;
    (void)bus;

    if (!u->initialized) return kAudioUnitErr_Uninitialized;
    if (frames > u->maxFrames || !io || io->mNumberBuffers < 1)
        return kAudioUnitErr_TooManyFramesToProcess;

    for (int i = 0; i < u->nNotifies; i++) {
        AudioUnitRenderActionFlags f = flags | kAudioUnitRenderAction_PreRender;
        u->notifies[i].cb.inputProc(u->notifies[i].cb.inputProcRefCon, &f, ts, bus, frames, io);
    }

    /* Pull upstream audio into our own non-interleaved scratch. */
    float* src[2] = { u->inBuf[0], u->inBuf[1] };
    if ((u->haveInputCB && u->inputCB.inputProc) || u->haveConn) {
        AudioBufferList* abl = u->pullABL;
        abl->mNumberBuffers = 2;
        abl->mBuffers[0].mNumberChannels = 1;
        abl->mBuffers[0].mDataByteSize   = frames * sizeof(float);
        abl->mBuffers[0].mData           = u->inBuf[0];
        abl->mBuffers[1].mNumberChannels = 1;
        abl->mBuffers[1].mDataByteSize   = frames * sizeof(float);
        abl->mBuffers[1].mData           = u->inBuf[1];
        AudioUnitRenderActionFlags f = 0;
        OSStatus st = u->haveConn
            ? AudioUnitRender(u->conn.sourceAudioUnit, &f, ts,
                              u->conn.sourceOutputNumber, frames, abl)
            : u->inputCB.inputProc(u->inputCB.inputProcRefCon, &f, ts, 0, frames, abl);
        if (st != noErr) { u->lastRenderError = st; return st; }
        /* The callback may hand back its own buffers rather than filling ours. */
        if (abl->mBuffers[0].mData) src[0] = (float*)abl->mBuffers[0].mData;
        if (abl->mNumberBuffers > 1 && abl->mBuffers[1].mData)
            src[1] = (float*)abl->mBuffers[1].mData;
        else src[1] = src[0];
    } else {
        memset(u->inBuf[0], 0, frames * sizeof(float));
        memset(u->inBuf[1], 0, frames * sizeof(float));
    }

    /* A host may hand us buffers with mData == NULL, meaning "use your own
       memory and tell me where it is". Satisfy that before reading them. */
    for (UInt32 i = 0; i < io->mNumberBuffers && i < 2; i++) {
        if (!io->mBuffers[i].mData) {
            io->mBuffers[i].mData         = u->outBuf[i];
            io->mBuffers[i].mDataByteSize = frames * (UInt32)sizeof(float);
        }
    }
    float* dst[2];
    dst[0] = (float*)io->mBuffers[0].mData;
    dst[1] = (io->mNumberBuffers > 1) ? (float*)io->mBuffers[1].mData : dst[0];
    if (!dst[0]) return kAudio_ParamError;
    if (!dst[1]) dst[1] = dst[0];

    if (u->bypass) {
        if (dst[0] != src[0]) memcpy(dst[0], src[0], frames * sizeof(float));
        if (dst[1] != src[1]) memcpy(dst[1], src[1], frames * sizeof(float));
    } else {
        bridge_process(&u->br, src, dst, (int)frames);
    }

    for (int i = 0; i < u->nNotifies; i++) {
        AudioUnitRenderActionFlags f = flags | kAudioUnitRenderAction_PostRender;
        u->notifies[i].cb.inputProc(u->notifies[i].cb.inputProcRefCon, &f, ts, bus, frames, io);
    }
    return noErr;
}

/* ------------------------------------------------------- listeners / MIDI */

static OSStatus CCAddPropertyListener(void* self, AudioUnitPropertyID pid,
                                      AudioUnitPropertyListenerProc proc, void* data) {
    CCAU* u = (CCAU*)self;
    if (u->nListeners >= CC_MAX_LISTEN) return kAudio_MemFullError;
    u->listeners[u->nListeners].pid  = pid;
    u->listeners[u->nListeners].proc = proc;
    u->listeners[u->nListeners].data = data;
    u->nListeners++;
    return noErr;
}

static OSStatus CCRemovePropertyListenerWithUserData(void* self, AudioUnitPropertyID pid,
                                                     AudioUnitPropertyListenerProc proc,
                                                     void* data) {
    CCAU* u = (CCAU*)self;
    for (int i = 0; i < u->nListeners; i++) {
        if (u->listeners[i].pid == pid && u->listeners[i].proc == proc &&
            (!data || u->listeners[i].data == data)) {
            u->listeners[i] = u->listeners[--u->nListeners];
            i--;
        }
    }
    return noErr;
}

static OSStatus CCRemovePropertyListener(void* self, AudioUnitPropertyID pid,
                                         AudioUnitPropertyListenerProc proc) {
    return CCRemovePropertyListenerWithUserData(self, pid, proc, NULL);
}

static OSStatus CCAddRenderNotify(void* self, AURenderCallback proc, void* data) {
    CCAU* u = (CCAU*)self;
    if (u->nNotifies >= CC_MAX_NOTIFY) return kAudio_MemFullError;
    u->notifies[u->nNotifies].cb.inputProc = proc;
    u->notifies[u->nNotifies].cb.inputProcRefCon = data;
    u->nNotifies++;
    return noErr;
}

static OSStatus CCRemoveRenderNotify(void* self, AURenderCallback proc, void* data) {
    CCAU* u = (CCAU*)self;
    for (int i = 0; i < u->nNotifies; i++)
        if (u->notifies[i].cb.inputProc == proc && u->notifies[i].cb.inputProcRefCon == data)
            u->notifies[i] = u->notifies[--u->nNotifies];
    return noErr;
}

/* Registered as a music effect ('aumf') like the original, so hosts may send
   MIDI. The hosted DSP has no MIDI-driven behaviour, so events are accepted
   and discarded rather than rejected, which keeps hosts from erroring. */
static OSStatus CCMIDIEvent(void* self, UInt32 status, UInt32 d1, UInt32 d2, UInt32 off) {
    (void)self; (void)status; (void)d1; (void)d2; (void)off;
    return noErr;
}
static OSStatus CCSysEx(void* self, const UInt8* data, UInt32 len) {
    (void)self; (void)data; (void)len; return noErr;
}

/* ------------------------------------------------------- open/close/lookup */

static OSStatus CCClose(void* self) {
    CCAU* u = (CCAU*)self;
    if (!u) return noErr;
    if (u->initialized) bridge_set_mains(&u->br, 0);
    if (u->started) bridge_stop(&u->br);
    for (int i = 0; i < CC_NUM_PRESETS; i++)
        if (u->presetNames[i]) CFRelease(u->presetNames[i]);
    if (u->preset.presetName) CFRelease(u->preset.presetName);
    free(u->inBuf[0]);  free(u->inBuf[1]);
    free(u->outBuf[0]); free(u->outBuf[1]); free(u->pullABL);
    free(u);
    return noErr;
}

static OSStatus CCOpen(void* self, AudioComponentInstance ci) {
    CCAU* u = (CCAU*)self;
    u->ci         = ci;
    u->sampleRate = 44100.0;
    u->maxFrames  = 1156;              /* Apple's default; hosts override */
    u->bypass     = 0;
    u->lastRenderError = noErr;
    asbd_init(&u->fmtIn,  u->sampleRate, 2);
    asbd_init(&u->fmtOut, u->sampleRate, 2);

    if (!bridge_start(&u->br)) return kAudioUnitErr_FailedInitialization;
    u->started = 1;

    for (int i = 0; i < CC_NUM_PRESETS; i++) {
        const char* nm = bridge_program_name(&u->br, i);
        u->presetNames[i] = CFStringCreateWithCString(NULL, nm && *nm ? nm : "Preset",
                                                      kCFStringEncodingUTF8);
    }
    u->preset.presetNumber = 0;
    u->preset.presetName   = CFRetain(u->presetNames[0]);
    ensure_scratch(u, u->maxFrames);
    return noErr;
}

static AudioComponentMethod CCLookup(SInt16 sel) {
    switch (sel) {
    case kAudioUnitInitializeSelect:        return (AudioComponentMethod)CCInitialize;
    case kAudioUnitUninitializeSelect:      return (AudioComponentMethod)CCUninitialize;
    case kAudioUnitGetPropertyInfoSelect:   return (AudioComponentMethod)CCGetPropertyInfo;
    case kAudioUnitGetPropertySelect:       return (AudioComponentMethod)CCGetProperty;
    case kAudioUnitSetPropertySelect:       return (AudioComponentMethod)CCSetProperty;
    case kAudioUnitAddPropertyListenerSelect:    return (AudioComponentMethod)CCAddPropertyListener;
    case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)CCRemovePropertyListener;
    case kAudioUnitRemovePropertyListenerWithUserDataSelect:
                                            return (AudioComponentMethod)CCRemovePropertyListenerWithUserData;
    case kAudioUnitAddRenderNotifySelect:   return (AudioComponentMethod)CCAddRenderNotify;
    case kAudioUnitRemoveRenderNotifySelect:return (AudioComponentMethod)CCRemoveRenderNotify;
    case kAudioUnitGetParameterSelect:      return (AudioComponentMethod)CCGetParameter;
    case kAudioUnitSetParameterSelect:      return (AudioComponentMethod)CCSetParameter;
    case kAudioUnitScheduleParametersSelect:return (AudioComponentMethod)CCScheduleParameters;
    case kAudioUnitRenderSelect:            return (AudioComponentMethod)CCRender;
    case kAudioUnitResetSelect:             return (AudioComponentMethod)CCReset;
    case kMusicDeviceMIDIEventSelect:       return (AudioComponentMethod)CCMIDIEvent;
    case kMusicDeviceSysExSelect:           return (AudioComponentMethod)CCSysEx;
    default: return NULL;
    }
}

/* Factory named in Info.plist's AudioComponents entry. */
AudioComponentPlugInInterface* CamelCrusherAUFactory(const AudioComponentDescription* desc);
AudioComponentPlugInInterface* CamelCrusherAUFactory(const AudioComponentDescription* desc) {
    (void)desc;
    CCAU* u = (CCAU*)calloc(1, sizeof(CCAU));
    if (!u) return NULL;
    u->iface.Open     = CCOpen;
    u->iface.Close    = CCClose;
    u->iface.Lookup   = CCLookup;
    u->iface.reserved = NULL;
    return &u->iface;
}
