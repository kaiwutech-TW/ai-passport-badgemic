/*
 * Copyright (c) 2026 BadgeMic contributors
 * SPDX-License-Identifier: MIT
 *
 * Host-side harness: loads the built plug-in binary with dlopen and drives it
 * the way coreaudiod would, with a fake host interface.
 */
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach_time.h>

static int gFail, gPass;
#define CHECK(c, ...) do { if (c) gPass++; else { gFail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int gReqCount; static UInt64 gReqAction; static int gChangedCount;
static OSStatus H_Changed(AudioServerPlugInHostRef h, AudioObjectID o, UInt32 n, const AudioObjectPropertyAddress *a) { (void)h;(void)o;(void)n;(void)a; gChangedCount++; return 0; }
static OSStatus H_From(AudioServerPlugInHostRef h, CFStringRef k, CFPropertyListRef *out) { (void)h;(void)k; *out = NULL; return kAudioHardwareUnknownPropertyError; }
static OSStatus H_To(AudioServerPlugInHostRef h, CFStringRef k, CFPropertyListRef d) { (void)h;(void)k;(void)d; return 0; }
static OSStatus H_Del(AudioServerPlugInHostRef h, CFStringRef k) { (void)h;(void)k; return 0; }
static OSStatus H_Req(AudioServerPlugInHostRef h, AudioObjectID o, UInt64 a, void *i) { (void)h;(void)o;(void)i; gReqCount++; gReqAction = a; return 0; }
static AudioServerPlugInHostInterface gHostIf = { H_Changed, H_From, H_To, H_Del, H_Req };

static AudioServerPlugInDriverRef D;
static AudioServerPlugInDriverInterface *V;

static AudioObjectPropertyAddress A(AudioObjectPropertySelector s, AudioObjectPropertyScope sc) {
    AudioObjectPropertyAddress a = { s, sc, kAudioObjectPropertyElementMain }; return a;
}

static OSStatus Get(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc,
                    UInt32 qs, const void *q, void *buf, UInt32 cap, UInt32 *outSize)
{
    AudioObjectPropertyAddress a = A(s, sc);
    UInt32 sz = 0, got = 0;
    OSStatus st = V->GetPropertyDataSize(D, o, 0, &a, qs, q, &sz);
    if (st) return st;
    if (sz > cap) return kAudioHardwareBadPropertySizeError;
    st = V->GetPropertyData(D, o, 0, &a, qs, q, cap, &got, buf);
    if (st == 0) { CHECK(got == sz, "size mismatch sel %08x: %u vs %u", s, sz, got); if (outSize) *outSize = got; }
    return st;
}
static UInt32 GetU32(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc) {
    UInt32 v = 0xDEADBEEF, n; OSStatus st = Get(o, s, sc, 0, NULL, &v, 4, &n);
    CHECK(st == 0 && n == 4, "GetU32 obj %u sel %08x st %d", o, s, (int)st); return v;
}
static CFStringRef GetStr(AudioObjectID o, AudioObjectPropertySelector s) {
    CFStringRef v = NULL; UInt32 n; OSStatus st = Get(o, s, kAudioObjectPropertyScopeGlobal, 0, NULL, &v, sizeof v, &n);
    CHECK(st == 0 && v, "GetStr obj %u sel %08x", o, s); return v;
}
static void CheckStr(AudioObjectID o, AudioObjectPropertySelector s, const char *want) {
    CFStringRef v = GetStr(o, s); char b[128] = "";
    if (v) CFStringGetCString(v, b, sizeof b, kCFStringEncodingUTF8);
    CHECK(strcmp(b, want) == 0, "string sel %08x = '%s' want '%s'", s, b, want);
}

