/**
 * @file
 * @brief An audio filter using GTK and miniaudio.
 *
 * This application allows you to filter from an audio source and send the filtered audio an audio target.
 *
 * @author Roberto Luiz Souza Monteiro
 * @copyright Copyright (c) 2025 Roberto Luiz Souza Monteiro
 * @license BSD 3-Clause License
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "rnnoise/include/rnnoise.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gtk/gtk.h>
#include <math.h>

#define SAMPLE_RATE 48000       // Sampling rate (48kHz).
#define RNNOISE_FRAME_SIZE 480  // RNNoise frame size (480 samples for 48kHz).

/**
 * Biquad filter structure.
 */
typedef struct {
    float b0, b1, b2, a1, a2;  // Filter coefficients.
    float x1, x2, y1, y2;      // Filter state variables.
} BiquadFilter;

/**
 * Application state structure containing all UI elements and audio processing state.
 */
typedef struct {
    // UI elements.
    GtkWidget *window;
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *filter_toggle;
    GtkWidget *vu_meter;
    GtkWidget *status_label;

    GtkWidget *input_combo;
    GtkWidget *output_combo;

    // Audio context and devices.
    ma_context context;
    ma_device device;
    ma_device_info *input_devices;
    ma_device_info *output_devices;
    ma_uint32 input_device_count;
    ma_uint32 output_device_count;

    // Audio processing.
    BiquadFilter bandpass_filter1;
    BiquadFilter bandpass_filter2;

    DenoiseState *rnnoise_state;
    
    // State flags.
    gboolean is_processing;
    gboolean filter_enabled;
    gboolean device_initialized;
} AppState;

/**
 * Structure for VU meter update data.
 */
typedef struct {
    GtkWidget* vu;   // VU meter widget.
    float vol;       // Current volume level.
} VuUpdateData;

/**
 * Updates the VU meter widget.
 * @param user_data Pointer to VuUpdateData containing VU widget and volume level.
 * @return FALSE to remove the idle callback.
 */
static gboolean update_vu_meter(gpointer user_data) {
    VuUpdateData* data = (VuUpdateData*)user_data;
    if (data->vu) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(data->vu), data->vol);
    }
    g_free(data);
    return FALSE;
}

/**
 * Calculates RMS volume from audio samples.
 * @param samples Pointer to audio samples.
 * @param frameCount Number of frames to process.
 * @return Normalized RMS volume (0.0-1.0).
 */
static float calculate_rms_volume(const int16_t* samples, size_t frameCount) {
    double sum = 0.0;
    for (size_t i = 0; i < frameCount; i++) {
        sum += samples[i] * samples[i];
    }
    double mean = sum / frameCount;
    return (float)(sqrt(mean) / 32768.0);  // Normalize to 0.0–1.0.
}

/**
 * Initializes a bandpass biquad filter.
 * @param f Pointer to BiquadFilter structure.
 * @param fs Sampling frequency.
 * @param f0 Center frequency.
 * @param Q Quality factor.
 */
static void biquad_init_bandpass(BiquadFilter* f, float fs, float f0, float Q) {
    float w0 = 2.0f * M_PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * Q);
    float cos_w0 = cosf(w0);

    f->b0 = alpha;
    f->b1 = 0.0f;
    f->b2 = -alpha;
    f->a1 = -2.0f * cos_w0;
    f->a2 = 1.0f - alpha;

    // Normalize coefficients.
    float a0 = 1.0f + alpha;
    f->b0 /= a0;
    f->b1 /= a0;
    f->b2 /= a0;
    f->a1 /= a0;
    f->a2 /= a0;

    // Initialize state variables.
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
}

/**
 * Processes a single sample through a biquad filter.
 * @param f Pointer to BiquadFilter structure.
 * @param in Input sample.
 * @return Filtered output sample.
 */
static float biquad_process(BiquadFilter* f, float in) {
    float out = f->b0 * in + f->b1 * f->x1 + f->b2 * f->x2
                - f->a1 * f->y1 - f->a2 * f->y2;

    // Update state variables.
    f->x2 = f->x1;
    f->x1 = in;
    f->y2 = f->y1;
    f->y1 = out;

    return out;
}

/**
 * Audio duplex callback for simultaneous capture and playback.
 * @param pDevice Pointer to miniaudio device.
 * @param pOutput Pointer to output buffer.
 * @param pInput Pointer to input buffer.
 * @param frameCount Number of frames to process.
 */
