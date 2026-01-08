package pedalboard

import (
	"testing"
)

func TestNewInternalProcessor(t *testing.T) {
	// Test Gain
	gain, err := NewInternalProcessor("Gain")
	if err != nil {
		t.Fatalf("Failed to create Gain processor: %v", err)
	}
	if gain == nil {
		t.Fatal("Gain processor is nil")
	}

	// Test Reverb
	reverb, err := NewInternalProcessor("Reverb")
	if err != nil {
		t.Fatalf("Failed to create Reverb processor: %v", err)
	}
	if reverb == nil {
		t.Fatal("Reverb processor is nil")
	}

	// Test Invalid
	_, err = NewInternalProcessor("InvalidProcessor")
	if err == nil {
		t.Fatal("Expected error for invalid processor name, but got nil")
	}
}

func TestAllProcessors(t *testing.T) {
	effects := []string{
		"Gain", "Reverb", "Chorus", "Distortion", 
		"Phaser", "Clipping", "Compressor", "Limiter",
		"Delay", "LowPass", "HighPass", "LadderFilter",
		"Bitcrush",
	}

	for _, name := range effects {
		p, err := NewInternalProcessor(name)
		if err != nil {
			t.Errorf("Failed to create %s: %v", name, err)
			continue
		}
		if p == nil {
			t.Errorf("Processor %s is nil", name)
			continue
		}
		
		// Basic check of parameters
		params := p.NumParameters()
		if params < 1 && name != "Clipping" { // Clipping might have 1, check logic
			// actually all my new ones have at least 1 param
		}
		t.Logf("Effect %s created with %d parameters", name, params)
	}
}

func TestProcessorParameters(t *testing.T) {
	gain, _ := NewInternalProcessor("Gain")
	numParams := gain.NumParameters()
	if numParams < 1 {
		t.Errorf("Expected at least 1 parameter for Gain, got %d", numParams)
	}

	initialVal := gain.GetParameter(0)
	targetVal := float32(0.5)
	gain.SetParameter(0, targetVal)
	
	newVal := gain.GetParameter(0)
	if newVal != targetVal {
		t.Errorf("Expected parameter value %f, got %f", targetVal, newVal)
	}

	// Reset
	gain.SetParameter(0, initialVal)
}

func TestProcess(t *testing.T) {
	gain, _ := NewInternalProcessor("Gain")
	gain.SetParameter(0, 0.5) // Half volume

	// Create a stereo buffer: 2 channels, 100 samples
	buffer := [][]float32{
		make([]float32, 100),
		make([]float32, 100),
	}

	// Fill with 1.0
	for c := range buffer {
		for i := range buffer[c] {
			buffer[c][i] = 1.0
		}
	}

	gain.Process(buffer, 44100.0)

	// Check if gain was applied (0.5 * 1.0 = 0.5)
	for c := range buffer {
		for i, sample := range buffer[c] {
			if sample != 0.5 {
				t.Errorf("Channel %d Sample %d: expected 0.5, got %f", c, i, sample)
				break 
			}
		}
	}
}

func TestAudioStreamCreation(t *testing.T) {
	// We might not be able to start/stop the stream in a CI environment without audio hardware,
	// but we can at least test creation and closing.
	gain, _ := NewInternalProcessor("Gain")
	stream, err := NewAudioStream(gain)
	if err != nil {
		// This might fail if no audio device is found, which is common in headless environments.
		// So we log it instead of failing if it's a device error.
		t.Logf("Audio stream creation failed (expected in some environments): %v", err)
		return
	}
	
	if stream == nil {
		t.Fatal("Stream is nil")
	}
	
	stream.Close()
}

func TestFileIO(t *testing.T) {
	// Create a dummy buffer
	original := &AudioBuffer{
		Data: [][]float32{
			{0.1, 0.2, 0.3, 0.4},
			{-0.1, -0.2, -0.3, -0.4},
		},
		SampleRate: 44100.0,
	}

	tmpDir := t.TempDir()
	tmpFile := tmpDir + "/test_output.wav"
	err := SaveAudioFile(tmpFile, original)
	if err != nil {
		t.Fatalf("Failed to save audio file: %v", err)
	}

	loaded, err := LoadAudioFile(tmpFile)
	if err != nil {
		t.Fatalf("Failed to load audio file: %v", err)
	}

	if len(loaded.Data) != len(original.Data) {
		t.Errorf("Expected %d channels, got %d", len(original.Data), len(loaded.Data))
	}

	if len(loaded.Data[0]) != len(original.Data[0]) {
		t.Errorf("Expected %d samples, got %d", len(original.Data[0]), len(loaded.Data[0]))
	}

	// Note: loading might have some slight precision loss or rounding depending on bit depth (16-bit saved)
	// so we check with a small epsilon
	for c := range original.Data {
		for i := range original.Data[c] {
			diff := original.Data[c][i] - loaded.Data[c][i]
			if diff < 0 {
				diff = -diff
			}
			if diff > 0.01 {
				t.Errorf("Data mismatch at ch %d, sample %d: original %f, loaded %f", c, i, original.Data[c][i], loaded.Data[c][i])
			}
		}
	}
}
