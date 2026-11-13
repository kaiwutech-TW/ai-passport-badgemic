/*
 * Copyright (c) 2026 BadgeMic contributors
 * SPDX-License-Identifier: MIT
 *
 * BadgeMic loopback virtual audio device: a minimal AudioServerPlugIn
 * (CoreAudio HAL plug-in) written from Apple's public headers
 * (CoreAudio/AudioServerPlugIn.h, AudioHardwareBase.h) only.
 *
 * Object tree:
 *   1  plug-in
 *   2  device "BadgeMic Microphone"
 *   3    input stream  (2ch float32, interleaved)
 *   4    output stream (2ch float32, interleaved)
 *
 * Whatever clients write to the output stream is placed in a ring buffer
 * indexed by device sample time and is returned on the input stream at the
 * same sample time. Anything not written reads back as silence.
 */

#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

enum {
    kObj_PlugIn       = kAudioObjectPlugInObject, /* 1 */
    kObj_Device       = 2,
    kObj_StreamInput  = 3,
    kObj_StreamOutput = 4
};

#define kDeviceName         CFSTR("BadgeMic Microphone")
#define kManufacturerName   CFSTR("BadgeMic")
#define kModelName          CFSTR("BadgeMic Loopback")
#define kDeviceUID          CFSTR("BadgeMicLoopback_UID")
#define kModelUID           CFSTR("BadgeMicLoopback_ModelUID")

#define kChannels           2u
#define kBytesPerFrame      (kChannels * 4u)
#define kRingFrames         65536u              /* ~1.36 s at 48 kHz, power of 2 */
#define kZTSPeriodFrames    16384u
#define kNumRates           2

static const Float64 kRates[kNumRates] = { 44100.0, 48000.0 };

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static AudioServerPlugInHostRef gHost;
static pthread_mutex_t gStateMutex = PTHREAD_MUTEX_INITIALIZER;
static UInt32  gRefCount = 1;
static UInt32  gIOCount;                         /* guarded by gStateMutex */
static _Atomic UInt64 gAnchorHostTime;           /* host time at device sample time 0 */
static _Atomic UInt64 gTicksPerPeriodBits;       /* Float64 bits */
static Float64 gRate = 48000.0;
static _Atomic UInt32 gStreamActive[2] = { 1, 1 }; /* [0]=input [1]=output */
static _Atomic int64_t gWriteEnd;                /* one past last frame written */
static float   gRing[kRingFrames * kChannels];

static AudioServerPlugInDriverInterface *gDriverVtable;
static AudioServerPlugInDriverInterface **gDriverRef = &gDriverVtable;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static Float64 TicksPerPeriod(Float64 rate)
{
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    Float64 ticksPerSec = 1e9 * (Float64)tb.denom / (Float64)tb.numer;
    return ticksPerSec * (Float64)kZTSPeriodFrames / rate;
}

static void StoreTicksPerPeriod(Float64 v)
{
    UInt64 bits; memcpy(&bits, &v, sizeof bits);
    atomic_store(&gTicksPerPeriodBits, bits);
}

static Float64 LoadTicksPerPeriod(void)
{
    UInt64 bits = atomic_load(&gTicksPerPeriodBits);
    Float64 v; memcpy(&v, &bits, sizeof v);
    return v;
}

static Boolean IsSupportedRate(Float64 r)
{
    for (int i = 0; i < kNumRates; i++) if (r == kRates[i]) return true;
    return false;
}

static AudioStreamBasicDescription MakeFormat(Float64 rate)
{
    AudioStreamBasicDescription d;
    memset(&d, 0, sizeof d);
    d.mSampleRate       = rate;
    d.mFormatID         = kAudioFormatLinearPCM;
    d.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    d.mBytesPerPacket   = kBytesPerFrame;
    d.mFramesPerPacket  = 1;
    d.mBytesPerFrame    = kBytesPerFrame;
    d.mChannelsPerFrame = kChannels;
    d.mBitsPerChannel   = 32;
    return d;
}

static Boolean IsStream(AudioObjectID o) { return o == kObj_StreamInput || o == kObj_StreamOutput; }

