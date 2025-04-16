/**
* @file
* @brief Simplified real-time noise reduction tool using RNNoise and miniaudio.
*
* This minimal version captures from the default input and sends to the default output.
* It removes device selection and VU meter for easier debugging.
*/

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gtk/gtk.h>
#include <math.h>

#include "rnnoise/include/rnnoise.h"

#define FRAME_SIZE 480
#define SAMPLE_RATE 48000

typedef struct {
    GtkWidget *window;
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *status_label;

    ma_context context;
    ma_device device;
    DenoiseState *rnnoise_state;

    gboolean is_processing;
    gboolean device_initialized;
} AppState;

#define RNNOISE_GAIN 0.95f  // Ajuste esse valor para suavizar mais ou menos

static void duplex_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AppState *state = (AppState*)pDevice->pUserData;

    if (!state || !state->is_processing || !pInput || !pOutput || !state->rnnoise_state) {
        memset(pOutput, 0, frameCount * sizeof(int16_t));
        return;
    }

    int16_t *in  = (int16_t*)pInput;
    int16_t *out = (int16_t*)pOutput;
    float float_buffer[FRAME_SIZE];

    for (ma_uint32 i = 0; i < frameCount; i += FRAME_SIZE) {
        ma_uint32 block = (i + FRAME_SIZE <= frameCount) ? FRAME_SIZE : (frameCount - i);

        for (ma_uint32 j = 0; j < block; j++) {
            float_buffer[j] = (float)in[i + j] / 32768.0f;
        }

        rnnoise_process_frame(state->rnnoise_state, float_buffer, float_buffer);

        for (ma_uint32 j = 0; j < block; j++) {
            float sample = float_buffer[j] * RNNOISE_GAIN;
            sample = fmaxf(-1.0f, fminf(1.0f, sample));
            out[i + j] = (int16_t)(sample * 32767.0f);
        }
    }
}

static void start_processing(AppState *state) {
    state->rnnoise_state = rnnoise_create(NULL);
    if (!state->rnnoise_state) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to initialize RNNoise");
        return;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.sampleRate       = SAMPLE_RATE;
    config.capture.format   = ma_format_s16;
    config.capture.channels = 1;
    config.playback.format  = ma_format_s16;
    config.playback.channels = 1;
    config.dataCallback     = duplex_callback;
    config.pUserData        = state;

    if (ma_device_init(NULL, &config, &state->device) != MA_SUCCESS) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Failed to init device");
        rnnoise_destroy(state->rnnoise_state);
        state->rnnoise_state = NULL;
        return;
    }

    state->device_initialized = TRUE;

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
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppState state = {0};

    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "Noise Reduction");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 300, 100);
    gtk_container_set_border_width(GTK_CONTAINER(state.window), 10);
    g_signal_connect(state.window, "destroy", G_CALLBACK(on_window_destroy), &state);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(state.window), vbox);

    state.start_button = gtk_button_new_with_label("Start");
    g_signal_connect(state.start_button, "clicked", G_CALLBACK(on_start), &state);
    gtk_box_pack_start(GTK_BOX(vbox), state.start_button, FALSE, FALSE, 0);

    state.stop_button = gtk_button_new_with_label("Stop");
    g_signal_connect(state.stop_button, "clicked", G_CALLBACK(on_stop), &state);
    gtk_widget_set_sensitive(state.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), state.stop_button, FALSE, FALSE, 0);

    state.status_label = gtk_label_new("Click Start to begin");
    gtk_box_pack_start(GTK_BOX(vbox), state.status_label, FALSE, FALSE, 0);

    gtk_widget_show_all(state.window);
    gtk_main();

    return 0;
}
