# Contributing to go-pedalboard

Thank you for your interest in contributing! This project aims to bring the power of JUCE and professional audio processing to the Go ecosystem.

## Development Setup

### Prerequisites

- **Go**: 1.25 or later.
- **CMake**: 3.22 or later.
- **C++ Compiler**: Clang (macOS) or GCC (Linux) with C++17 support.
- **JUCE**: The repository includes JUCE as a submodule in `third_party/JUCE`.

### Building

We use a `Makefile` to manage the complex build process involving both C++ and Go.

1. **Clone the repository**:
   ```bash
   git clone https://github.com/brianliu/go-pedalboard
   cd go-pedalboard
   ```

2. **Build the C++ bridge and Go library**:
   ```bash
   make build-go
   ```

## Project Structure

- `/cpp`: Contains the C++ wrapper around JUCE.
    - `/cpp/include`: Public C headers used by CGO.
    - `/cpp/src`: C++ implementation of the bridge.
- `/pkg/pedalboard`: The Go package that wraps the C bridge.
- `/examples`: Usage examples.

## Adding New Features

### Adding a new Internal Processor
1. Define your `juce::AudioProcessor` subclass in `cpp/src/pedalboard.cpp`.
2. Update `pedalboard_create_internal_processor` to recognize your processor name.
3. (Optional) Expose specific parameters if they don't follow the standard JUCE parameter system.

### Modifying the C API
If you need to expose new JUCE functionality to Go:
1. Update `cpp/include/pedalboard.h` with the new C function signature.
2. Implement the function in `cpp/src/pedalboard.cpp`.
3. Add the corresponding Go wrapper in `pkg/pedalboard/pedalboard.go`.

## Code Style

- **Go**: Follow standard `gofmt` and idiomatic Go patterns.
- **C++**: Follow JUCE coding standards (camelCase for methods, etc.).

## License

By contributing to this project, you agree that your contributions will be licensed under the Apache License 2.0. Note that JUCE itself has its own licensing requirements (GPLv3/Commercial).