/* Class-ID qualifier filter for owned-object style lists. */
static Boolean ClassMatches(UInt32 qSize, const void *q, AudioClassID cls)
{
    if (qSize == 0 || q == NULL) return true;
    const AudioClassID *c = (const AudioClassID *)q;
    for (UInt32 i = 0; i < qSize / sizeof(AudioClassID); i++)
        if (c[i] == cls || c[i] == kAudioObjectClassID) return true;
    return false;
}

/* Property value scratch. */
typedef struct {
    UInt32 size;
    Boolean isList;   /* may be truncated to fit the caller's buffer */
    _Alignas(16) uint8_t buf[512];
} PropVal;

static void PutU32(PropVal *pv, UInt32 v) { pv->size = 4; memcpy(pv->buf, &v, 4); }
static void PutF64(PropVal *pv, Float64 v) { pv->size = 8; memcpy(pv->buf, &v, 8); }
static void PutStr(PropVal *pv, CFStringRef s) { pv->size = sizeof(CFStringRef); memcpy(pv->buf, &s, sizeof s); }
static void PutID(PropVal *pv, UInt32 *ids, UInt32 n)
{
    pv->isList = true; pv->size = n * 4; if (n) memcpy(pv->buf, ids, n * 4);
}

/* ------------------------------------------------------------------ */
/* Property tables                                                     */
/* ------------------------------------------------------------------ */

static Boolean HasProp(AudioObjectID o, AudioObjectPropertySelector s)
{
    /* Common to every object */
    if (s == kAudioObjectPropertyBaseClass || s == kAudioObjectPropertyClass ||
        s == kAudioObjectPropertyOwner || s == kAudioObjectPropertyOwnedObjects)
        return o == kObj_PlugIn || o == kObj_Device || IsStream(o);

    switch (o) {
    case kObj_PlugIn:
        switch (s) {
        case kAudioObjectPropertyManufacturer:
        case kAudioPlugInPropertyBoxList:
        case kAudioPlugInPropertyDeviceList:
        case kAudioPlugInPropertyTranslateUIDToBox:
        case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
            return true;
        }
        return false;
    case kObj_Device:
        switch (s) {
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyModelName:
        case kAudioObjectPropertyControlList:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyRelatedDevices:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertyStreams:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyPreferredChannelsForStereo:
            return true;
        }
        return false;
    case kObj_StreamInput:
    case kObj_StreamOutput:
        switch (s) {
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            return true;
        }
        return false;
    }
    return false;
}

static Boolean IsSettableProp(AudioObjectID o, AudioObjectPropertySelector s)
{
    if (o == kObj_Device) return s == kAudioDevicePropertyNominalSampleRate;
    if (IsStream(o))
        return s == kAudioStreamPropertyIsActive || s == kAudioStreamPropertyVirtualFormat ||
               s == kAudioStreamPropertyPhysicalFormat;
    return false;
}

