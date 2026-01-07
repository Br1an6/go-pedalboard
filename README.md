# go-pedalboard

A Go library for working with audio: reading, writing, rendering, adding effects, and more. It supports popular audio file formats and provides a Go-idiomatic interface for loading third-party software instruments and effects (VST3® and Audio Unit).

Inspired by [Spotify's Pedalboard](https://github.com/spotify/pedalboard).

## Features

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

```go
package main

import (
	"github.com/brianliu/go-pedalboard/pkg/pedalboard"
)

func main() {
	// Load an audio file
	buffer, _ := pedalboard.LoadAudioFile("input.wav")

	// Create a processor (internal or VST3/AU)
	gain, _ := pedalboard.NewInternalProcessor("Gain")
    // or: plugin, _ := pedalboard.LoadPlugin("/path/to/plugin.vst3")

	// Process audio
	gain.Process(buffer.Data, buffer.SampleRate)

	// Save the result
	pedalboard.SaveAudioFile("output.wav", buffer)
}
```

## Building

This library requires a C++ compiler and CMake to build the JUCE bridge.

```bash
make build-go
```

## License

This project is licensed under the Apache License 2.0. JUCE is licensed under its own terms (GPL/Commercial).
