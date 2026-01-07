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

// Processor represents an audio processor (internal or external plugin)
type Processor struct {
	handle C.PedalboardProcessor
}

// NewInternalProcessor creates a new JUCE internal processor by name
func NewInternalProcessor(name string) (*Processor, error) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	handle := C.pedalboard_create_internal_processor(cName)
	if handle == nil {
		return nil, fmt.Errorf("failed to create processor: %s", name)
	}

	return wrapProcessor(handle), nil
}

// LoadPlugin loads a VST3 or AU plugin from a file path
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

// AudioBuffer represents an audio buffer in memory
type AudioBuffer struct {
	Data       [][]float32
	SampleRate float64
}

// LoadAudioFile loads an audio file into an AudioBuffer
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
	for i := 0; i < numChannels; i++ {
		data[i] = make([]float32, numSamples)
		// Access the underlying pointer array
		cChannelData := *(*[]*C.float)(unsafe.Pointer(&cBuffer.data))
		// Copy data from C to Go
		src := unsafe.Slice((*float32)(unsafe.Pointer(cChannelData[i])), numSamples)
		copy(data[i], src)
	}

	return &AudioBuffer{
		Data:       data,
		SampleRate: sampleRate,
	}, nil
}

// SaveAudioFile saves an AudioBuffer to a file
func SaveAudioFile(path string, buffer *AudioBuffer) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	numChannels := len(buffer.Data)
	if numChannels == 0 {
		return fmt.Errorf("empty buffer")
	}
	numSamples := len(buffer.Data[0])

	// Create C buffer
	var cBuffer C.PedalboardAudioBuffer
	cBuffer.num_channels = C.int(numChannels)
	cBuffer.num_samples = C.int(numSamples)
	cBuffer.sample_rate = C.double(buffer.SampleRate)

	// Allocate pointer array for C
	cData := (**C.float)(C.malloc(C.size_t(numChannels) * C.size_t(unsafe.Sizeof((*C.float)(nil)))))
	cDataSlice := unsafe.Slice(cData, numChannels)
	
	// We don't want to copy all data if we can avoid it, but for Save we might need to if C expects it to stay.
	// Actually pedalboard_save_audio_file just reads from it.
	for i := 0; i < numChannels; i++ {
		cDataSlice[i] = (*C.float)(unsafe.Pointer(&buffer.Data[i][0]))
	}
	cBuffer.data = cData

	C.pedalboard_save_audio_file(cPath, &cBuffer)
	
	C.free(unsafe.Pointer(cData))

	return nil
}

// Process processes a block of audio
func (p *Processor) Process(buffer [][]float32, sampleRate float64) {
	numChannels := len(buffer)
	if numChannels == 0 {
		return
	}
	numSamples := len(buffer[0])

	// Create a pointer array for channels
	channelPtrs := make([]*C.float, numChannels)
	for i := 0; i < numChannels; i++ {
		channelPtrs[i] = (*C.float)(unsafe.Pointer(&buffer[i][0]))
	}

	C.pedalboard_processor_process(
		p.handle,
		(**C.float)(unsafe.Pointer(&channelPtrs[0])),
		C.int(numChannels),
		C.int(numSamples),
		C.double(sampleRate),
	)
}

// SetParameter sets a parameter value (0.0 to 1.0)
func (p *Processor) SetParameter(index int, value float32) {
	C.pedalboard_processor_set_parameter(p.handle, C.int(index), C.float(value))
}

// GetParameter gets a parameter value (0.0 to 1.0)
func (p *Processor) GetParameter(index int) float32 {
	return float32(C.pedalboard_processor_get_parameter(p.handle, C.int(index)))
}

// NumParameters returns the number of parameters supported by the processor
func (p *Processor) NumParameters() int {
	return int(C.pedalboard_processor_get_num_parameters(p.handle))
}
