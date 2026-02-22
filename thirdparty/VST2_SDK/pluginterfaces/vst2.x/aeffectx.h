/*
 * VST2 SDK compatibility header (extended) for JUCE plugin hosting.
 *
 * This file provides the extended VST 2.4 interface definitions required
 * by JUCE's VST2 hosting code including opcodes, events, time info,
 * and speaker arrangements.
 *
 * These are interface-only definitions for binary compatibility with
 * existing VST2 plugins.
 */

#pragma once

#include "aeffect.h"

typedef int16_t VstInt16;

// ─── String length constants ───────────────────────────────────────
enum
{
    kVstMaxProductStrLen = 64,
    kVstMaxVendorStrLen  = 64,
    kVstMaxParamStrLen   = 8,
    kVstMaxProgNameLen   = 24,
    kVstMaxEffectNameLen = 32,
    kVstMaxNameLen       = 64
};

// ─── Effect (plugin) dispatch opcodes ──────────────────────────────
enum
{
    effOpen                 = 0,
    effClose                = 1,
    effSetProgram           = 2,
    effGetProgram           = 3,
    effSetProgramName       = 4,
    effGetProgramName       = 5,
    effGetParamLabel        = 6,
    effGetParamDisplay      = 7,
    effGetParamName         = 8,
    effSetSampleRate        = 10,
    effSetBlockSize         = 11,
    effMainsChanged         = 12,
    effEditGetRect          = 13,
    effEditOpen             = 14,
    effEditClose            = 15,
    effEditIdle             = 19,
    effEditTop              = 20,
    effIdentify             = 22,
    effGetChunk             = 23,
    effSetChunk             = 24,

    // VST 2.0
    effProcessEvents        = 25,
    effCanBeAutomated       = 26,
    effString2Parameter     = 27,
    effGetProgramNameIndexed = 29,
    effConnectInput         = 31,
    effConnectOutput        = 32,
    effGetInputProperties   = 33,
    effGetOutputProperties  = 34,
    effGetPlugCategory      = 35,
    effOfflineNotify        = 36,
    effOfflinePrepare       = 37,
    effOfflineRun           = 38,
    effProcessVarIo         = 39,
    effSetSpeakerArrangement = 42,
    effSetBypass            = 44,
    effGetEffectName        = 45,
    effGetVendorString      = 47,
    effGetProductString     = 48,
    effGetVendorVersion     = 49,
    effVendorSpecific       = 50,
    effCanDo                = 51,
    effGetTailSize          = 52,
    effIdle                 = 53,
    effGetParameterProperties = 56,
    effGetVstVersion        = 58,

    // VST 2.1
    effEditKeyDown          = 59,
    effEditKeyUp            = 60,
    effSetEditKnobMode      = 61,
    effGetMidiProgramName   = 62,
    effGetCurrentMidiProgram = 63,
    effGetMidiProgramCategory = 64,
    effHasMidiProgramsChanged = 65,
    effGetMidiKeyName       = 66,
    effBeginSetProgram      = 67,
    effEndSetProgram        = 68,

    // VST 2.3
    effGetSpeakerArrangement = 69,
    effShellGetNextPlugin   = 70,
    effStartProcess         = 71,
    effStopProcess          = 72,
    effSetTotalSampleToProcess = 73,
    effSetPanLaw            = 74,
    effBeginLoadBank        = 75,
    effBeginLoadProgram     = 76,

    // VST 2.4
    effSetProcessPrecision  = 77,
    effGetNumMidiInputChannels = 78,
    effGetNumMidiOutputChannels = 79,

    effKeysRequired         = 57  // deprecated
};