static void CheckFormat(const AudioStreamBasicDescription *f, Float64 rate) {
    CHECK(f->mSampleRate == rate, "rate %f", f->mSampleRate);
    CHECK(f->mFormatID == kAudioFormatLinearPCM, "fmt id");
    CHECK((f->mFormatFlags & kAudioFormatFlagIsFloat) && (f->mFormatFlags & kAudioFormatFlagIsPacked) &&
          !(f->mFormatFlags & kAudioFormatFlagIsNonInterleaved), "flags %x", f->mFormatFlags);
    CHECK(f->mChannelsPerFrame == 2 && f->mBitsPerChannel == 32 && f->mBytesPerFrame == 8 &&
          f->mBytesPerPacket == 8 && f->mFramesPerPacket == 1, "layout");
}

/* Every property HasProperty claims must be gettable (translate props need qualifier). */
static void SweepObject(AudioObjectID o) {
    static const AudioObjectPropertySelector sels[] = {
        kAudioObjectPropertyBaseClass, kAudioObjectPropertyClass, kAudioObjectPropertyOwner, kAudioObjectPropertyOwnedObjects,
        kAudioObjectPropertyName, kAudioObjectPropertyModelName, kAudioObjectPropertyManufacturer, kAudioObjectPropertyControlList,
        kAudioPlugInPropertyBoxList, kAudioPlugInPropertyDeviceList, kAudioPlugInPropertyResourceBundle,
        kAudioDevicePropertyDeviceUID, kAudioDevicePropertyModelUID, kAudioDevicePropertyTransportType,
        kAudioDevicePropertyRelatedDevices, kAudioDevicePropertyClockDomain, kAudioDevicePropertyDeviceIsAlive,
        kAudioDevicePropertyDeviceIsRunning, kAudioDevicePropertyDeviceCanBeDefaultDevice,
        kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioDevicePropertyLatency, kAudioDevicePropertyStreams,
        kAudioDevicePropertySafetyOffset, kAudioDevicePropertyNominalSampleRate,
        kAudioDevicePropertyAvailableNominalSampleRates, kAudioDevicePropertyIsHidden,
        kAudioDevicePropertyZeroTimeStampPeriod, kAudioDevicePropertyPreferredChannelsForStereo,
        kAudioStreamPropertyIsActive, kAudioStreamPropertyDirection, kAudioStreamPropertyTerminalType,
        kAudioStreamPropertyStartingChannel, kAudioStreamPropertyLatency, kAudioStreamPropertyVirtualFormat,
        kAudioStreamPropertyPhysicalFormat, kAudioStreamPropertyAvailableVirtualFormats,
        kAudioStreamPropertyAvailablePhysicalFormats,
        'xxxx', kAudioObjectPropertyCustomPropertyInfoList, kAudioDevicePropertyBufferFrameSize,
    };
    int has = 0;
    for (size_t i = 0; i < sizeof sels / sizeof *sels; i++) {
        AudioObjectPropertyAddress a = A(sels[i], kAudioObjectPropertyScopeGlobal);
        Boolean h = V->HasProperty(D, o, 0, &a);
        UInt32 sz = 0; OSStatus st = V->GetPropertyDataSize(D, o, 0, &a, 0, NULL, &sz);
        Boolean settable = 0; OSStatus st2 = V->IsPropertySettable(D, o, 0, &a, &settable);
        if (!h) {
            CHECK(st == kAudioHardwareUnknownPropertyError, "obj %u sel %08x not has but size st %d", o, sels[i], (int)st);
            CHECK(st2 == kAudioHardwareUnknownPropertyError, "settable st for unknown");
            /* GetPropertyData must also reject */
            char b[64]; UInt32 n;
            CHECK(V->GetPropertyData(D, o, 0, &a, 0, NULL, sizeof b, &n, b) == kAudioHardwareUnknownPropertyError, "GetPropertyData unknown");
            continue;
        }
        has++;
        if (sels[i] == kAudioPlugInPropertyTranslateUIDToDevice) continue;
        CHECK(st == 0, "obj %u sel %08x has but size st %d", o, sels[i], (int)st);
        CHECK(st2 == 0, "settable query st");
        if (st == 0) {
            /* too-small buffer on a fixed-size property must be rejected */
            char b[512]; UInt32 n = 0;
            OSStatus g = V->GetPropertyData(D, o, 0, &a, 0, NULL, sz, &n, b);
            CHECK(g == 0 && n == sz, "full-size get sel %08x", sels[i]);
        }
    }
    printf("  object %u: %d properties advertised\n", o, has);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "../dist/BadgeMicAudio.driver/Contents/MacOS/BadgeMicAudio";
    void *lib = dlopen(path, RTLD_NOW);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 2; }
    typedef void *(*Factory)(CFAllocatorRef, CFUUIDRef);
    Factory f = (Factory)dlsym(lib, "BadgeMicAudio_Factory");
    if (!f) { printf("factory symbol missing\n"); return 2; }

    CHECK(f(NULL, CFUUIDCreate(NULL)) == NULL, "factory must reject other type UUIDs");
    D = (AudioServerPlugInDriverRef)f(NULL, kAudioServerPlugInTypeUUID);
    CHECK(D != NULL, "factory returned NULL");
    if (!D) return 2;
    V = *D;
    void *vt = NULL;
    CHECK(V->QueryInterface(D, CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID), &vt) == 0 && vt == D, "QueryInterface");
    CHECK(V->QueryInterface(D, CFUUIDGetUUIDBytes(CFUUIDCreate(NULL)), &vt) != 0, "QueryInterface bad uuid");
    V->AddRef(D); V->Release(D); V->Release(D);

    CHECK(V->Initialize(D, &gHostIf) == 0, "Initialize");
    CHECK(V->CreateDevice(D, NULL, NULL, NULL) == kAudioHardwareUnsupportedOperationError, "CreateDevice");
    CHECK(V->DestroyDevice(D, 2) == kAudioHardwareUnsupportedOperationError, "DestroyDevice");
    CHECK(V->AddDeviceClient(D, 2, NULL) == 0 && V->RemoveDeviceClient(D, 2, NULL) == 0, "device clients");

    /* Plug-in */
    printf("plug-in\n");
    CHECK(GetU32(1, kAudioObjectPropertyClass, 0) == kAudioPlugInClassID, "plugin class");
    CHECK(GetU32(1, kAudioObjectPropertyOwner, 0) == kAudioObjectUnknown, "plugin owner");
    CheckStr(1, kAudioObjectPropertyManufacturer, "BadgeMic");
    AudioObjectID ids[8]; UInt32 n;
    CHECK(Get(1, kAudioPlugInPropertyDeviceList, 0, 0, NULL, ids, sizeof ids, &n) == 0 && n == 4 && ids[0] == 2, "device list");
    CHECK(Get(1, kAudioPlugInPropertyBoxList, 0, 0, NULL, ids, sizeof ids, &n) == 0 && n == 0, "box list empty");
    CFStringRef uid = CFSTR("BadgeMicLoopback_UID"); AudioObjectID dev = 0;
    CHECK(Get(1, kAudioPlugInPropertyTranslateUIDToDevice, 0, sizeof uid, &uid, &dev, 4, &n) == 0 && dev == 2, "translate uid");
    uid = CFSTR("nope");
    CHECK(Get(1, kAudioPlugInPropertyTranslateUIDToDevice, 0, sizeof uid, &uid, &dev, 4, &n) == 0 && dev == 0, "translate bad uid");
    CHECK(Get(1, kAudioPlugInPropertyTranslateUIDToDevice, 0, 0, NULL, &dev, 4, &n) == kAudioHardwareBadPropertySizeError, "translate w/o qualifier");
    AudioClassID cls = kAudioDeviceClassID;
    CHECK(Get(1, kAudioObjectPropertyOwnedObjects, 0, 4, &cls, ids, sizeof ids, &n) == 0 && n == 4, "owned by class device");
    cls = kAudioStreamClassID;
    CHECK(Get(1, kAudioObjectPropertyOwnedObjects, 0, 4, &cls, ids, sizeof ids, &n) == 0 && n == 0, "owned by class stream (none)");
    SweepObject(1);

    /* Device */
    printf("device\n");
    CHECK(GetU32(2, kAudioObjectPropertyClass, 0) == kAudioDeviceClassID, "device class");
    CHECK(GetU32(2, kAudioObjectPropertyOwner, 0) == 1, "device owner");
    CheckStr(2, kAudioObjectPropertyName, "BadgeMic Microphone");
    CheckStr(2, kAudioObjectPropertyManufacturer, "BadgeMic");
    CheckStr(2, kAudioDevicePropertyDeviceUID, "BadgeMicLoopback_UID");
    CheckStr(2, kAudioDevicePropertyModelUID, "BadgeMicLoopback_ModelUID");
    CHECK(GetU32(2, kAudioDevicePropertyTransportType, 0) == kAudioDeviceTransportTypeVirtual, "transport");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceIsAlive, 0) == 1, "alive");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceIsRunning, 0) == 0, "not running yet");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeOutput) == 0, "default out");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeInput) == 1, "default in");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioObjectPropertyScopeOutput) == 0, "sys out");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioObjectPropertyScopeInput) == 0, "sys in");
    CHECK(GetU32(2, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput) == 0, "latency");
    CHECK(GetU32(2, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput) == 0, "safety");
    CHECK(GetU32(2, kAudioDevicePropertyZeroTimeStampPeriod, 0) == 16384, "zts period");
    CHECK(GetU32(2, kAudioDevicePropertyIsHidden, 0) == 0, "hidden");
    Float64 rate = 0;
    CHECK(Get(2, kAudioDevicePropertyNominalSampleRate, 0, 0, NULL, &rate, 8, &n) == 0 && rate == 48000.0, "rate");
    AudioValueRange rr[4];
    CHECK(Get(2, kAudioDevicePropertyAvailableNominalSampleRates, 0, 0, NULL, rr, sizeof rr, &n) == 0 && n == 32 && rr[1].mMinimum == 48000.0, "avail rates");
    CHECK(Get(2, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, 0, NULL, ids, sizeof ids, &n) == 0 && n == 4 && ids[0] == 3, "in streams");
    CHECK(Get(2, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, 0, NULL, ids, sizeof ids, &n) == 0 && n == 4 && ids[0] == 4, "out streams");
    CHECK(Get(2, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeGlobal, 0, NULL, ids, sizeof ids, &n) == 0 && n == 8, "all streams");
    CHECK(Get(2, kAudioObjectPropertyOwnedObjects, 0, 0, NULL, ids, sizeof ids, &n) == 0 && n == 8, "owned objects");
    CHECK(Get(2, kAudioObjectPropertyControlList, 0, 0, NULL, ids, sizeof ids, &n) == 0 && n == 0, "no controls");
    { /* truncation of list into small buffer */
        AudioObjectPropertyAddress a = A(kAudioDevicePropertyStreams, 0); UInt32 got = 99; AudioObjectID one;
        CHECK(V->GetPropertyData(D, 2, 0, &a, 0, NULL, 4, &got, &one) == 0 && got == 4, "list truncation");
        /* fixed-size property with small buffer */
        a = A(kAudioDevicePropertyNominalSampleRate, 0); Float32 small;
        CHECK(V->GetPropertyData(D, 2, 0, &a, 0, NULL, 4, &got, &small) == kAudioHardwareBadPropertySizeError, "bad size");
    }
    SweepObject(2);

    /* Streams */
    for (int s = 0; s < 2; s++) {
        AudioObjectID so = s == 0 ? 3 : 4; Boolean in = s == 0;
        printf(in ? "input stream\n" : "output stream\n");
        CHECK(GetU32(so, kAudioObjectPropertyClass, 0) == kAudioStreamClassID, "stream class");
        CHECK(GetU32(so, kAudioObjectPropertyOwner, 0) == 2, "stream owner");
        CHECK(GetU32(so, kAudioStreamPropertyDirection, 0) == (in ? 1u : 0u), "direction");
        CHECK(GetU32(so, kAudioStreamPropertyIsActive, 0) == 1, "active");
        CHECK(GetU32(so, kAudioStreamPropertyStartingChannel, 0) == 1, "start ch");
        CHECK(GetU32(so, kAudioStreamPropertyLatency, 0) == 0, "stream latency");
        CHECK(GetU32(so, kAudioStreamPropertyTerminalType, 0) ==
              (in ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker), "terminal");
        AudioStreamBasicDescription fmt;
        CHECK(Get(so, kAudioStreamPropertyVirtualFormat, 0, 0, NULL, &fmt, sizeof fmt, &n) == 0 && n == sizeof fmt, "vfmt");
        CheckFormat(&fmt, 48000);
        CHECK(Get(so, kAudioStreamPropertyPhysicalFormat, 0, 0, NULL, &fmt, sizeof fmt, &n) == 0, "pfmt");
        CheckFormat(&fmt, 48000);
        AudioStreamRangedDescription rd[4];
        CHECK(Get(so, kAudioStreamPropertyAvailableVirtualFormats, 0, 0, NULL, rd, sizeof rd, &n) == 0 && n == 2 * sizeof rd[0], "avail vfmts");
        CheckFormat(&rd[0].mFormat, 44100); CheckFormat(&rd[1].mFormat, 48000);
        CHECK(Get(so, kAudioStreamPropertyAvailablePhysicalFormats, 0, 0, NULL, rd, sizeof rd, &n) == 0 && n == 2 * sizeof rd[0], "avail pfmts");
        CHECK(Get(so, kAudioObjectPropertyOwnedObjects, 0, 0, NULL, ids, sizeof ids, &n) == 0 && n == 0, "stream owns nothing");
        SweepObject(so);
    }

    /* Settability and sets */
    printf("set properties\n");
    {
        Boolean st; AudioObjectPropertyAddress a = A(kAudioDevicePropertyNominalSampleRate, 0);
        CHECK(V->IsPropertySettable(D, 2, 0, &a, &st) == 0 && st, "rate settable");
        a = A(kAudioObjectPropertyName, 0);
        CHECK(V->IsPropertySettable(D, 2, 0, &a, &st) == 0 && !st, "name not settable");
        Float64 r = 44100.0; a = A(kAudioDevicePropertyNominalSampleRate, 0);
        CHECK(V->SetPropertyData(D, 2, 0, &a, 0, NULL, 8, &r) == 0 && gReqCount == 1 && gReqAction == 44100, "request rate change");
        CHECK(V->PerformDeviceConfigurationChange(D, 2, gReqAction, NULL) == 0, "perform change");
        Float64 cur = 0; Get(2, kAudioDevicePropertyNominalSampleRate, 0, 0, NULL, &cur, 8, &n);
        CHECK(cur == 44100.0, "rate now 44100");
        AudioStreamBasicDescription fmt; Get(3, kAudioStreamPropertyVirtualFormat, 0, 0, NULL, &fmt, sizeof fmt, &n);
        CheckFormat(&fmt, 44100);
        r = 12345.0;
        CHECK(V->SetPropertyData(D, 2, 0, &a, 0, NULL, 8, &r) == kAudioDeviceUnsupportedFormatError, "reject bad rate");
        CHECK(V->SetPropertyData(D, 2, 0, &a, 0, NULL, 4, &r) == kAudioHardwareBadPropertySizeError, "reject bad size");
        AudioStreamBasicDescription bad = fmt; bad.mChannelsPerFrame = 1;
        a = A(kAudioStreamPropertyVirtualFormat, 0);
        CHECK(V->SetPropertyData(D, 4, 0, &a, 0, NULL, sizeof bad, &bad) == kAudioDeviceUnsupportedFormatError, "reject mono");
        AudioStreamBasicDescription good = fmt; good.mSampleRate = 48000;
        int before = gReqCount;
        CHECK(V->SetPropertyData(D, 4, 0, &a, 0, NULL, sizeof good, &good) == 0 && gReqCount == before + 1 && gReqAction == 48000, "format set requests 48k");
        V->PerformDeviceConfigurationChange(D, 2, gReqAction, NULL);
        Get(2, kAudioDevicePropertyNominalSampleRate, 0, 0, NULL, &cur, 8, &n);
        CHECK(cur == 48000.0, "back to 48000");
        UInt32 off = 0; a = A(kAudioStreamPropertyIsActive, 0);
        CHECK(V->SetPropertyData(D, 3, 0, &a, 0, NULL, 4, &off) == 0 && GetU32(3, kAudioStreamPropertyIsActive, 0) == 0, "deactivate");
        off = 1; V->SetPropertyData(D, 3, 0, &a, 0, NULL, 4, &off);
        CHECK(GetU32(3, kAudioStreamPropertyIsActive, 0) == 1, "reactivate");
        CHECK(gChangedCount > 0, "PropertiesChanged called");
    }

    /* IO */
    printf("io\n");
    Boolean will, inplace;
    CHECK(V->WillDoIOOperation(D, 2, 1, kAudioServerPlugInIOOperationReadInput, &will, &inplace) == 0 && will, "will read");
    CHECK(V->WillDoIOOperation(D, 2, 1, kAudioServerPlugInIOOperationWriteMix, &will, &inplace) == 0 && will, "will write");
    CHECK(V->WillDoIOOperation(D, 2, 1, kAudioServerPlugInIOOperationProcessOutput, &will, &inplace) == 0 && !will, "won't process");
    CHECK(V->StopIO(D, 2, 1) == kAudioHardwareIllegalOperationError, "stop without start");
    CHECK(V->StartIO(D, 2, 1) == 0, "StartIO");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceIsRunning, 0) == 1, "running");

    Float64 s0; UInt64 h0, seed;
    CHECK(V->GetZeroTimeStamp(D, 2, 1, &s0, &h0, &seed) == 0, "zts");
    CHECK(s0 == 0.0, "first ZTS sample time 0 (%f)", s0);
    usleep(400 * 1000);
    Float64 s1; UInt64 h1;
    V->GetZeroTimeStamp(D, 2, 1, &s1, &h1, &seed);
    CHECK(s1 > s0 && fmod(s1, 16384.0) == 0.0 && h1 > h0, "ZTS advanced: %f host %llu->%llu", s1, h0, h1);
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    Float64 dtNs = (Float64)(h1 - h0) * tb.numer / tb.denom;
    Float64 expectNs = (s1 - s0) / 48000.0 * 1e9;
    CHECK(fabs(dtNs - expectNs) < 1e6, "host time delta matches sample time (%f vs %f ns)", dtNs, expectNs);

    enum { N = 480, CYCLES = 20 };
    float wbuf[N * 2], rbuf[N * 2];
    AudioServerPlugInIOCycleInfo ci; memset(&ci, 0, sizeof ci);
    int mismatches = 0; double phase = 0;
    static float expected[CYCLES][N * 2];
    /* output leads input: write cycle c at t, then read at same t */
    for (int c = 0; c < CYCLES; c++) {
        Float64 t = 1000 + (Float64)c * N;
        for (int i = 0; i < N; i++) {
            float v = 0.5f * sinf((float)phase); phase += 2 * M_PI * 440.0 / 48000.0;
            wbuf[2*i] = v; wbuf[2*i+1] = -v;
        }
        memcpy(expected[c], wbuf, sizeof wbuf);
        ci.mOutputTime.mSampleTime = t; ci.mInputTime.mSampleTime = t; ci.mNominalIOBufferFrameSize = N;
        V->BeginIOOperation(D, 2, 1, kAudioServerPlugInIOOperationWriteMix, N, &ci);
        CHECK(V->DoIOOperation(D, 2, 4, 1, kAudioServerPlugInIOOperationWriteMix, N, &ci, wbuf, NULL) == 0, "write");
        V->EndIOOperation(D, 2, 1, kAudioServerPlugInIOOperationWriteMix, N, &ci);
    }
    for (int c = 0; c < CYCLES; c++) {
        ci.mInputTime.mSampleTime = 1000 + (Float64)c * N;
        memset(rbuf, 0x7f, sizeof rbuf);
        CHECK(V->DoIOOperation(D, 2, 3, 2, kAudioServerPlugInIOOperationReadInput, N, &ci, rbuf, NULL) == 0, "read");
        if (memcmp(rbuf, expected[c], sizeof rbuf) != 0) mismatches++;
    }
    CHECK(mismatches == 0, "loopback mismatches in %d cycles", mismatches);

    /* read straddling data end, and unwritten regions -> zeros */
    ci.mInputTime.mSampleTime = 1000 + (Float64)CYCLES * N - 100;
    memset(rbuf, 0x7f, sizeof rbuf);
    V->DoIOOperation(D, 2, 3, 2, kAudioServerPlugInIOOperationReadInput, N, &ci, rbuf, NULL);
    CHECK(memcmp(rbuf, &expected[CYCLES-1][(N-100)*2], 100 * 8) == 0, "tail data");
    int nz = 0; for (int i = 100 * 2; i < N * 2; i++) if (rbuf[i] != 0) nz++;
    CHECK(nz == 0, "silence after end of data");
    ci.mInputTime.mSampleTime = 50; memset(rbuf, 0x7f, sizeof rbuf);
    V->DoIOOperation(D, 2, 3, 2, kAudioServerPlugInIOOperationReadInput, N, &ci, rbuf, NULL);
    nz = 0; for (int i = 0; i < N * 2; i++) if (rbuf[i] != 0) nz++;
    CHECK(nz == 0, "silence before data");

    /* ring wrap: write far ahead, past the 65536-frame ring, then read back */
    {
        Float64 t = 300000.0;
        for (int i = 0; i < N; i++) { wbuf[2*i] = 0.25f; wbuf[2*i+1] = 0.75f; }
        ci.mOutputTime.mSampleTime = t;
        V->DoIOOperation(D, 2, 4, 1, kAudioServerPlugInIOOperationWriteMix, N, &ci, wbuf, NULL);
        ci.mInputTime.mSampleTime = t;
        V->DoIOOperation(D, 2, 3, 2, kAudioServerPlugInIOOperationReadInput, N, &ci, rbuf, NULL);
        CHECK(memcmp(rbuf, wbuf, sizeof wbuf) == 0, "wrapped region readback");
        /* the old data must not reappear at an aliasing time inside the 1 s window */
        ci.mInputTime.mSampleTime = t - 1000;
        V->DoIOOperation(D, 2, 3, 2, kAudioServerPlugInIOOperationReadInput, N, &ci, rbuf, NULL);
        nz = 0; for (int i = 0; i < N * 2; i++) if (rbuf[i] != 0) nz++;
        CHECK(nz == 0, "no stale data in gap before new write");
        /* wrapping buffer straddling ring boundary */
        t = 65536.0 * 5 - 100;
        ci.mOutputTime.mSampleTime = t;
        for (int i = 0; i < N; i++) { wbuf[2*i] = (float)i; wbuf[2*i+1] = (float)-i; }
        V->DoIOOperation(D, 2, 4, 1, kAudioServerPlugInIOOperationWriteMix, N, &ci, wbuf, NULL);
        ci.mInputTime.mSampleTime = t;
        V->DoIOOperation(D, 2, 3, 2, kAudioServerPlugInIOOperationReadInput, N, &ci, rbuf, NULL);
        CHECK(memcmp(rbuf, wbuf, sizeof wbuf) == 0, "ring-boundary straddle");
    }

    CHECK(V->StopIO(D, 2, 1) == 0, "StopIO");
    CHECK(GetU32(2, kAudioDevicePropertyDeviceIsRunning, 0) == 0, "stopped");

    /* unknown object */
    AudioObjectPropertyAddress a = A(kAudioObjectPropertyClass, 0); UInt32 sz;
    CHECK(V->GetPropertyDataSize(D, 99, 0, &a, 0, NULL, &sz) == kAudioHardwareBadObjectError, "bad object");
    CHECK(V->HasProperty(D, 99, 0, &a) == 0, "bad object has");

    printf("\n%d checks passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
