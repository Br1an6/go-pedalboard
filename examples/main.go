package main

import (
	"fmt"
	"log"
	"os"

	"github.com/brianliu/go-pedalboard/pkg/pedalboard"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Println("Usage: go run examples/main.go <input_file> <output_file>")
		return
	}

	inputPath := os.Args[1]
	outputPath := os.Args[2]

	// 1. Load audio file
	buffer, err := pedalboard.LoadAudioFile(inputPath)
	if err != nil {
		log.Fatalf("Failed to load audio file: %v", err)
	}
	fmt.Printf("Loaded audio file: %s (%d channels, %d samples, %.1f Hz)\n",
		inputPath, len(buffer.Data), len(buffer.Data[0]), buffer.SampleRate)

	// 2. Create a Gain processor
	gain, err := pedalboard.NewInternalProcessor("Gain")
	if err != nil {
		log.Fatalf("Failed to create gain processor: %v", err)
	}

	// 3. Process the audio
	// For simplicity, we process the whole buffer at once
	// In a real app, you might want to process in blocks
	fmt.Println("Applying Gain...")
	gain.Process(buffer.Data, buffer.SampleRate)

	// 4. Save the result
	err = pedalboard.SaveAudioFile(outputPath, buffer)
	if err != nil {
		log.Fatalf("Failed to save audio file: %v", err)
	}
	fmt.Printf("Saved processed audio to: %s\n", outputPath)
}
