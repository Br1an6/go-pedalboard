# go-pedalboard

![version](https://img.shields.io/badge/version-0.0.1-blue)

![alt tag](https://github.com/Br1an6/go-pedalboard/blob/main/img/pedal.png)

A Go library for working with audio: reading, writing, rendering, adding effects, and more. It supports popular audio file formats and provides a Go-idiomatic interface for loading third-party software instruments and effects (VST3® and Audio Unit).

Inspired by [Spotify's Pedalboard](https://github.com/spotify/pedalboard).

## Features

- **Live Audio Processing**: Process audio in real-time using default input/output devices (like `AudioStream`).
- **VST3® & Audio Unit Hosting**: Load third-party plugins directly in Go.
- **Audio Effects**: Built-in common audio effects powered by [JUCE](https://github.com/juce-framework/JUCE).
- **Format Support**: Support for most popular audio file formats.
- **High Performance**: Low-latency audio processing using a C++ bridge to JUCE.

## Architecture

`go-pedalboard` uses CGO to bridge between Go and a C++ wrapper around the JUCE framework.

```text
Go Application <-> CGO <-> C++ Bridge <-> JUCE Framework
```

## Usage

### Offline Processing (File to File)

```go
package main

import (
	"github.com/brianliu/go-pedalboard/pkg/pedalboard"
)

func main() {
	// Load an audio file
	buffer, _ := pedalboard.LoadAudioFile("input.wav")

	// Create a processor
	// Internal: "Gain", "Reverb", "Chorus", "Distortion", etc.
	reverb, _ := pedalboard.NewInternalProcessor("Reverb")
	
	// Set parameters (Room Size)
	reverb.SetParameter(0, 0.8)

    // Or load a plugin:
    // plugin, _ := pedalboard.LoadPlugin("/path/to/plugin.vst3")

	// Process audio
	reverb.Process(buffer.Data, buffer.SampleRate)

	// Save the result
	pedalboard.SaveAudioFile("output.wav", buffer)
}
```

### Live Audio Stream

```go
package main

import (
	"time"
	"github.com/brianliu/go-pedalboard/pkg/pedalboard"
)

func main() {
	// Create an effect
	reverb, _ := pedalboard.NewInternalProcessor("Reverb")
	reverb.SetParameter(0, 0.9) // Large room size

	// Create a live audio stream
	stream, err := pedalboard.NewAudioStream(reverb)
	if err != nil {
		panic(err)
	}
	defer stream.Close()

	// Start processing
	stream.Start()

	// Run for 10 seconds
	time.Sleep(10 * time.Second)

	// Stop processing
	stream.Stop()
}
```

## Available Internal Effects

Parameters are typically normalized (0.0 - 1.0) unless otherwise noted.

| Effect | Parameter 0 | Parameter 1 | Parameter 2 | Parameter 3 | Parameter 4 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Gain** | Gain | - | - | - | - |
| **Reverb** | Room Size | Damping | Wet Level | Dry Level | Width |
| **Delay** | Time (0-2s) | Feedback | Mix | - | - |
| **Chorus** | Rate | Depth | Delay | Feedback | Mix |
| **Phaser** | Rate | Depth | Freq | Feedback | Mix |
| **Distortion** | Drive | - | - | - | - |
| **Clipping** | Threshold | - | - | - | - |
| **Compressor** | Threshold | Ratio | Attack | Release | - |
| **Limiter** | Threshold | Release | - | - | - |
| **LowPass** | Cutoff | Q | - | - | - |
| **HighPass** | Cutoff | Q | - | - | - |
| **LadderFilter** | Cutoff | Resonance | Drive | - | - |
| **Bitcrush** | Bit Depth (32-2) | Downsample (1-50x) | - | - | - |

## Building

This library requires a C++ compiler and CMake to build the JUCE bridge.

```bash
make build-go
```

## License

This project is licensed under the Apache License 2.0. JUCE is licensed under its own terms (GPL/Commercial).