static OSStatus BuildProp(AudioObjectID o, const AudioObjectPropertyAddress *a,
                          UInt32 qSize, const void *q, PropVal *pv)
{
    memset(pv, 0, sizeof *pv);
    if (!HasProp(o, a->mSelector)) {
        if (o != kObj_PlugIn && o != kObj_Device && !IsStream(o)) return kAudioHardwareBadObjectError;
        return kAudioHardwareUnknownPropertyError;
    }
    UInt32 ids[4];

    switch (a->mSelector) {
    case kAudioObjectPropertyBaseClass:
        PutU32(pv, kAudioObjectClassID); return 0;
    case kAudioObjectPropertyClass:
        PutU32(pv, o == kObj_PlugIn ? kAudioPlugInClassID :
                   o == kObj_Device ? kAudioDeviceClassID : kAudioStreamClassID);
        return 0;
    case kAudioObjectPropertyOwner:
        PutU32(pv, o == kObj_PlugIn ? kAudioObjectUnknown :
                   o == kObj_Device ? kObj_PlugIn : kObj_Device);
        return 0;
    case kAudioObjectPropertyManufacturer:
        PutStr(pv, kManufacturerName); return 0;
    case kAudioObjectPropertyOwnedObjects: {
        UInt32 n = 0;
        if (o == kObj_PlugIn) {
            if (ClassMatches(qSize, q, kAudioDeviceClassID)) ids[n++] = kObj_Device;
        } else if (o == kObj_Device) {
            if (ClassMatches(qSize, q, kAudioStreamClassID)) { ids[n++] = kObj_StreamInput; ids[n++] = kObj_StreamOutput; }
        }
        PutID(pv, ids, n); return 0;
    }
    default: break;
    }

    if (o == kObj_PlugIn) {
        switch (a->mSelector) {
        case kAudioPlugInPropertyBoxList:
            PutID(pv, ids, 0); return 0;
        case kAudioPlugInPropertyDeviceList:
            ids[0] = kObj_Device; PutID(pv, ids, 1); return 0;
        case kAudioPlugInPropertyTranslateUIDToBox:
            if (qSize != sizeof(CFStringRef) || q == NULL) return kAudioHardwareBadPropertySizeError;
            PutU32(pv, kAudioObjectUnknown); return 0;
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            if (qSize != sizeof(CFStringRef) || q == NULL) return kAudioHardwareBadPropertySizeError;
            CFStringRef uid = *(CFStringRef const *)q;
            PutU32(pv, (uid && CFStringCompare(uid, kDeviceUID, 0) == kCFCompareEqualTo)
                           ? kObj_Device : kAudioObjectUnknown);
            return 0;
        }
        case kAudioPlugInPropertyResourceBundle:
            PutStr(pv, CFSTR("")); return 0;
        }
        return kAudioHardwareUnknownPropertyError;
    }

    if (o == kObj_Device) {
        Boolean isIn  = a->mScope == kAudioObjectPropertyScopeInput;
        Boolean isOut = a->mScope == kAudioObjectPropertyScopeOutput;
        switch (a->mSelector) {
        case kAudioObjectPropertyName:       PutStr(pv, kDeviceName); return 0;
        case kAudioObjectPropertyModelName:  PutStr(pv, kModelName); return 0;
        case kAudioObjectPropertyControlList: PutID(pv, ids, 0); return 0;
        case kAudioDevicePropertyDeviceUID:  PutStr(pv, kDeviceUID); return 0;
        case kAudioDevicePropertyModelUID:   PutStr(pv, kModelUID); return 0;
        case kAudioDevicePropertyTransportType: PutU32(pv, kAudioDeviceTransportTypeVirtual); return 0;
        case kAudioDevicePropertyRelatedDevices:
            ids[0] = kObj_Device; PutID(pv, ids, 1); return 0;
        case kAudioDevicePropertyClockDomain: PutU32(pv, 0); return 0;
        case kAudioDevicePropertyDeviceIsAlive: PutU32(pv, 1); return 0;
        case kAudioDevicePropertyDeviceIsRunning: {
            pthread_mutex_lock(&gStateMutex);
            UInt32 r = gIOCount > 0;
            pthread_mutex_unlock(&gStateMutex);
            PutU32(pv, r); return 0;
        }
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            PutU32(pv, isOut ? 0 : 1); return 0;
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
            PutU32(pv, 0); return 0;
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
            PutU32(pv, 0); return 0;
        case kAudioDevicePropertyStreams: {
            UInt32 n = 0;
            if (!isOut) ids[n++] = kObj_StreamInput;   /* input and global */
            if (!isIn)  ids[n++] = kObj_StreamOutput;  /* output and global */
            PutID(pv, ids, n); return 0;
        }
        case kAudioDevicePropertyNominalSampleRate: {
            pthread_mutex_lock(&gStateMutex);
            Float64 r = gRate;
            pthread_mutex_unlock(&gStateMutex);
            PutF64(pv, r); return 0;
        }
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            AudioValueRange rg[kNumRates];
            for (int i = 0; i < kNumRates; i++) { rg[i].mMinimum = kRates[i]; rg[i].mMaximum = kRates[i]; }
            pv->isList = true; pv->size = sizeof rg; memcpy(pv->buf, rg, sizeof rg); return 0;
        }
        case kAudioDevicePropertyIsHidden: PutU32(pv, 0); return 0;
        case kAudioDevicePropertyZeroTimeStampPeriod: PutU32(pv, kZTSPeriodFrames); return 0;
        case kAudioDevicePropertyPreferredChannelsForStereo:
            ids[0] = 1; ids[1] = 2; pv->size = 8; memcpy(pv->buf, ids, 8); return 0;
        }
        return kAudioHardwareUnknownPropertyError;
    }

    /* streams */
    Boolean input = (o == kObj_StreamInput);
    switch (a->mSelector) {
    case kAudioStreamPropertyIsActive:
        PutU32(pv, atomic_load(&gStreamActive[input ? 0 : 1])); return 0;
    case kAudioStreamPropertyDirection:
        PutU32(pv, input ? 1 : 0); return 0;
    case kAudioStreamPropertyTerminalType:
        PutU32(pv, input ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker); return 0;
    case kAudioStreamPropertyStartingChannel: PutU32(pv, 1); return 0;
    case kAudioStreamPropertyLatency:         PutU32(pv, 0); return 0;
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
        pthread_mutex_lock(&gStateMutex);
        Float64 r = gRate;
        pthread_mutex_unlock(&gStateMutex);
        AudioStreamBasicDescription d = MakeFormat(r);
        pv->size = sizeof d; memcpy(pv->buf, &d, sizeof d); return 0;
    }
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats: {
        AudioStreamRangedDescription rd[kNumRates];
        for (int i = 0; i < kNumRates; i++) {
            rd[i].mFormat = MakeFormat(kRates[i]);
            rd[i].mSampleRateRange.mMinimum = kRates[i];
            rd[i].mSampleRateRange.mMaximum = kRates[i];
        }
        pv->isList = true; pv->size = sizeof rd; memcpy(pv->buf, rd, sizeof rd); return 0;
    }
    }
    return kAudioHardwareUnknownPropertyError;
}