static void duplex_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AppState *state = (AppState*)pDevice->pUserData;

    if (!state || !state->is_processing || !pInput || !pOutput) {
        memset(pOutput, 0, frameCount * 2 * sizeof(int16_t));
        return;
    }

    const int16_t *in = (const int16_t*)pInput;
    int16_t *out = (int16_t*)pOutput;

    for (ma_uint32 i = 0; i < frameCount; i += RNNOISE_FRAME_SIZE) {
        ma_uint32 remaining = frameCount - i;
        ma_uint32 to_process = (remaining > RNNOISE_FRAME_SIZE) ? RNNOISE_FRAME_SIZE : remaining;

        float volume = 0.0f;

        float buffer[RNNOISE_FRAME_SIZE] = {0};

        // Convert PCM to float.
        for (ma_uint32 j = 0; j < to_process; ++j) {
            int32_t l = in[(i + j) * 2];
            int32_t r = in[(i + j) * 2 + 1];
            int16_t mono = (int16_t)((l + r) / 2);
            buffer[j] = (float)mono;

            volume += l * r;
        }

        // Zero padding for last frame.
        for (size_t j = to_process; j < RNNOISE_FRAME_SIZE; j++) {
            buffer[j] = 0.0f;
        }

        volume = sqrtf(volume / to_process) / 32768.0f;

        if (state->filter_enabled) {
            // Apply RNNoise.
            rnnoise_process_frame(state->rnnoise_state, buffer, buffer);

            // Convert float back to PCM.
            for (ma_uint32 j = 0; j < to_process; ++j) {
                int16_t sample = (int16_t)buffer[j];
                out[(i + j) * 2] = sample;
                out[(i + j) * 2 + 1] = sample;
            }
        } else {
            for (ma_uint32 j = 0; j < to_process; ++j) {
                int32_t l = in[(i + j) * 2];
                int32_t r = in[(i + j) * 2 + 1];
                int16_t sample = (int16_t)((l + r) / 2);
                out[(i + j) * 2] = sample;
                out[(i + j) * 2 + 1] = sample;
            }
        }

        VuUpdateData* vu_data = g_malloc(sizeof(VuUpdateData));
        if (vu_data) {
            vu_data->vu = state->vu_meter;
            vu_data->vol = volume;
            g_idle_add_full(G_PRIORITY_DEFAULT, update_vu_meter, vu_data, NULL);
        }
    }
}

/**
 * Starts audio processing.
 * @param state Pointer to application state.
 */
static void start_processing(AppState *state) {
    GtkComboBoxText *input_cb = GTK_COMBO_BOX_TEXT(state->input_combo);
    GtkComboBoxText *output_cb = GTK_COMBO_BOX_TEXT(state->output_combo);

    int input_index = gtk_combo_box_get_active(GTK_COMBO_BOX(input_cb));
    int output_index = gtk_combo_box_get_active(GTK_COMBO_BOX(output_cb));

    if (input_index < 0 || output_index < 0) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Select input/output devices");
        return;
    }

    state->rnnoise_state = rnnoise_create(NULL);
    if (!state->rnnoise_state) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to init RNNoise");
        return;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.sampleRate = SAMPLE_RATE;
    config.capture.format = ma_format_s16;
    config.capture.channels = 2;
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.dataCallback = duplex_callback;
    config.pUserData = state;

    config.capture.pDeviceID = &state->input_devices[input_index].id;
    config.playback.pDeviceID = &state->output_devices[output_index].id;

    if (ma_device_init(&state->context, &config, &state->device) != MA_SUCCESS) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to init device");
        rnnoise_destroy(state->rnnoise_state);
        return;
    }

    state->device_initialized = TRUE;

    if (ma_device_start(&state->device) != MA_SUCCESS) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to start device");
        ma_device_uninit(&state->device);
        rnnoise_destroy(state->rnnoise_state);
        return;
    }

    state->is_processing = TRUE;
    gtk_label_set_text(GTK_LABEL(state->status_label), "Processing...");
    gtk_widget_set_sensitive(state->start_button, FALSE);
    gtk_widget_set_sensitive(state->stop_button, TRUE);
}

/**
 * Stops audio processing.
 * @param state Pointer to application state.
 */
static void stop_processing(AppState *state) {
    if (state->is_processing) {
        if (state->device_initialized) {
            ma_device_uninit(&state->device);
            state->device_initialized = FALSE;
        }
        
        if (state->rnnoise_state) {
            rnnoise_destroy(state->rnnoise_state);
            state->rnnoise_state = NULL;
        }

        state->is_processing = FALSE;
        gtk_label_set_text(GTK_LABEL(state->status_label), "Stopped");
        gtk_widget_set_sensitive(state->start_button, TRUE);
        gtk_widget_set_sensitive(state->stop_button, FALSE);
    }
}

/**
 * Callback for start button click.
 * @param widget Button widget.
 * @param data Pointer to application state.
 */
