#include "pedalboard.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

extern "C" {

class PedalboardInternal {
public:
    PedalboardInternal() {
        formatManager.registerBasicFormats();
        
#if JUCE_MAC
        pluginFormatManager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
        pluginFormatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
    }

    juce::AudioFormatManager formatManager;
    juce::AudioPluginFormatManager pluginFormatManager;
};

static PedalboardInternal* g_internal = nullptr;

void pedalboard_init() {
    if (g_internal == nullptr) {
        g_internal = new PedalboardInternal();
    }
}

struct ProcessorWrapper {
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiBuffer;
};

class GainProcessor : public juce::AudioProcessor {
public:
    GainProcessor() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}
    ~GainProcessor() override = default;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        buffer.applyGain(gain);
    }

    const juce::String getName() const override { return "Gain"; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    float gain = 1.0f;
};

PedalboardProcessor pedalboard_create_internal_processor(const char* name) {
    juce::String processorName(name);
    
    if (processorName == "Gain") {
        auto wrapper = new ProcessorWrapper();
        wrapper->processor = std::make_unique<GainProcessor>();
        return static_cast<PedalboardProcessor>(wrapper);
    }
    
    return nullptr;
}

PedalboardProcessor pedalboard_load_plugin(const char* path) {
    pedalboard_init();
    juce::File file(path);
    
    juce::OwnedArray<juce::PluginDescription> descriptions;
    
    // We might need to scan or just try all formats
    for (int i = 0; i < g_internal->pluginFormatManager.getNumFormats(); ++i) {
        auto* format = g_internal->pluginFormatManager.getFormat(i);
        format->findAllTypesForFile(descriptions, path);
        if (descriptions.size() > 0) break;
    }
    
    if (descriptions.size() == 0) return nullptr;
    
    juce::String error;
    auto plugin = g_internal->pluginFormatManager.createPluginInstance(*descriptions[0], 44100.0, 512, error);
    
    if (plugin == nullptr) return nullptr;
    
    auto wrapper = new ProcessorWrapper();
    wrapper->processor = std::move(plugin);
    return static_cast<PedalboardProcessor>(wrapper);
}

PedalboardAudioBuffer* pedalboard_load_audio_file(const char* path) {
    pedalboard_init();
    juce::File file(path);
    std::unique_ptr<juce::AudioFormatReader> reader(g_internal->formatManager.createReaderFor(file));
    
    if (reader == nullptr) return nullptr;
    
    auto* result = new PedalboardAudioBuffer();
    result->num_channels = (int)reader->numChannels;
    result->num_samples = (int)reader->lengthInSamples;
    result->sample_rate = reader->sampleRate;
    
    // Allocate data
    result->data = (float**)malloc(sizeof(float*) * result->num_channels);
    for (int i = 0; i < result->num_channels; ++i) {
        result->data[i] = (float*)malloc(sizeof(float) * result->num_samples);
    }
    
    // Read samples
    juce::AudioBuffer<float> tempBuffer(result->data, result->num_channels, result->num_samples);
    reader->read(&tempBuffer, 0, result->num_samples, 0, true, true);
    
    return result;
}

void pedalboard_save_audio_file(const char* path, PedalboardAudioBuffer* buffer) {
    if (buffer == nullptr) return;
    pedalboard_init();
    
    juce::File file(path);
    if (file.existsAsFile()) file.deleteFile();
    
    auto* format = g_internal->formatManager.findFormatForFileExtension(file.getFileExtension());
    if (format == nullptr) format = g_internal->formatManager.getDefaultFormat();
    if (format == nullptr) return;
    
    // Using the raw pointer version for simplicity as unique_ptr version is being tricky with options
    auto* stream = new juce::FileOutputStream(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(format->createWriterFor(stream, 
                                                                         buffer->sample_rate, 
                                                                         (unsigned int)buffer->num_channels, 
                                                                         16, 
                                                                         {}, 
                                                                         0));
    
    if (writer != nullptr) {
        juce::AudioBuffer<float> tempBuffer(buffer->data, buffer->num_channels, buffer->num_samples);
        writer->writeFromAudioSampleBuffer(tempBuffer, 0, buffer->num_samples);
    }
}

void pedalboard_audio_buffer_free(PedalboardAudioBuffer* buffer) {
    if (buffer == nullptr) return;
    for (int i = 0; i < buffer->num_channels; ++i) {
        free(buffer->data[i]);
    }
    free(buffer->data);
    delete buffer;
}

void pedalboard_processor_free(PedalboardProcessor processor) {
    if (processor) {
        delete static_cast<ProcessorWrapper*>(processor);
    }
}

void pedalboard_processor_set_parameter(PedalboardProcessor processor, int index, float value) {
    if (!processor) return;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    
    // For plugins, use the standard parameter system
    auto& params = wrapper->processor->getParameters();
    if (index >= 0 && index < params.size()) {
        params[index]->setValueNotifyingHost(value);
    } else if (auto* gainProc = dynamic_cast<GainProcessor*>(wrapper->processor.get())) {
        // Special case for our custom GainProcessor if it's not using parameters properly
        if (index == 0) gainProc->gain = value;
    }
}

float pedalboard_processor_get_parameter(PedalboardProcessor processor, int index) {
    if (!processor) return 0.0f;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    auto& params = wrapper->processor->getParameters();
    if (index >= 0 && index < params.size()) {
        return params[index]->getValue();
    } else if (auto* gainProc = dynamic_cast<GainProcessor*>(wrapper->processor.get())) {
        if (index == 0) return gainProc->gain;
    }
    return 0.0f;
}

int pedalboard_processor_get_num_parameters(PedalboardProcessor processor) {
    if (!processor) return 0;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    int num = wrapper->processor->getParameters().size();
    if (num == 0 && dynamic_cast<GainProcessor*>(wrapper->processor.get())) return 1;
    return num;
}

void pedalboard_processor_process(PedalboardProcessor processor, float** samples, int num_channels, int num_samples, double sample_rate) {
    if (!processor) return;
    
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    
    // Wrap the provided samples in a juce::AudioBuffer
    juce::AudioBuffer<float> buffer(samples, num_channels, num_samples);
    
    // Set sample rate and block size if they changed (simplified)
    if (wrapper->processor->getSampleRate() != sample_rate) {
        wrapper->processor->prepareToPlay(sample_rate, num_samples);
    }
    
    wrapper->processor->processBlock(buffer, wrapper->midiBuffer);
}

} // extern "C"
