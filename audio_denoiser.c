/**
* @file
* @brief Real-time noise reduction tool using RNNoise and miniaudio.
*
* This application processes audio in real-time to reduce background noise
* using the RNNoise library, providing a simple GTK interface.
*
* @author Roberto Luiz Souza Monteiro
* @copyright Copyright (c) 2025 Roberto Luiz Souza Monteiro
* @license BSD 3-Clause License
*/

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gtk/gtk.h>
 
// Include RNNoise headers directly.
#include "rnnoise/include/rnnoise.h"

#define FRAME_SIZE 480     // RNNoise frame size (10ms at 48kHz).
#define SAMPLE_RATE 48000  // Expected sample rate for RNNoise.

/**
 * @brief Struct holding all application state.
 */
typedef struct {
    GtkWidget *window;
    GtkWidget *input_dev_combo;
    GtkWidget *output_dev_combo;
    GtkWidget *status_label;
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *vu_meter;
    
    ma_context context;
    ma_device device;
    DenoiseState *rnnoise_state;
    gboolean is_processing;
    ma_device_info *playback_infos;
    ma_device_info *capture_infos;
    ma_uint32 playback_count;
    ma_uint32 capture_count;
} AppState;

/**
 * @brief Callback for full-duplex input/output audio.
 */
static void duplex_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AppState *state = (AppState*)pDevice->pUserData;
    if (!state->is_processing || !pInput || !pOutput) return;

    int16_t *in  = (int16_t*)pInput;
    int16_t *out = (int16_t*)pOutput;

    float float_buffer[FRAME_SIZE];

    for (ma_uint32 i = 0; i < frameCount; i += FRAME_SIZE) {
        ma_uint32 block = (i + FRAME_SIZE <= frameCount) ? FRAME_SIZE : (frameCount - i);
    
        float sum = 0.0f;
    
        // Convert from int16 to float and calculate RMS.
        for (ma_uint32 j = 0; j < block; j++) {
            float_buffer[j] = (float)in[i + j] / 32768.0f;
            sum += float_buffer[j] * float_buffer[j];
        }
    
        float rms = sqrtf(sum / block);
        float db = 20.0f * log10f(rms + 1e-6f);
        float level = (db + 60.0f) / 60.0f;  // From 0 to 1.
    
        // RNNoise.
        rnnoise_process_frame(state->rnnoise_state, float_buffer, float_buffer);
    
        // BAck to int16.
        for (ma_uint32 j = 0; j < block; j++) {
            out[i + j] = (int16_t)(float_buffer[j] * 32768.0f);
        }
    
        // Update the VU meter
        g_idle_add((GSourceFunc)gtk_level_bar_set_value, state->vu_meter);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(state->vu_meter), level);
    }
}    

/**
 * @brief Initialize miniaudio.
 */
static void init_audio_devices(AppState *state) {
    ma_context_init(NULL, 0, NULL, &state->context);
    ma_context_get_devices(&state->context,
        &state->playback_infos, &state->playback_count,
        &state->capture_infos, &state->capture_count);

    for (ma_uint32 i = 0; i < state->capture_count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->input_dev_combo), state->capture_infos[i].name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->input_dev_combo), 0);

    for (ma_uint32 i = 0; i < state->playback_count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->output_dev_combo), state->playback_infos[i].name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->output_dev_combo), 0);
}

/**
 * @brief Starts audio processing.
 */