// ─── AudioMaster (host) callback opcodes ───────────────────────────
enum
{
    audioMasterAutomate                  = 0,
    audioMasterVersion                   = 1,
    audioMasterCurrentId                 = 2,
    audioMasterIdle                      = 3,
    audioMasterPinConnected              = 4,  // deprecated
    audioMasterWantMidi                  = 6,  // deprecated
    audioMasterGetTime                   = 7,
    audioMasterProcessEvents             = 8,
    audioMasterSetTime                   = 9,  // deprecated
    audioMasterTempoAt                   = 10, // deprecated
    audioMasterGetNumAutomatableParameters = 11, // deprecated
    audioMasterGetParameterQuantization  = 12, // deprecated
    audioMasterIOChanged                 = 13,
    audioMasterNeedIdle                  = 14, // deprecated
    audioMasterSizeWindow                = 15,
    audioMasterGetSampleRate             = 16,
    audioMasterGetBlockSize              = 17,
    audioMasterGetInputLatency           = 18,
    audioMasterGetOutputLatency          = 19,
    audioMasterGetPreviousPlug           = 20, // deprecated
    audioMasterGetNextPlug               = 21, // deprecated
    audioMasterWillReplaceOrAccumulate   = 22, // deprecated
    audioMasterGetCurrentProcessLevel    = 23,
    audioMasterGetAutomationState        = 24,
    audioMasterOfflineStart              = 25,
    audioMasterOfflineRead               = 26,
    audioMasterOfflineWrite              = 27,
    audioMasterOfflineGetCurrentPass     = 28,
    audioMasterOfflineGetCurrentMetaPass = 29,
    audioMasterSetOutputSampleRate       = 30, // deprecated
    audioMasterGetOutputSpeakerArrangement = 31, // deprecated
    audioMasterGetVendorString           = 32,
    audioMasterGetProductString          = 33,
    audioMasterGetVendorVersion          = 34,
    audioMasterVendorSpecific            = 35,
    audioMasterSetIcon                   = 36, // deprecated
    audioMasterCanDo                     = 37,
    audioMasterGetLanguage               = 38,
    audioMasterOpenWindow                = 39, // deprecated
    audioMasterCloseWindow               = 40, // deprecated
    audioMasterGetDirectory              = 41,
    audioMasterUpdateDisplay             = 42,
    audioMasterBeginEdit                 = 43,
    audioMasterEndEdit                   = 44
};

// ─── Process precision ─────────────────────────────────────────────
enum VstProcessPrecision
{
    kVstProcessPrecision32 = 0,
    kVstProcessPrecision64 = 1
};

// ─── Plugin categories ─────────────────────────────────────────────
enum VstPlugCategory
{
    kPlugCategUnknown        = 0,
    kPlugCategEffect         = 1,
    kPlugCategSynth          = 2,
    kPlugCategAnalysis       = 3,
    kPlugCategMastering      = 4,
    kPlugCategSpacializer    = 5,
    kPlugCategRoomFx         = 6,
    kPlugSurroundFx          = 7,
    kPlugCategRestoration    = 8,
    kPlugCategOfflineProcess = 9,
    kPlugCategShell          = 10,
    kPlugCategGenerator      = 11,
    kPlugCategMaxCount       = 12
};

// ─── Time info ─────────────────────────────────────────────────────
enum VstTimeInfoFlags
{
    kVstTransportChanged     = 1,
    kVstTransportPlaying     = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstTransportRecording   = 1 << 3,
    kVstAutomationWriting    = 1 << 6,
    kVstAutomationReading    = 1 << 7,
    kVstNanosValid           = 1 << 8,
    kVstPpqPosValid          = 1 << 9,
    kVstTempoValid           = 1 << 10,
    kVstBarsValid            = 1 << 11,
    kVstCyclePosValid        = 1 << 12,
    kVstTimeSigValid         = 1 << 13,
    kVstSmpteValid           = 1 << 14,
    kVstClockValid           = 1 << 15
};

enum VstSmpteFrameRate
{
    kVstSmpte24fps    = 0,
    kVstSmpte25fps    = 1,
    kVstSmpte2997fps  = 2,
    kVstSmpte30fps    = 3,
    kVstSmpte2997dfps = 4,
    kVstSmpte30dfps   = 5,

    kVstSmpte239fps   = 10,
    kVstSmpte249fps   = 11,
    kVstSmpte599fps   = 12,
    kVstSmpte60fps    = 13
};

struct VstTimeInfo
{
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    VstInt32 timeSigNumerator;
    VstInt32 timeSigDenominator;
    VstInt32 smpteOffset;
    VstInt32 smpteFrameRate;
    VstInt32 samplesToNextClock;
    VstInt32 flags;
};

