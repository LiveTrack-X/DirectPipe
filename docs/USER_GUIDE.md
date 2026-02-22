# DirectPipe User Guide

## What is DirectPipe?

DirectPipe lets you apply VST audio effects to your USB microphone and send
the processed audio to OBS, Discord, Zoom, and other applications with
ultra-low latency (3-12ms) — without needing a hardware audio interface.

## Quick Start

1. **Launch DirectPipe**
2. **Select your microphone** from the INPUT dropdown
3. **Add VST plugins** to the chain (click "+ Add Plugin")
4. **Configure outputs**:
   - OBS Plugin: Enable for OBS Studio integration
   - Virtual Mic: Enable for Discord/Zoom
   - Monitor: Enable to hear yourself through headphones

## OBS Studio Setup

1. Install DirectPipe (OBS plugin included)
2. In OBS, add a new Audio Source
3. Select "DirectPipe Audio" from the source list
4. The plugin automatically connects when DirectPipe is running

## Discord/Zoom Setup

1. Install VB-Cable (free) from https://vb-audio.com/Cable
2. DirectPipe will auto-detect the virtual cable
3. In Discord/Zoom, set your input device to "CABLE Output (VB-Audio)"

## Recommended Free VST Plugins

- **ReaPlugs** (ReaEQ, ReaComp) — by Cockos
- **TDR Nova** — by Tokyo Dawn Records
- **MeldaProduction MFreeFXBundle**

## Latency Settings

| Buffer Size | Latency (OBS) | Latency (Virtual Mic) | Notes |
|-------------|---------------|----------------------|-------|
| 64 samples  | ~3ms          | ~6ms                 | Lowest latency, needs fast CPU |
| 128 samples | ~5ms          | ~8ms                 | Recommended default |
| 256 samples | ~8ms          | ~13ms                | More stable, higher latency |
| 512 samples | ~13ms         | ~24ms                | For slower systems |

## Troubleshooting

**No audio input?**
- Check that the correct microphone is selected
- Verify the microphone is not in use by another exclusive-mode app

**Glitches or dropouts?**
- Increase the buffer size (128 → 256)
- Close other audio applications
- Check CPU usage in the status bar

**OBS shows "Waiting..."?**
- Make sure DirectPipe is running
- Restart the OBS source

**Virtual mic not detected?**
- Install VB-Cable and restart DirectPipe
- Check that VB-Cable appears in Windows Sound Settings