static void on_start(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    start_processing(state);
}

/**
 * Callback for stop button click.
 * @param widget Button widget.
 * @param data Pointer to application state.
 */
static void on_stop(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    stop_processing(state);
}

/**
 * Callback for filter toggle button.
 * @param widget Toggle button widget.
 * @param data Pointer to application state.
 */
static void on_filter_toggle(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    state->filter_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    
    // Reinitialize filters when toggled to avoid artifacts.
    if (state->filter_enabled) {
        biquad_init_bandpass(&state->bandpass_filter1, (float)SAMPLE_RATE, 500.0f, 2.0f);
        biquad_init_bandpass(&state->bandpass_filter2, (float)SAMPLE_RATE, 2000.0f, 2.0f);
    }
}

/**
 * Callback for window destroy event.
 * @param widget Window widget.
 * @param data Pointer to application state.
 */
static void on_window_destroy(GtkWidget *widget, gpointer data) {
    AppState *state = (AppState*)data;
    stop_processing(state);
    ma_context_uninit(&state->context);
    gtk_main_quit();
}

/**
 * Populates device lists in the UI.
 * @param state Pointer to application state.
 */
static void populate_device_lists(AppState *state) {
    if (ma_context_init(NULL, 0, NULL, &state->context) != MA_SUCCESS) {
        fprintf(stderr, "Failed to initialize context\n");
        return;
    }

    ma_device_info *capture_devices = NULL;
    ma_device_info *playback_devices = NULL;
    ma_uint32 captureCount, playbackCount;

    if (ma_context_get_devices(&state->context, &playback_devices, &playbackCount, &capture_devices, &captureCount) != MA_SUCCESS) {
        fprintf(stderr, "Failed to enumerate devices\n");
        return;
    }

    state->input_devices = capture_devices;
    state->output_devices = playback_devices;
    state->input_device_count = captureCount;
    state->output_device_count = playbackCount;

    // Clear old devices.
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state->input_combo));
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state->output_combo));

    // Add input devices.
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->input_combo), capture_devices[i].name);
    }
    if (captureCount > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state->input_combo), 0);
    }

    // Add output devices.
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->output_combo), playback_devices[i].name);
    }
    if (playbackCount > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state->output_combo), 0);
    }
}

/**
 * Main function.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Application exit code.
 */
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppState state = {0};

    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "Noise Reduction");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 400, 250);
    gtk_container_set_border_width(GTK_CONTAINER(state.window), 10);
    g_signal_connect(state.window, "destroy", G_CALLBACK(on_window_destroy), &state);

    // Main vertical container.
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(state.window), vbox);

    // Add device combos.
    state.input_combo = gtk_combo_box_text_new();
    state.output_combo = gtk_combo_box_text_new();

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Input Device:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), state.input_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Output Device:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), state.output_combo, FALSE, FALSE, 0);

    // Button container.
    GtkWidget *button_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_container, FALSE, FALSE, 5);

    // Left spacer for centering.
    gtk_box_pack_start(GTK_BOX(button_container), gtk_label_new(""), TRUE, TRUE, 0);

    // Button box.
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(button_container), button_box, FALSE, FALSE, 0);

    // Add buttons.
    state.start_button = gtk_button_new_with_label("Start");
    g_signal_connect(state.start_button, "clicked", G_CALLBACK(on_start), &state);
    gtk_box_pack_start(GTK_BOX(button_box), state.start_button, FALSE, FALSE, 0);

    state.stop_button = gtk_button_new_with_label("Stop");
    g_signal_connect(state.stop_button, "clicked", G_CALLBACK(on_stop), &state);
    gtk_widget_set_sensitive(state.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(button_box), state.stop_button, FALSE, FALSE, 0);

    state.filter_toggle = gtk_toggle_button_new_with_label("Filter");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.filter_toggle), TRUE);
    state.filter_enabled = TRUE;
    g_signal_connect(state.filter_toggle, "toggled", G_CALLBACK(on_filter_toggle), &state);
    gtk_box_pack_start(GTK_BOX(button_box), state.filter_toggle, FALSE, FALSE, 0);

    // Right spacer for centering.
    gtk_box_pack_start(GTK_BOX(button_container), gtk_label_new(""), TRUE, TRUE, 0);

    // VU meter and status label.
    state.vu_meter = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state.vu_meter), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), state.vu_meter, FALSE, FALSE, 5);

    state.status_label = gtk_label_new("Select devices and click Start");
    gtk_box_pack_start(GTK_BOX(vbox), state.status_label, FALSE, FALSE, 0);

    // Initialize devices and show window.
    populate_device_lists(&state);
    gtk_widget_show_all(state.window);

    gtk_main();

    return 0;
}