// ─── Events ────────────────────────────────────────────────────────
enum VstEventTypes
{
    kVstMidiType  = 1,
    kVstSysExType = 6
};

struct VstEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    char data[16];
};

struct VstEvents
{
    VstInt32 numEvents;
    VstIntPtr reserved;
    VstEvent* events[2];  // variable size
};

struct VstMidiEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 noteLength;
    VstInt32 noteOffset;
    char     midiData[4];
    char     detune;
    char     noteOffVelocity;
    char     reserved1;
    char     reserved2;
};

struct VstMidiSysexEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 dumpBytes;
    VstIntPtr resvd1;
    char*    sysexDump;
    VstIntPtr resvd2;
};

// ─── Pin properties ────────────────────────────────────────────────
enum VstPinPropertiesFlags
{
    kVstPinIsActive   = 1 << 0,
    kVstPinIsStereo   = 1 << 1,
    kVstPinUseSpeaker = 1 << 2
};

struct VstPinProperties
{
    char label[kVstMaxNameLen];
    VstInt32 flags;
    VstInt32 arrangementType;
    char shortLabel[8];
    char future[48];
};

// ─── Speaker arrangement ───────────────────────────────────────────
enum VstSpeakerType
{
    kSpeakerUndefined = 0x7fffffff,
    kSpeakerM         = 0,
    kSpeakerL         = 1,
    kSpeakerR         = 2,
    kSpeakerC         = 3,
    kSpeakerLfe       = 4,
    kSpeakerLs        = 5,
    kSpeakerRs        = 6,
    kSpeakerLc        = 7,
    kSpeakerRc        = 8,
    kSpeakerS         = 9,
    kSpeakerSl        = 10,
    kSpeakerSr        = 11,
    kSpeakerTm        = 12,
    kSpeakerTfl       = 13,
    kSpeakerTfc       = 14,
    kSpeakerTfr       = 15,
    kSpeakerTrl       = 16,
    kSpeakerTrc       = 17,
    kSpeakerTrr       = 18,
    kSpeakerLfe2      = 19
};

enum VstSpeakerArrangementType
{
    kSpeakerArrUserDefined    = -2,
    kSpeakerArrEmpty          = -1,
    kSpeakerArrMono           = 0,
    kSpeakerArrStereo         = 1,
    kSpeakerArrStereoSurround = 2,
    kSpeakerArrStereoCenter   = 3,
    kSpeakerArrStereoSide     = 4,
    kSpeakerArrStereoCLfe     = 5,
    kSpeakerArr30Cine         = 6,
    kSpeakerArr30Music        = 7,
    kSpeakerArr31Cine         = 8,
    kSpeakerArr31Music        = 9,
    kSpeakerArr40Cine         = 10,
    kSpeakerArr40Music        = 11,
    kSpeakerArr41Cine         = 12,
    kSpeakerArr41Music        = 13,
    kSpeakerArr50             = 14,
    kSpeakerArr51             = 15,
    kSpeakerArr60Cine         = 16,
    kSpeakerArr60Music        = 17,
    kSpeakerArr61Cine         = 18,
    kSpeakerArr61Music        = 19,
    kSpeakerArr70Cine         = 20,
    kSpeakerArr70Music        = 21,
    kSpeakerArr71Cine         = 22,
    kSpeakerArr71Music        = 23,
    kSpeakerArr80Cine         = 24,
    kSpeakerArr80Music        = 25,
    kSpeakerArr81Cine         = 26,
    kSpeakerArr81Music        = 27,
    kSpeakerArr102            = 28
};

struct VstSpeakerProperties
{
    float azimuth;
    float elevation;
    float radius;
    float reserved;
    char  name[kVstMaxNameLen];
    VstInt32 type;
    char future[28];
};

struct VstSpeakerArrangement
{
    VstInt32 type;
    VstInt32 numChannels;
    VstSpeakerProperties speakers[8];  // variable size
};

// ─── Editor rectangle ──────────────────────────────────────────────
struct ERect
{
    VstInt16 top;
    VstInt16 left;
    VstInt16 bottom;
    VstInt16 right;
};
