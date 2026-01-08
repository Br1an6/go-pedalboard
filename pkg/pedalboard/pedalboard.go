package pedalboard

/*
#cgo CFLAGS: -I../../cpp/include
#cgo LDFLAGS: -L../../cpp/build -lpedalboard_static
#cgo darwin LDFLAGS: -framework Accelerate -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreAudioKit -framework CoreMIDI -framework CoreFoundation -framework Foundation -framework AppKit -framework WebKit -framework CoreVideo -framework IOKit -framework QuartzCore -framework CoreImage -framework Security -lstdc++

#include "pedalboard.h"
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"runtime"
	"unsafe"
)

func init() {
	C.pedalboard_init()
}

// Processor represents an audio processor (internal effect or external plugin).
// It wraps a JUCE AudioProcessor instance.
type Processor struct {
	handle C.PedalboardProcessor
}

// NewInternalProcessor creates a new internal processor by name.
// Supported names: "Gain", "Reverb".
// Returns a pointer to the Processor or an error if creation failed.
func NewInternalProcessor(name string) (*Processor, error) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	handle := C.pedalboard_create_internal_processor(cName)
	if handle == nil {
		return nil, fmt.Errorf("failed to create processor: %s", name)
	}

	return wrapProcessor(handle), nil
}

// LoadPlugin loads a VST3 or AU plugin from the specified file path.
// path: The absolute path to the plugin file (e.g., .vst3 or .component).
// Returns a pointer to the Processor or an error if loading failed.
func LoadPlugin(path string) (*Processor, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	handle := C.pedalboard_load_plugin(cPath)
	if handle == nil {
		return nil, fmt.Errorf("failed to load plugin: %s", path)
	}

	return wrapProcessor(handle), nil
}

func wrapProcessor(handle C.PedalboardProcessor) *Processor {
	p := &Processor{handle: handle}
	runtime.SetFinalizer(p, func(obj *Processor) {
		C.pedalboard_processor_free(obj.handle)
	})
	return p
}

// AudioBuffer represents a multi-channel audio buffer in memory.
type AudioBuffer struct {
	// Data holds the audio samples as [channel][sample].
	Data       [][]float32
	// SampleRate is the sample rate of the audio data in Hz.
	SampleRate float64
}

// LoadAudioFile loads an audio file from disk into an AudioBuffer.
// path: The path to the audio file.
// Returns an AudioBuffer or an error if loading failed.
func LoadAudioFile(path string) (*AudioBuffer, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	cBuffer := C.pedalboard_load_audio_file(cPath)
	if cBuffer == nil {
		return nil, fmt.Errorf("failed to load audio file: %s", path)
	}
	defer C.pedalboard_audio_buffer_free(cBuffer)

	numChannels := int(cBuffer.num_channels)
	numSamples := int(cBuffer.num_samples)
	sampleRate := float64(cBuffer.sample_rate)

	data := make([][]float32, numChannels)
	cChannelData := unsafe.Slice(cBuffer.data, numChannels)
	for i := 0; i < numChannels; i++ {
		data[i] = make([]float32, numSamples)
		// Copy data from C to Go
		src := unsafe.Slice((*float32)(unsafe.Pointer(cChannelData[i])), numSamples)
		copy(data[i], src)
	}

	return &AudioBuffer{
		Data:       data,
		SampleRate: sampleRate,
	}, nil
}

// SaveAudioFile saves an AudioBuffer to a file.
// path: The output file path. Format is determined by extension (e.g., .wav, .aiff).
// buffer: The AudioBuffer to save.
// Returns an error if saving failed.
func SaveAudioFile(path string, buffer *AudioBuffer) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	numChannels := len(buffer.Data)
	if numChannels == 0 {
		return fmt.Errorf("empty buffer")
	}
	numSamples := len(buffer.Data[0])

	// Create C buffer structure
	var cBuffer C.PedalboardAudioBuffer
	cBuffer.num_channels = C.int(numChannels)
	cBuffer.num_samples = C.int(numSamples)
	cBuffer.sample_rate = C.double(buffer.SampleRate)

	// Allocate pointer array for C
	cData := (**C.float)(C.malloc(C.size_t(numChannels) * C.size_t(unsafe.Sizeof((*C.float)(nil)))))
	if cData == nil {
		return fmt.Errorf("failed to allocate memory")
	}
	
	cDataSlice := unsafe.Slice(cData, numChannels)
	
	for i := 0; i < numChannels; i++ {
		cDataSlice[i] = (*C.float)(unsafe.Pointer(&buffer.Data[i][0]))
	}
	cBuffer.data = cData

	C.pedalboard_save_audio_file(cPath, &cBuffer)
	
	C.free(unsafe.Pointer(cData))

	return nil
}

// Process processes a block of audio data through the processor.
// buffer: The audio data to process (modified in-place).
// sampleRate: The sample rate of the audio data.
func (p *Processor) Process(buffer [][]float32, sampleRate float64) {
	numChannels := len(buffer)
	if numChannels == 0 {
		return
	}
	numSamples := len(buffer[0])

	// Allocate pointer array in C memory to avoid CGO pointer rules violation
	// (Go pointer to Go pointer in a C call).
	cPtrs := (**C.float)(C.malloc(C.size_t(numChannels) * C.size_t(unsafe.Sizeof((*C.float)(nil)))))
	if cPtrs == nil {
		return
	}
	defer C.free(unsafe.Pointer(cPtrs))

	cPtrsSlice := unsafe.Slice(cPtrs, numChannels)
	for i := 0; i < numChannels; i++ {
		cPtrsSlice[i] = (*C.float)(unsafe.Pointer(&buffer[i][0]))
	}

	C.pedalboard_processor_process(
		p.handle,
		cPtrs,
		C.int(numChannels),
		C.int(numSamples),
		C.double(sampleRate),
	)
}

// SetParameter sets a parameter value for the processor.
// index: The 0-based index of the parameter.
// value: The new value (typically normalized 0.0 to 1.0).
func (p *Processor) SetParameter(index int, value float32) {
	C.pedalboard_processor_set_parameter(p.handle, C.int(index), C.float(value))
}

// GetParameter returns the current value of a parameter.
// index: The 0-based index of the parameter.
// Returns the parameter value.
func (p *Processor) GetParameter(index int) float32 {
	return float32(C.pedalboard_processor_get_parameter(p.handle, C.int(index)))
}

// NumParameters returns the total number of parameters available in the processor.
func (p *Processor) NumParameters() int {
	return int(C.pedalboard_processor_get_num_parameters(p.handle))
}

// AudioStream represents a live audio stream processing audio from default input to output.
type AudioStream struct {
	handle    C.PedalboardAudioStream
	processor *Processor // Keep reference to prevent GC
}

// NewAudioStream creates a new audio stream using the specified processor.
// It opens the default audio input and output devices.
// processor: The processor to apply to the audio stream.
// Returns the AudioStream instance or an error.
func NewAudioStream(processor *Processor) (*AudioStream, error) {
	handle := C.pedalboard_create_audio_stream(processor.handle)
	if handle == nil {
		return nil, fmt.Errorf("failed to create audio stream")
	}
	return &AudioStream{handle: handle, processor: processor}, nil
}

// Start starts the audio processing on the stream.
func (s *AudioStream) Start() {
	C.pedalboard_audio_stream_start(s.handle)
}

// Stop stops the audio processing on the stream.
func (s *AudioStream) Stop() {
	C.pedalboard_audio_stream_stop(s.handle)
}

// Close releases the audio stream resources.
func (s *AudioStream) Close() {
	C.pedalboard_audio_stream_free(s.handle)
}