/* ------------------------------------------------------------------ */
/* IUnknown                                                            */
/* ------------------------------------------------------------------ */

static Boolean IsOurRef(AudioServerPlugInDriverRef r) { return r == (AudioServerPlugInDriverRef)gDriverRef; }

static HRESULT STDMETHODCALLTYPE BM_QueryInterface(void *inDriver, REFIID inUUID, LPVOID *outInterface)
{
    if (!IsOurRef(inDriver) || outInterface == NULL) return E_POINTER;
    CFUUIDRef req = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
    if (req == NULL) return E_NOINTERFACE;
    Boolean ok = CFEqual(req, IUnknownUUID) || CFEqual(req, kAudioServerPlugInDriverInterfaceUUID);
    CFRelease(req);
    if (!ok) { *outInterface = NULL; return E_NOINTERFACE; }
    pthread_mutex_lock(&gStateMutex); gRefCount++; pthread_mutex_unlock(&gStateMutex);
    *outInterface = gDriverRef;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE BM_AddRef(void *inDriver)
{
    if (!IsOurRef(inDriver)) return 0;
    pthread_mutex_lock(&gStateMutex);
    ULONG r = ++gRefCount;
    pthread_mutex_unlock(&gStateMutex);
    return r;
}

static ULONG STDMETHODCALLTYPE BM_Release(void *inDriver)
{
    if (!IsOurRef(inDriver)) return 0;
    pthread_mutex_lock(&gStateMutex);
    if (gRefCount > 0) gRefCount--;   /* static object: never freed */
    ULONG r = gRefCount;
    pthread_mutex_unlock(&gStateMutex);
    return r;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static OSStatus BM_Initialize(AudioServerPlugInDriverRef d, AudioServerPlugInHostRef host)
{
    if (!IsOurRef(d)) return kAudioHardwareBadObjectError;
    gHost = host;
    StoreTicksPerPeriod(TicksPerPeriod(gRate));
    return kAudioHardwareNoError;
}

static OSStatus BM_CreateDevice(AudioServerPlugInDriverRef d, CFDictionaryRef desc,
                                const AudioServerPlugInClientInfo *ci, AudioObjectID *out)
{
    (void)desc; (void)ci; (void)out;
    return IsOurRef(d) ? kAudioHardwareUnsupportedOperationError : kAudioHardwareBadObjectError;
}

static OSStatus BM_DestroyDevice(AudioServerPlugInDriverRef d, AudioObjectID dev)
{
    (void)dev;
    return IsOurRef(d) ? kAudioHardwareUnsupportedOperationError : kAudioHardwareBadObjectError;
}

static OSStatus BM_AddDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID dev, const AudioServerPlugInClientInfo *ci)
{
    (void)ci;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    return kAudioHardwareNoError;
}

static OSStatus BM_RemoveDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID dev, const AudioServerPlugInClientInfo *ci)
{
    (void)ci;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    return kAudioHardwareNoError;
}

