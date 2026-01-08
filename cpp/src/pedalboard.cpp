#include "pedalboard.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

extern "C" {

// --- Helper Functions ---
float mapRange(float input, float min, float max) {
    return min + input * (max - min);
}

float mapRangeLog(float input, float min, float max) {
    return min * std::pow(max / min, input);
}

class PedalboardInternal {
public:
    PedalboardInternal() {
        juce::MessageManager::getInstance();
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

// --- Base Processor Class ---
class BaseInternalProcessor : public juce::AudioProcessor {
public:
    BaseInternalProcessor(const juce::String& name) 
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          procName(name) {}
    
    ~BaseInternalProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (uint32_t)samplesPerBlock;
        spec.numChannels = (uint32_t)getTotalNumOutputChannels();
        prepare(spec);
    }
    
    virtual void prepare(const juce::dsp::ProcessSpec& spec) {}

    void releaseResources() override { reset(); }
    virtual void reset() {}

    const juce::String getName() const override { return procName; }
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

    virtual void setParam(int index, float value) = 0;
    virtual float getParam(int index) = 0;
    virtual int getNumParams() = 0;

private:
    juce::String procName;
};


// --- Gain ---
class GainProcessor : public BaseInternalProcessor {
public:
    GainProcessor() : BaseInternalProcessor("Gain") {}
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        gain.prepare(spec);
        gain.setRampDurationSeconds(0.05);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        gain.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) gain.setGainLinear(value);
    }
    float getParam(int index) override {
        if (index == 0) return gain.getGainLinear();
        return 0.0f;
    }
    int getNumParams() override { return 1; }

    juce::dsp::Gain<float> gain;
};

// --- Reverb ---
class ReverbProcessor : public BaseInternalProcessor {
public:
    ReverbProcessor() : BaseInternalProcessor("Reverb") {
        params.roomSize = 0.5f;
        params.damping = 0.5f;
        params.wetLevel = 0.33f;
        params.dryLevel = 0.4f;
    }
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        reverb.setSampleRate(spec.sampleRate);
    }

    void reset() override { reverb.reset(); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (buffer.getNumChannels() == 1) {
             reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
        } else if (buffer.getNumChannels() == 2) {
             reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        }
    }

    void setParam(int index, float value) override {
        if (index == 0) params.roomSize = value;
        else if (index == 1) params.damping = value;
        else if (index == 2) params.wetLevel = value;
        else if (index == 3) params.dryLevel = value;
        else if (index == 4) params.width = value;
        reverb.setParameters(params);
    }
    float getParam(int index) override {
        if (index == 0) return params.roomSize;
        if (index == 1) return params.damping;
        if (index == 2) return params.wetLevel;
        if (index == 3) return params.dryLevel;
        if (index == 4) return params.width;
        return 0.0f;
    }
    int getNumParams() override { return 5; }

    juce::Reverb reverb;
    juce::Reverb::Parameters params;
};

// --- Delay ---
class DelayProcessor : public BaseInternalProcessor {
public:
    DelayProcessor() : BaseInternalProcessor("Delay") {
        delayLine.setMaximumDelayInSamples(192000); // Max ~4 sec at 48k
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override {
        sampleRate = spec.sampleRate;
        delayLine.prepare(spec);
        updateDelay();
    }

    void reset() override { delayLine.reset(); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        auto numSamples = buffer.getNumSamples();
        auto numChannels = buffer.getNumChannels();

        for (int ch = 0; ch < numChannels; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                float input = data[i];
                float delayedSample = delayLine.popSample(ch);
                
                // Simple feedback + mix
                float nextInput = input + (delayedSample * feedback);
                delayLine.pushSample(ch, nextInput);

                data[i] = (input * (1.0f - mix)) + (delayedSample * mix);
            }
        }
    }

    void updateDelay() {
        // Map 0-1 to 0-2 seconds
        float delaySec = mapRange(timeParam, 0.0f, 2.0f);
        float delaySamples = delaySec * (float)sampleRate;
        if (delaySamples < 1.0f) delaySamples = 1.0f;
        delayLine.setDelay(delaySamples);
    }

