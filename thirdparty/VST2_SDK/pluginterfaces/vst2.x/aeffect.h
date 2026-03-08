/*
 * VST2 SDK compatibility header for JUCE plugin hosting.
 *
 * This file provides the minimal VST 2.4 interface definitions required
 * by JUCE's VST2 hosting code. The VST2 plugin interface is a well-known
 * binary standard originally defined by Steinberg.
 *
 * Steinberg discontinued the VST2 SDK in October 2018.
 * These are interface-only definitions for binary compatibility with
 * existing VST2 plugins.
 */

#pragma once

#include <cstdint>

#if _WIN32
 #define VSTCALLBACK __cdecl
#else
 #define VSTCALLBACK
#endif

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;

// Forward declaration
struct AEffect;

// Callback from plugin to host
typedef VstIntPtr (VSTCALLBACK *audioMasterCallback)(AEffect* effect, VstInt32 opcode,
    VstInt32 index, VstIntPtr value, void* ptr, float opt);

// Dispatcher function pointer
typedef VstIntPtr (VSTCALLBACK *AEffectDispatcherProc)(AEffect* effect, VstInt32 opcode,
    VstInt32 index, VstIntPtr value, void* ptr, float opt);

// Process function pointers
typedef void (VSTCALLBACK *AEffectProcessProc)(AEffect* effect, float** inputs,
    float** outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK *AEffectProcessDoubleProc)(AEffect* effect, double** inputs,
    double** outputs, VstInt32 sampleFrames);

// Parameter function pointers
typedef void (VSTCALLBACK *AEffectSetParameterProc)(AEffect* effect, VstInt32 index, float parameter);
typedef float (VSTCALLBACK *AEffectGetParameterProc)(AEffect* effect, VstInt32 index);

// Effect flags
enum VstAEffectFlags
{
    effFlagsHasEditor          = 1 << 0,
    effFlagsCanReplacing       = 1 << 4,
    effFlagsProgramChunks      = 1 << 5,
    effFlagsIsSynth            = 1 << 8,
    effFlagsNoSoundInStop      = 1 << 9,
    effFlagsCanDoubleReplacing = 1 << 12
};

// Magic number to identify a valid AEffect
const VstInt32 kEffectMagic = 0x56737450; // 'VstP'

struct AEffect
{
    VstInt32 magic;

    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process;            // deprecated
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;

    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;

    VstInt32 flags;

    VstIntPtr resvd1;
    VstIntPtr resvd2;

    VstInt32 initialDelay;

    VstInt32 realQualities;    // unused
    VstInt32 offQualities;     // unused
    float    ioRatio;          // unused

    void*    object;
    void*    user;

    VstInt32 uniqueID;
    VstInt32 version;

    AEffectProcessProc processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;

    char future[56];
};