static void NotifyRateChanged(void)
{
    if (gHost == NULL || gHost->PropertiesChanged == NULL) return;
    AudioObjectPropertyAddress dev[2] = {
        { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
        { kAudioDevicePropertyZeroTimeStampPeriod, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
    };
    gHost->PropertiesChanged(gHost, kObj_Device, 2, dev);
    AudioObjectPropertyAddress st[2] = {
        { kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
        { kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
    };
    gHost->PropertiesChanged(gHost, kObj_StreamInput, 2, st);
    gHost->PropertiesChanged(gHost, kObj_StreamOutput, 2, st);
}

/* Action = the new nominal sample rate as an integer. Called with IO stopped. */
static OSStatus BM_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID dev,
                                                    UInt64 action, void *info)
{
    (void)info;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    Float64 r = (Float64)action;
    if (!IsSupportedRate(r)) return kAudioHardwareIllegalOperationError;
    pthread_mutex_lock(&gStateMutex);
    gRate = r;
    StoreTicksPerPeriod(TicksPerPeriod(r));
    pthread_mutex_unlock(&gStateMutex);
    NotifyRateChanged();
    return kAudioHardwareNoError;
}

static OSStatus BM_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID dev,
                                                  UInt64 action, void *info)
{
    (void)action; (void)info;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    return kAudioHardwareNoError;
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

static Boolean BM_HasProperty(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid,
                              const AudioObjectPropertyAddress *a)
{
    (void)pid;
    if (!IsOurRef(d) || a == NULL) return false;
    return HasProp(o, a->mSelector);
}

static OSStatus BM_IsPropertySettable(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid,
                                      const AudioObjectPropertyAddress *a, Boolean *out)
{
    (void)pid;
    if (!IsOurRef(d)) return kAudioHardwareBadObjectError;
    if (a == NULL || out == NULL) return kAudioHardwareIllegalOperationError;
    if (o != kObj_PlugIn && o != kObj_Device && !IsStream(o)) return kAudioHardwareBadObjectError;
    if (!HasProp(o, a->mSelector)) return kAudioHardwareUnknownPropertyError;
    *out = IsSettableProp(o, a->mSelector);
    return kAudioHardwareNoError;
}

static OSStatus BM_GetPropertyDataSize(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid,
                                       const AudioObjectPropertyAddress *a, UInt32 qSize,
                                       const void *q, UInt32 *outSize)
{
    (void)pid;
    if (!IsOurRef(d)) return kAudioHardwareBadObjectError;
    if (a == NULL || outSize == NULL) return kAudioHardwareIllegalOperationError;
    PropVal pv;
    OSStatus st = BuildProp(o, a, qSize, q, &pv);
    if (st == 0) *outSize = pv.size;
    return st;
}

static OSStatus BM_GetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid,
                                   const AudioObjectPropertyAddress *a, UInt32 qSize, const void *q,
                                   UInt32 inSize, UInt32 *outSize, void *outData)
{
    (void)pid;
    if (!IsOurRef(d)) return kAudioHardwareBadObjectError;
    if (a == NULL || outSize == NULL || outData == NULL) return kAudioHardwareIllegalOperationError;
    PropVal pv;
    OSStatus st = BuildProp(o, a, qSize, q, &pv);
    if (st != 0) return st;
    UInt32 n = pv.size;
    if (pv.isList) {
        /* Lists: return as many whole elements as fit. */
        UInt32 elem = (a->mSelector == kAudioDevicePropertyAvailableNominalSampleRates) ? sizeof(AudioValueRange) :
                      (a->mSelector == kAudioStreamPropertyAvailableVirtualFormats ||
                       a->mSelector == kAudioStreamPropertyAvailablePhysicalFormats) ? sizeof(AudioStreamRangedDescription) : 4;
        if (n > inSize) n = (inSize / elem) * elem;
    } else if (inSize < n) {
        return kAudioHardwareBadPropertySizeError;
    }
    if (n) memcpy(outData, pv.buf, n);
    *outSize = n;
    return kAudioHardwareNoError;
}

static OSStatus RequestRate(Float64 r)
{
    pthread_mutex_lock(&gStateMutex);
    Float64 cur = gRate;
    pthread_mutex_unlock(&gStateMutex);
    if (r == cur) return kAudioHardwareNoError;
    if (gHost && gHost->RequestDeviceConfigurationChange)
        return gHost->RequestDeviceConfigurationChange(gHost, kObj_Device, (UInt64)r, NULL);
    return kAudioHardwareNoError;
}

static OSStatus BM_SetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid,
                                   const AudioObjectPropertyAddress *a, UInt32 qSize, const void *q,
                                   UInt32 inSize, const void *inData)
{
    (void)pid; (void)qSize; (void)q;
    if (!IsOurRef(d)) return kAudioHardwareBadObjectError;
    if (a == NULL || inData == NULL) return kAudioHardwareIllegalOperationError;
    if (o != kObj_PlugIn && o != kObj_Device && !IsStream(o)) return kAudioHardwareBadObjectError;
    if (!HasProp(o, a->mSelector)) return kAudioHardwareUnknownPropertyError;
    if (!IsSettableProp(o, a->mSelector)) return kAudioHardwareUnsupportedOperationError;

    if (o == kObj_Device) {          /* nominal sample rate */
        if (inSize != sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
        Float64 r; memcpy(&r, inData, sizeof r);
        if (!IsSupportedRate(r)) return kAudioDeviceUnsupportedFormatError;
        return RequestRate(r);
    }
    if (a->mSelector == kAudioStreamPropertyIsActive) {
        if (inSize != sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
        UInt32 v; memcpy(&v, inData, 4);
        atomic_store(&gStreamActive[o == kObj_StreamInput ? 0 : 1], v ? 1 : 0);
        if (gHost && gHost->PropertiesChanged) {
            AudioObjectPropertyAddress pa = { kAudioStreamPropertyIsActive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            gHost->PropertiesChanged(gHost, o, 1, &pa);
        }
        return kAudioHardwareNoError;
    }
    /* virtual / physical format */
    if (inSize != sizeof(AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
    AudioStreamBasicDescription f; memcpy(&f, inData, sizeof f);
    const AudioFormatFlags need = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    if (f.mFormatID != kAudioFormatLinearPCM || (f.mFormatFlags & need) != need ||
        (f.mFormatFlags & kAudioFormatFlagIsNonInterleaved) || (f.mFormatFlags & kAudioFormatFlagIsBigEndian) ||
        f.mBitsPerChannel != 32 || f.mChannelsPerFrame != kChannels || f.mFramesPerPacket != 1 ||
        f.mBytesPerFrame != kBytesPerFrame || f.mBytesPerPacket != kBytesPerFrame ||
        !IsSupportedRate(f.mSampleRate))
        return kAudioDeviceUnsupportedFormatError;
    return RequestRate(f.mSampleRate);
}

/* ------------------------------------------------------------------ */
/* IO                                                                  */
/* ------------------------------------------------------------------ */

static OSStatus BM_StartIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID)
{
    (void)clientID;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gStateMutex);
    if (gIOCount == UINT32_MAX) { pthread_mutex_unlock(&gStateMutex); return kAudioHardwareIllegalOperationError; }
    if (gIOCount == 0) {
        memset(gRing, 0, sizeof gRing);
        atomic_store(&gWriteEnd, 0);
        StoreTicksPerPeriod(TicksPerPeriod(gRate));
        atomic_store(&gAnchorHostTime, mach_absolute_time());
    }
    gIOCount++;
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

static OSStatus BM_StopIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID)
{
    (void)clientID;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gStateMutex);
    OSStatus st = kAudioHardwareNoError;
    if (gIOCount == 0) st = kAudioHardwareIllegalOperationError; else gIOCount--;
    pthread_mutex_unlock(&gStateMutex);
    return st;
}

static OSStatus BM_GetZeroTimeStamp(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID,
                                    Float64 *outSample, UInt64 *outHost, UInt64 *outSeed)
{
    (void)clientID;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    UInt64 anchor = atomic_load(&gAnchorHostTime);
    Float64 tpp = LoadTicksPerPeriod();
    UInt64 now = mach_absolute_time();
    UInt64 elapsed = now > anchor ? now - anchor : 0;
    UInt64 periods = (tpp > 0) ? (UInt64)((Float64)elapsed / tpp) : 0;
    *outSample = (Float64)(periods * (UInt64)kZTSPeriodFrames);
    *outHost   = anchor + (UInt64)((Float64)periods * tpp);
    *outSeed   = 1;
    return kAudioHardwareNoError;
}

static OSStatus BM_WillDoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID,
                                     UInt32 op, Boolean *outWillDo, Boolean *outInPlace)
{
    (void)clientID;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    *outWillDo = (op == kAudioServerPlugInIOOperationReadInput || op == kAudioServerPlugInIOOperationWriteMix);
    *outInPlace = true;
    return kAudioHardwareNoError;
}

static OSStatus BM_BeginIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID, UInt32 op,
                                    UInt32 frames, const AudioServerPlugInIOCycleInfo *ci)
{
    (void)clientID; (void)op; (void)frames; (void)ci;
    return (IsOurRef(d) && dev == kObj_Device) ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

static OSStatus BM_EndIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID, UInt32 op,
                                  UInt32 frames, const AudioServerPlugInIOCycleInfo *ci)
{
    (void)clientID; (void)op; (void)frames; (void)ci;
    return (IsOurRef(d) && dev == kObj_Device) ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

/* Copy frames between linear buffer and ring starting at absolute frame index start. */
static void RingCopy(int64_t start, UInt32 frames, float *linear, Boolean toRing)
{
    UInt32 pos = (UInt32)((uint64_t)start & (kRingFrames - 1));
    UInt32 first = frames;
    if (pos + first > kRingFrames) first = kRingFrames - pos;
    if (toRing) {
        memcpy(&gRing[pos * kChannels], linear, (size_t)first * kBytesPerFrame);
        if (frames > first) memcpy(gRing, linear + first * kChannels, (size_t)(frames - first) * kBytesPerFrame);
    } else {
        memcpy(linear, &gRing[pos * kChannels], (size_t)first * kBytesPerFrame);
        if (frames > first) memcpy(linear + first * kChannels, gRing, (size_t)(frames - first) * kBytesPerFrame);
    }
}

static void RingWrite(int64_t t, UInt32 frames, const float *src)
{
    if (frames > kRingFrames) { src += (frames - kRingFrames) * kChannels; t += frames - kRingFrames; frames = kRingFrames; }
    int64_t end = atomic_load_explicit(&gWriteEnd, memory_order_acquire);
    if (t > end) {   /* gap: wipe stale data between the old end and the new start */
        int64_t gap = t - end;
        if (gap >= (int64_t)kRingFrames) memset(gRing, 0, sizeof gRing);
        else {
            UInt32 pos = (UInt32)((uint64_t)end & (kRingFrames - 1));
            UInt32 n = (UInt32)gap, first = n;
            if (pos + first > kRingFrames) first = kRingFrames - pos;
            memset(&gRing[pos * kChannels], 0, (size_t)first * kBytesPerFrame);
            if (n > first) memset(gRing, 0, (size_t)(n - first) * kBytesPerFrame);
        }
    }
    RingCopy(t, frames, (float *)src, true);
    int64_t newEnd = t + frames;
    if (newEnd > end) atomic_store_explicit(&gWriteEnd, newEnd, memory_order_release);
}

static void RingRead(int64_t t, UInt32 frames, float *dst)
{
    int64_t end = atomic_load_explicit(&gWriteEnd, memory_order_acquire);
    int64_t lo = end - (int64_t)kRingFrames; if (lo < 0) lo = 0;
    int64_t from = t < lo ? lo : t;
    int64_t to = t + frames; if (to > end) to = end;
    if (to <= from) { memset(dst, 0, (size_t)frames * kBytesPerFrame); return; }
    UInt32 lead = (UInt32)(from - t), n = (UInt32)(to - from);
    if (lead) memset(dst, 0, (size_t)lead * kBytesPerFrame);
    RingCopy(from, n, dst + lead * kChannels, false);
    UInt32 tail = frames - lead - n;
    if (tail) memset(dst + (lead + n) * kChannels, 0, (size_t)tail * kBytesPerFrame);
}

static OSStatus BM_DoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, AudioObjectID stream,
                                 UInt32 clientID, UInt32 op, UInt32 frames,
                                 const AudioServerPlugInIOCycleInfo *ci, void *mainBuf, void *secBuf)
{
    (void)clientID; (void)secBuf;
    if (!IsOurRef(d) || dev != kObj_Device) return kAudioHardwareBadObjectError;
    if (!IsStream(stream)) return kAudioHardwareBadObjectError;
    if (mainBuf == NULL || ci == NULL) return kAudioHardwareNoError;
    if (op == kAudioServerPlugInIOOperationReadInput && stream == kObj_StreamInput) {
        RingRead((int64_t)ci->mInputTime.mSampleTime, frames, (float *)mainBuf);
    } else if (op == kAudioServerPlugInIOOperationWriteMix && stream == kObj_StreamOutput) {
        RingWrite((int64_t)ci->mOutputTime.mSampleTime, frames, (const float *)mainBuf);
    }
    return kAudioHardwareNoError;
}

/* ------------------------------------------------------------------ */
/* Factory                                                             */
/* ------------------------------------------------------------------ */

static AudioServerPlugInDriverInterface gVtable = {
    ._reserved                       = NULL,
    .QueryInterface                  = BM_QueryInterface,
    .AddRef                          = BM_AddRef,
    .Release                         = BM_Release,
    .Initialize                      = BM_Initialize,
    .CreateDevice                    = BM_CreateDevice,
    .DestroyDevice                   = BM_DestroyDevice,
    .AddDeviceClient                 = BM_AddDeviceClient,
    .RemoveDeviceClient              = BM_RemoveDeviceClient,
    .PerformDeviceConfigurationChange = BM_PerformDeviceConfigurationChange,
    .AbortDeviceConfigurationChange  = BM_AbortDeviceConfigurationChange,
    .HasProperty                     = BM_HasProperty,
    .IsPropertySettable              = BM_IsPropertySettable,
    .GetPropertyDataSize             = BM_GetPropertyDataSize,
    .GetPropertyData                 = BM_GetPropertyData,
    .SetPropertyData                 = BM_SetPropertyData,
    .StartIO                         = BM_StartIO,
    .StopIO                          = BM_StopIO,
    .GetZeroTimeStamp                = BM_GetZeroTimeStamp,
    .WillDoIOOperation               = BM_WillDoIOOperation,
    .BeginIOOperation                = BM_BeginIOOperation,
    .DoIOOperation                   = BM_DoIOOperation,
    .EndIOOperation                  = BM_EndIOOperation,
};

EXPORT void *BadgeMicAudio_Factory(CFAllocatorRef allocator, CFUUIDRef requestedTypeUUID)
{
    (void)allocator;
    if (requestedTypeUUID == NULL || !CFEqual(requestedTypeUUID, kAudioServerPlugInTypeUUID)) return NULL;
    gDriverVtable = &gVtable;
    return gDriverRef;
}