    void setParam(int index, float value) override {
        if (index == 0) { timeParam = value; updateDelay(); }
        else if (index == 1) feedback = value;
        else if (index == 2) mix = value;
    }
    float getParam(int index) override {
        if (index == 0) return timeParam;
        if (index == 1) return feedback;
        if (index == 2) return mix;
        return 0.0f;
    }
    int getNumParams() override { return 3; }

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    double sampleRate = 44100.0;
    
    float timeParam = 0.25f; // 0-1 mapped to 0-2s
    float feedback = 0.5f;
    float mix = 0.5f;
};

// --- Distortion (Tanh) ---
class DistortionProcessor : public BaseInternalProcessor {
public:
    DistortionProcessor() : BaseInternalProcessor("Distortion") {}

    void prepare(const juce::dsp::ProcessSpec& spec) override {
        shaper.prepare(spec);
        shaper.functionToUse = [](float x) { return std::tanh(x); };
        update();
    }
    
    void update() {
        // Drive 1.0 to 50.0
        float driveAmount = mapRangeLog(drive, 1.0f, 50.0f);
        inputGain.setGainLinear(driveAmount);
        // Compensation roughly 1/drive but tanh limits to 1 anyway
        outputGain.setGainLinear(1.0f / std::sqrt(driveAmount)); 
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        inputGain.process(context);
        shaper.process(context);
        outputGain.process(context);
    }

    void setParam(int index, float value) override {
        if (index == 0) { drive = value; update(); }
    }
    float getParam(int index) override { return drive; }
    int getNumParams() override { return 1; }

    float drive = 0.5f; // 0-1
    juce::dsp::Gain<float> inputGain, outputGain;
    juce::dsp::WaveShaper<float> shaper;
};

// --- Hard Clip ---
class ClippingProcessor : public BaseInternalProcessor {
public:
    ClippingProcessor() : BaseInternalProcessor("Clipping") {}

    void prepare(const juce::dsp::ProcessSpec& spec) override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        float thresh = mapRange(threshold, 0.1f, 1.0f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                data[i] = std::max(-thresh, std::min(thresh, data[i]));
            }
        }
    }

    void setParam(int index, float value) override {
        if (index == 0) threshold = value;
    }
    float getParam(int index) override { return threshold; }
    int getNumParams() override { return 1; }

    float threshold = 1.0f; // 1.0 = no clipping (if signal normalized), 0.1 = heavy
};

// --- Chorus ---
class ChorusProcessor : public BaseInternalProcessor {
public:
    ChorusProcessor() : BaseInternalProcessor("Chorus") {}
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        chorus.prepare(spec);
        update();
    }
    
    void update() {
        chorus.setRate(mapRange(rate, 0.1f, 5.0f));
        chorus.setDepth(depth);
        chorus.setCentreDelay(mapRange(delay, 1.0f, 30.0f));
        chorus.setFeedback(mapRange(feedback, -0.9f, 0.9f));
        chorus.setMix(mix);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) rate = value;
        else if (index == 1) depth = value;
        else if (index == 2) delay = value;
        else if (index == 3) feedback = value;
        else if (index == 4) mix = value;
        update();
    }
    float getParam(int index) override {
        if (index == 0) return rate;
        if (index == 1) return depth;
        if (index == 2) return delay;
        if (index == 3) return feedback;
        if (index == 4) return mix;
        return 0.0f;
    }
    int getNumParams() override { return 5; }
    
    float rate = 0.2f, depth = 0.5f, delay = 0.2f, feedback = 0.5f, mix = 0.5f;
    juce::dsp::Chorus<float> chorus;
};

