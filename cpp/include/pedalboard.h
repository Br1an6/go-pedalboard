#ifndef PEDALBOARD_H
#define PEDALBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// Handle types
typedef void* PedalboardProcessor;

// Global initialization
void pedalboard_init();

// Processor management
PedalboardProcessor pedalboard_create_internal_processor(const char* name);
PedalboardProcessor pedalboard_load_plugin(const char* path);
void pedalboard_processor_free(PedalboardProcessor processor);
void pedalboard_processor_set_parameter(PedalboardProcessor processor, int index, float value);
float pedalboard_processor_get_parameter(PedalboardProcessor processor, int index);
int pedalboard_processor_get_num_parameters(PedalboardProcessor processor);

// Audio processing
// samples is a pointer to an array of float pointers (one per channel)
void pedalboard_processor_process(PedalboardProcessor processor, float** samples, int num_channels, int num_samples, double sample_rate);

// Audio File IO
typedef struct {
    float** data;
    int num_channels;
    int num_samples;
    double sample_rate;
} PedalboardAudioBuffer;

PedalboardAudioBuffer* pedalboard_load_audio_file(const char* path);
void pedalboard_save_audio_file(const char* path, PedalboardAudioBuffer* buffer);
void pedalboard_audio_buffer_free(PedalboardAudioBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // PEDALBOARD_H