static void start_processing(AppState *state) {
    gint input_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(state->input_dev_combo));
    gint output_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(state->output_dev_combo));

    if (input_idx == -1 || output_idx == -1) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Please select input and output devices");
        return;
    }

    state->rnnoise_state = rnnoise_create(NULL);
    if (!state->rnnoise_state) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to initialize RNNoise");
        return;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.sampleRate         = SAMPLE_RATE;
    config.capture.format     = ma_format_s16;
    config.capture.channels   = 1;
    config.playback.format    = ma_format_s16;
    config.playback.channels  = 1;
    config.capture.pDeviceID  = &state->capture_infos[input_idx].id;
    config.playback.pDeviceID = &state->playback_infos[output_idx].id;
    config.dataCallback       = duplex_callback;
    config.pUserData          = state;

    if (ma_device_init(&state->context, &config, &state->device) != MA_SUCCESS) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to initialize device");
        rnnoise_destroy(state->rnnoise_state);
        state->rnnoise_state = NULL;
        return;
    }

    if (ma_device_start(&state->device) != MA_SUCCESS) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to start device");
        ma_device_uninit(&state->device);
        rnnoise_destroy(state->rnnoise_state);
        state->rnnoise_state = NULL;
        return;
    }

    state->is_processing = TRUE;
    gtk_label_set_text(GTK_LABEL(state->status_label), "Processing...");
    gtk_widget_set_sensitive(state->start_button, FALSE);
    gtk_widget_set_sensitive(state->stop_button, TRUE);
}

/**
 * @brief Ends processing.
 */
static void stop_processing(AppState *state) {
    if (state->is_processing) {
        ma_device_stop(&state->device);
        ma_device_uninit(&state->device);
        rnnoise_destroy(state->rnnoise_state);
        state->rnnoise_state = NULL;
        state->is_processing = FALSE;

        gtk_label_set_text(GTK_LABEL(state->status_label), "Stopped");
        gtk_widget_set_sensitive(state->start_button, TRUE);
        gtk_widget_set_sensitive(state->stop_button, FALSE);
    }
}

/**
 * @brief Callbacks for UI buttons.
 */
static void on_start(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    start_processing(state);
}

static void on_stop(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    stop_processing(state);
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    stop_processing(state);
    ma_context_uninit(&state->context);
    gtk_main_quit();
}

/**
 * @brief Entry point.
 */
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppState state = {0};
    state.is_processing = FALSE;

    // GUI setup.
    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "Real-time Noise Reduction");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 400, 200);
    gtk_container_set_border_width(GTK_CONTAINER(state.window), 10);
    g_signal_connect(state.window, "destroy", G_CALLBACK(on_window_destroy), &state);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(state.window), grid);

    GtkWidget *input_label = gtk_label_new("Input Device:");
    gtk_grid_attach(GTK_GRID(grid), input_label, 0, 0, 1, 1);
    state.input_dev_combo = gtk_combo_box_text_new();
    gtk_grid_attach(GTK_GRID(grid), state.input_dev_combo, 1, 0, 2, 1);

    GtkWidget *output_label = gtk_label_new("Output Device:");
    gtk_grid_attach(GTK_GRID(grid), output_label, 0, 1, 1, 1);
    state.output_dev_combo = gtk_combo_box_text_new();
    gtk_grid_attach(GTK_GRID(grid), state.output_dev_combo, 1, 1, 2, 1);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER); 
    gtk_grid_attach(GTK_GRID(grid), button_box, 0, 2, 3, 1);

    state.start_button = gtk_button_new_with_label("Start");
    g_signal_connect(state.start_button, "clicked", G_CALLBACK(on_start), &state);
    gtk_box_pack_start(GTK_BOX(button_box), state.start_button, FALSE, FALSE, 0);

    state.stop_button = gtk_button_new_with_label("Stop");
    g_signal_connect(state.stop_button, "clicked", G_CALLBACK(on_stop), &state);
    gtk_widget_set_sensitive(state.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(button_box), state.stop_button, FALSE, FALSE, 0);

    state.status_label = gtk_label_new("Select devices and click Start");
    gtk_grid_attach(GTK_GRID(grid), state.status_label, 0, 3, 3, 1);

    state.vu_meter = gtk_level_bar_new();
    gtk_level_bar_set_min_value(GTK_LEVEL_BAR(state.vu_meter), 0.0);
    gtk_level_bar_set_max_value(GTK_LEVEL_BAR(state.vu_meter), 1.0);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(state.vu_meter), 0.0);
    gtk_grid_attach(GTK_GRID(grid), state.vu_meter, 0, 4, 3, 1);

    init_audio_devices(&state);
    gtk_widget_show_all(state.window);
    gtk_main();

    return 0;
}