// --- Phaser ---
class PhaserProcessor : public BaseInternalProcessor {
public:
    PhaserProcessor() : BaseInternalProcessor("Phaser") {}
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        phaser.prepare(spec);
        update();
    }
    
    void update() {
        phaser.setRate(mapRange(rate, 0.1f, 10.0f));
        phaser.setDepth(depth);
        phaser.setCentreFrequency(mapRangeLog(freq, 100.0f, 5000.0f));
        phaser.setFeedback(mapRange(feedback, -0.9f, 0.9f));
        phaser.setMix(mix);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        phaser.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) rate = value;
        else if (index == 1) depth = value;
        else if (index == 2) freq = value;
        else if (index == 3) feedback = value;
        else if (index == 4) mix = value;
        update();
    }
    float getParam(int index) override {
        if (index == 0) return rate;
        if (index == 1) return depth;
        if (index == 2) return freq;
        if (index == 3) return feedback;
        if (index == 4) return mix;
        return 0.0f;
    }
    int getNumParams() override { return 5; }
    
    float rate = 0.1f, depth = 0.5f, freq = 0.5f, feedback = 0.5f, mix = 0.5f;
    juce::dsp::Phaser<float> phaser;
};

// --- Compressor ---
class CompressorProcessor : public BaseInternalProcessor {
public:
    CompressorProcessor() : BaseInternalProcessor("Compressor") {}
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        compressor.prepare(spec);
        update();
    }
    
    void update() {
        compressor.setThreshold(mapRange(threshold, -60.0f, 0.0f));
        compressor.setRatio(mapRange(ratio, 1.0f, 20.0f));
        compressor.setAttack(mapRange(attack, 1.0f, 200.0f));
        compressor.setRelease(mapRange(release, 20.0f, 500.0f));
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        compressor.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) threshold = value;
        else if (index == 1) ratio = value;
        else if (index == 2) attack = value;
        else if (index == 3) release = value;
        update();
    }
    float getParam(int index) override {
        if (index == 0) return threshold;
        if (index == 1) return ratio;
        if (index == 2) return attack;
        if (index == 3) return release;
        return 0.0f;
    }
    int getNumParams() override { return 4; }
    
    float threshold = 0.8f, ratio = 0.2f, attack = 0.1f, release = 0.2f;
    juce::dsp::Compressor<float> compressor;
};

// --- Limiter ---
class LimiterProcessor : public BaseInternalProcessor {
public:
    LimiterProcessor() : BaseInternalProcessor("Limiter") {}
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        limiter.prepare(spec);
        update();
    }
    
    void update() {
        limiter.setThreshold(mapRange(threshold, -20.0f, 0.0f));
        limiter.setRelease(mapRange(release, 10.0f, 500.0f));
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        limiter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) threshold = value;
        else if (index == 1) release = value;
        update();
    }
    float getParam(int index) override {
        if (index == 0) return threshold;
        if (index == 1) return release;
        return 0.0f;
    }
    int getNumParams() override { return 2; }
    
    float threshold = 1.0f, release = 0.2f;
    juce::dsp::Limiter<float> limiter;
};

// --- Filters (IIR) ---
enum FilterType { LowPass, HighPass };
class FilterProcessor : public BaseInternalProcessor {
public:
    FilterProcessor(FilterType t) : BaseInternalProcessor(t == LowPass ? "LowPass" : "HighPass"), type(t) {}
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        sampleRate = spec.sampleRate;
        filter.prepare(spec);
        update();
    }
    
    void update() {
        float freqHz = mapRangeLog(cutoff, 20.0f, 20000.0f);
        float qVal = mapRange(q, 0.1f, 10.0f);
        
        if (type == LowPass)
            *filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, freqHz, qVal);
        else
            *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, freqHz, qVal);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        filter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) cutoff = value;
        else if (index == 1) q = value;
        update();
    }
    float getParam(int index) override {
        if (index == 0) return cutoff;
        if (index == 1) return q;
        return 0.0f;
    }
    int getNumParams() override { return 2; }
    
    FilterType type;
    double sampleRate = 44100.0;
    float cutoff = 0.5f, q = 0.1f;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> filter;
};

// --- Ladder Filter ---
class LadderProcessor : public BaseInternalProcessor {
public:
    LadderProcessor() : BaseInternalProcessor("LadderFilter") {
        ladder.setMode(juce::dsp::LadderFilterMode::LPF12);
    }
    
    void prepare(const juce::dsp::ProcessSpec& spec) override {
        ladder.prepare(spec);
        update();
    }
    
