# SampleForge

A polyphonic multisampled sampler VST built in C++ using the [JUCE Framework](https://juce.com/) and CMake.

## Features

- **Multi-Zone Sample Mapping**: Add any number of samples as zones, each with its own key range, velocity range, and root note, to build a real multisampled instrument instead of one sample stretched across the whole keyboard.
- **RAM-Based Sample Playback**: Load a WAV/AIFF/FLAC/MP3/OGG file per zone and play it back polyphonically (16 voices) via MIDI.
- **FL Studio-Style Trim**: Drag the Start/End handles directly on the waveform display to slice the selected zone's sample without leaving the plugin.
- **Root-Note Pitch Tracking**: Set each zone's original pitch; every other key speeds up or slows down playback to match, using 4-point Lagrange interpolation for clean resampling.
- **Reverse Playback**: Any zone can play back-to-front from its trim End instead of its Start, including while looping.
- **Per-Zone Pan & Gain**: Balance and level each zone independently (equal-power pan) on top of the global Gain, for a properly mixed multisampled instrument.
- **Round-Robin / Random Cycling**: Assign zones sharing a key/velocity range to a Round-Robin group; each hit sounds exactly one member (sequential or random, set via RR Order) instead of layering all of them.
- **Filter**: A per-voice Low-pass/High-pass/Band-pass filter with cutoff, resonance, and an envelope-amount control that lets the same ADSR shaping the amplitude also sweep the cutoff.
- **Time-Stretch**: A Stretch control (0.25x-4x) decouples playback duration from pitch, powered by the [Rubber Band Library](https://breakfastquay.com/rubberband/) (see Licensing below). Currently only applies to non-looping, non-reversed zones.
- **Drag-and-Drop**: Drop an audio file onto the waveform view (or anywhere else in the window) to add it as a new zone, no file picker required.
- **Looping**: Forward and ping-pong looping between a zone's Start/End trim points, selectable via the Loop control; trim handles double as live loop points. An optional Loop Sync rate (1/1 down to 1/16) overrides the free-running loop length with one derived from the host's tempo instead.
- **Polyphonic ADSR**: Full envelope control (Attack, Decay, Sustain, Release) across all voices and zones, re-evaluated every block so trim/envelope tweaks are heard immediately.
- **Session Persistence**: Every zone's file path, key/velocity range, root note, trim, reverse/pan/gain/RR-group setting, plus all envelope/loop/filter settings, are saved with the plugin state and restored on reload.
- **Preset Save/Load**: The entire instrument (every zone plus all global settings) can be saved to or loaded from a standalone `.sfpreset` file, independent of any DAW project.
- **Cached Waveform Display**: Each zone builds its own `juce::AudioThumbnail` once on load, so the waveform view never rescans sample data on zone switches or window resizes, and stays responsive on long material.
- **Thread-safe Processing**: Parameter reads use `juce::AudioProcessorValueTreeState` atomics; sample loading happens on the message thread and is swapped into the synth via JUCE's internally-locked `clearSounds()`/`addSound()`, so the audio thread never touches disk.

## Architecture & Code Map

* `CMakeLists.txt` - Project structure configuration using `FetchContent`. Builds a universal (Intel + Apple Silicon) binary.
* `PluginProcessor.h/cpp` - The audio core logic. Defines `SampleForgeSound` (one zone: buffer, key/velocity range, root note, trim, reverse/pan/gain/RR-group, cached `AudioThumbnail`), `SampleForgeVoice` (per-voice filter, tempo-synced looping, and RubberBandStretcher-backed time-stretch), `SampleForgeSynthesiser` (round-robin note-on routing), zone loading, and state/preset management.
* `PluginEditor.h/cpp` - The visual interface, including the zone list, per-zone controls, the draggable/drop-target `WaveformDisplay` component, and parameter binding.
* `SampleForgeLookAndFeel.h` - The flat, single-accent-color visual theme applied across all controls.

## Licensing

SampleForge is licensed under the [GNU GPL v3](LICENSE). Its **Time-Stretch** feature vendors the [Rubber Band Library](https://breakfastquay.com/rubberband/) (GPL v2+), fetched and compiled directly into the binary — see `CMakeLists.txt`. GPL v3 was chosen because it's compatible with Rubber Band's "v2 or later" terms. That has real implications if this plugin is ever given to someone else:

- **Using it privately**: no restriction of any kind, regardless of what you do with the audio it produces.
- **Sharing a recording/song made with it**: no restriction — GPL covers the *software*, not output produced by using it.
- **Distributing the plugin binary itself** (even for free, even to one other person): technically GPL "distribution," satisfied here since the source is public in this repo. To distribute it as closed-source instead (e.g. selling it, or through an app store — Rubber Band's GPL terms explicitly disallow App Store distribution), a commercial license from Breakfast Quay would be required, and the GPL v3 license here would need to be dropped in favor of one that permits that.

## Roadmap

This is still a RAM-only sampler. Natural next step:

- Disk streaming with a preload buffer for large/multi-gig instruments.

## How To Build

Make sure you have CMake and Ninja installed. Then navigate to the project directory in your terminal and run:

```bash
# Provide the Release build instructions to CMake:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Execute the compiler target globally:
cmake --build build
```

The resulting plugins will be placed in `build/SampleForge_artefacts/Release/`.

## How To Run Without A DAW

Run the interactive standalone preview directly:
```bash
open build/SampleForge_artefacts/Release/Standalone/SampleForge.app
```

## Author

**Gary A. Newsome**
**(c) 2026**