    void update() {
        ladder.setCutoffFrequencyHz(mapRangeLog(cutoff, 20.0f, 20000.0f));
        ladder.setResonance(mapRange(resonance, 0.0f, 1.0f));
        ladder.setDrive(mapRange(drive, 1.0f, 5.0f));
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::dsp::AudioBlock<float> block(buffer);
        ladder.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(int index, float value) override {
        if (index == 0) cutoff = value;
        else if (index == 1) resonance = value;
        else if (index == 2) drive = value;
        update();
    }
    float getParam(int index) override {
        if (index == 0) return cutoff;
        if (index == 1) return resonance;
        if (index == 2) return drive;
        return 0.0f;
    }
    int getNumParams() override { return 3; }
    
    float cutoff = 0.5f, resonance = 0.0f, drive = 0.0f;
    juce::dsp::LadderFilter<float> ladder;
};

// --- Bitcrush ---
class BitcrushProcessor : public BaseInternalProcessor {
public:
    BitcrushProcessor() : BaseInternalProcessor("Bitcrush") {}

    void prepare(const juce::dsp::ProcessSpec& spec) override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        int depth = (int)mapRange(bitDepth, 32.0f, 2.0f); // Inverted so 0=32bit, 1=2bit
        int down = (int)mapRange(downsample, 1.0f, 50.0f);
        
        float step = 1.0f / (float)(1 << (depth - 1));
        
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            auto* data = buffer.getWritePointer(ch);
            float lastVal = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                if (i % down == 0) {
                    float val = data[i];
                    // Quantize
                    if (depth < 32) {
                        val = std::floor(val / step + 0.5f) * step;
                    }
                    lastVal = val;
                }
                data[i] = lastVal;
            }
        }
    }

    void setParam(int index, float value) override {
        if (index == 0) bitDepth = value;
        else if (index == 1) downsample = value;
    }
    float getParam(int index) override {
        if (index == 0) return bitDepth;
        if (index == 1) return downsample;
        return 0.0f;
    }
    int getNumParams() override { return 2; }

    float bitDepth = 0.0f; // 0 (32bit) -> 1 (2bit)
    float downsample = 0.0f; // 0 (1x) -> 1 (50x)
};


// --- Factory ---

PedalboardProcessor pedalboard_create_internal_processor(const char* name) {
    juce::String processorName(name);
    
    std::unique_ptr<BaseInternalProcessor> proc;

    if (processorName == "Gain") proc = std::make_unique<GainProcessor>();
    else if (processorName == "Reverb") proc = std::make_unique<ReverbProcessor>();
    else if (processorName == "Chorus") proc = std::make_unique<ChorusProcessor>();
    else if (processorName == "Distortion") proc = std::make_unique<DistortionProcessor>();
    else if (processorName == "Clipping") proc = std::make_unique<ClippingProcessor>();
    else if (processorName == "Phaser") proc = std::make_unique<PhaserProcessor>();
    else if (processorName == "Compressor") proc = std::make_unique<CompressorProcessor>();
    else if (processorName == "Limiter") proc = std::make_unique<LimiterProcessor>();
    else if (processorName == "Delay") proc = std::make_unique<DelayProcessor>();
    else if (processorName == "LowPass") proc = std::make_unique<FilterProcessor>(LowPass);
    else if (processorName == "HighPass") proc = std::make_unique<FilterProcessor>(HighPass);
    else if (processorName == "LadderFilter") proc = std::make_unique<LadderProcessor>();
    else if (processorName == "Bitcrush") proc = std::make_unique<BitcrushProcessor>();

    if (proc) {
        auto wrapper = new ProcessorWrapper();
        wrapper->processor = std::move(proc);
        return static_cast<PedalboardProcessor>(wrapper);
    }
    
    return nullptr;
}

// ... Rest of the file (LoadPlugin, AudioIO, Stream) ...

PedalboardProcessor pedalboard_load_plugin(const char* path) {
    pedalboard_init();
    juce::File file(path);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    
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
    
    result->data = (float**)malloc(sizeof(float*) * result->num_channels);
    for (int i = 0; i < result->num_channels; ++i) {
        result->data[i] = (float*)malloc(sizeof(float) * result->num_samples);
    }
    
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
    if (processor) delete static_cast<ProcessorWrapper*>(processor);
}

void pedalboard_processor_set_parameter(PedalboardProcessor processor, int index, float value) {
    if (!processor) return;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    
    // Check if it's our internal base class
    if (auto* internal = dynamic_cast<BaseInternalProcessor*>(wrapper->processor.get())) {
        internal->setParam(index, value);
        return;
    }

    // External plugin
    auto& params = wrapper->processor->getParameters();
    if (index >= 0 && index < params.size()) {
        params[index]->setValueNotifyingHost(value);
    }
}

float pedalboard_processor_get_parameter(PedalboardProcessor processor, int index) {
    if (!processor) return 0.0f;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    
    if (auto* internal = dynamic_cast<BaseInternalProcessor*>(wrapper->processor.get())) {
        return internal->getParam(index);
    }

    auto& params = wrapper->processor->getParameters();
    if (index >= 0 && index < params.size()) {
        return params[index]->getValue();
    }
    return 0.0f;
}

int pedalboard_processor_get_num_parameters(PedalboardProcessor processor) {
    if (!processor) return 0;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    
    if (auto* internal = dynamic_cast<BaseInternalProcessor*>(wrapper->processor.get())) {
        return internal->getNumParams();
    }

    return wrapper->processor->getParameters().size();
}

void pedalboard_processor_process(PedalboardProcessor processor, float** samples, int num_channels, int num_samples, double sample_rate) {
    if (!processor) return;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    juce::AudioBuffer<float> buffer(samples, num_channels, num_samples);
    
    // Simplified prepare call if rate changes
    if (wrapper->processor->getSampleRate() != sample_rate) {
        wrapper->processor->prepareToPlay(sample_rate, num_samples);
    }
    
    wrapper->processor->processBlock(buffer, wrapper->midiBuffer);
}

// --- Audio Stream ---
class AudioStreamInternal : public juce::AudioIODeviceCallback {
public:
    AudioStreamInternal(ProcessorWrapper* proc) : processorWrapper(proc) {
        juce::String error = deviceManager.initialiseWithDefaultDevices(2, 2);
    }
    ~AudioStreamInternal() {
        stop();
        deviceManager.closeAudioDevice();
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override {
        juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
        for (int i = 0; i < numOutputChannels; ++i) {
            if (i < numInputChannels && inputChannelData[i] != nullptr) {
                buffer.copyFrom(i, 0, inputChannelData[i], numSamples);
            } else {
                buffer.clear(i, 0, numSamples);
            }
        }
        if (processorWrapper && processorWrapper->processor) {
             processorWrapper->processor->processBlock(buffer, processorWrapper->midiBuffer);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        if (processorWrapper && processorWrapper->processor) {
            processorWrapper->processor->prepareToPlay(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        }
    }

    void audioDeviceStopped() override {
         if (processorWrapper && processorWrapper->processor) {
            processorWrapper->processor->releaseResources();
        }
    }
    
    void start() { deviceManager.addAudioCallback(this); }
    void stop() { deviceManager.removeAudioCallback(this); }
    juce::AudioDeviceManager deviceManager;
    ProcessorWrapper* processorWrapper;
};

PedalboardAudioStream pedalboard_create_audio_stream(PedalboardProcessor processor) {
    pedalboard_init();
    if (!processor) return nullptr;
    auto* wrapper = static_cast<ProcessorWrapper*>(processor);
    return new AudioStreamInternal(wrapper);
}

void pedalboard_audio_stream_start(PedalboardAudioStream stream) {
    if (stream) static_cast<AudioStreamInternal*>(stream)->start();
}

void pedalboard_audio_stream_stop(PedalboardAudioStream stream) {
    if (stream) static_cast<AudioStreamInternal*>(stream)->stop();
}

void pedalboard_audio_stream_free(PedalboardAudioStream stream) {
    if (stream) delete static_cast<AudioStreamInternal*>(stream);
}

} // extern "C"
