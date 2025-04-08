/**
 * @file
 * @brief An audio recorder using GTK.
 *
 * This application allows you to record from an audio source to a WAV file.
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

#include <gtk/gtk.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SAMPLES (48000 * 300)  // Max: 5 minutes of mono audio at 48kHz.
#define SAMPLE_RATE 48000
#define CHANNELS 1

/**
 * WAV file header structure.
 */
typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} WavHeader;

static int16_t *audio_buffer = NULL;
static size_t audio_buffer_pos = 0;
static gboolean is_recording = FALSE;
static gboolean is_paused = FALSE;

static ma_context context;
static ma_device device;

GtkWidget *button_record, *button_pause, *button_stop, *button_save, *device_combo, *level_bar;

/**
 * Creates a valid WAV header.
 * @param header Pointer to the WavHeader struct to populate.
 * @param data_size Size of audio data in bytes.
 */
void create_wav_header(WavHeader *header, uint32_t data_size) {
    memcpy(header->riff, "RIFF", 4);
    header->file_size = data_size + sizeof(WavHeader) - 8;
    memcpy(header->wave, "WAVE", 4);
    memcpy(header->fmt, "fmt ", 4);
    header->fmt_size = 16;
    header->format = 1;
    header->channels = CHANNELS;
    header->sample_rate = SAMPLE_RATE;
    header->bits_per_sample = 16;
    header->byte_rate = SAMPLE_RATE * CHANNELS * 2;
    header->block_align = CHANNELS * 2;
    memcpy(header->data, "data", 4);
    header->data_size = data_size;
}

/**
 * Updates the GTK level bar with the given audio level.
 * @param data Pointer to a double value representing the normalized RMS volume.
 * @return FALSE to remove the callback after execution.
 */
gboolean update_level_bar(gpointer data) {
    double *level = (double *)data;
    gtk_level_bar_set_value(GTK_LEVEL_BAR(level_bar), *level);
    free(level);
    return FALSE;
}

/**
 * Audio input callback from miniaudio.
 * Stores incoming audio in a buffer and calculates RMS for volume meter.
 * @param pDevice Pointer to the device instance.
 * @param pOutput Not used (capture only).
 * @param pInput Pointer to incoming audio data.
 * @param frameCount Number of frames in the input buffer.
 */
void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
    if (!is_recording || is_paused || pInput == NULL) return;

    const int16_t *input = (const int16_t *)pInput;
    size_t samples_to_copy = frameCount * CHANNELS;
    if (audio_buffer_pos + samples_to_copy > MAX_SAMPLES)
        samples_to_copy = MAX_SAMPLES - audio_buffer_pos;

    memcpy(audio_buffer + audio_buffer_pos, input, samples_to_copy * sizeof(int16_t));
    audio_buffer_pos += samples_to_copy;

    // Compute RMS volume for VU meter.
    double sum = 0;
    for (size_t i = 0; i < samples_to_copy; i++) {
        sum += input[i] * input[i];
    }
    double rms = sqrt(sum / samples_to_copy) / 32768.0; // Normalize to [0,1]

    // Update GTK LevelBar in main thread.
    double *rms_ptr = malloc(sizeof(double));
    *rms_ptr = rms;
    g_idle_add(update_level_bar, rms_ptr);
}

/**
 * Starts recording audio.
 */
void on_record(GtkButton *btn, gpointer user_data) {
    if (!is_recording) {
        audio_buffer_pos = 0;
        is_paused = FALSE;
        is_recording = TRUE;
        ma_device_start(&device);
        gtk_widget_set_sensitive(button_record, FALSE);
        gtk_widget_set_sensitive(button_pause, TRUE);
        gtk_widget_set_sensitive(button_stop, TRUE);
    }
}

/**
 * Toggles pause/resume state.
 */
void on_pause(GtkButton *btn, gpointer user_data) {
    is_paused = !is_paused;
    gtk_button_set_label(GTK_BUTTON(button_pause), is_paused ? "Resume" : "Pause");
}

/**
 * Stops the recording session.
 */
void on_stop(GtkButton *btn, gpointer user_data) {
    if (is_recording) {
        ma_device_stop(&device);
        is_recording = FALSE;
        is_paused = FALSE;
        gtk_button_set_label(GTK_BUTTON(button_pause), "Pause");
        gtk_widget_set_sensitive(button_record, TRUE);
        gtk_widget_set_sensitive(button_pause, FALSE);
        gtk_widget_set_sensitive(button_stop, FALSE);
    }
}

/**
 * Opens a file dialog and saves recorded audio to a WAV file.
 */
void on_save(GtkButton *btn, gpointer user_data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save WAV File", NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WAV files");
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        FILE *f = fopen(filename, "wb");
        if (!f) {
            GtkWidget *err = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                    "Failed to open file for writing.");
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
            g_free(filename);
            gtk_widget_destroy(dialog);
            return;
        }

        WavHeader header;
        create_wav_header(&header, audio_buffer_pos * sizeof(int16_t));
        fwrite(&header, sizeof(WavHeader), 1, f);
        fwrite(audio_buffer, sizeof(int16_t), audio_buffer_pos, f);
        fclose(f);

        GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                "File saved successfully!");
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

/**
 * Handles device change event and reinitializes the capture device.
 * @param combo The GTK combo box widget.
 */
void on_device_changed(GtkComboBox *combo, gpointer user_data) {
    if (is_recording) return;
    ma_device_uninit(&device);

    const char *device_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (!device_name) return;

    ma_device_id new_id = {0};
    ma_uint32 count;
    ma_device_info *infos;
    ma_context_get_devices(&context, NULL, NULL, &infos, &count);
    for (ma_uint32 i = 0; i < count; i++) {
        if (strcmp(infos[i].name, device_name) == 0) {
            new_id = infos[i].id;
            break;
        }
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16;
    deviceConfig.capture.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.capture.pDeviceID = &new_id;

    if (ma_device_init(&context, &deviceConfig, &device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to init selected capture device\n");
    }
}

/**
 * Application entry point.
 */
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    audio_buffer = (int16_t *)malloc(sizeof(int16_t) * MAX_SAMPLES);

    // Initialize miniaudio context.
    ma_result result;
    ma_context_config ctxConfig = ma_context_config_init();
    result = ma_context_init(NULL, 0, &ctxConfig, &context);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "Failed to init context\n");
        return -1;
    }

    // Default device setup.
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16;
    deviceConfig.capture.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;

    if (ma_device_init(&context, &deviceConfig, &device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to init capture device\n");
        return -2;
    }

    // Create GTK window.
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Audio Recorder");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Main layout.
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(window), box);

    // Device selector.
    device_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(box), device_combo, FALSE, FALSE, 2);

    ma_uint32 capture_count;
    ma_device_info *capture_infos;
    ma_context_get_devices(&context, NULL, NULL, &capture_infos, &capture_count);
    for (ma_uint32 i = 0; i < capture_count; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_combo), capture_infos[i].name);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(device_combo), 0);
    g_signal_connect(device_combo, "changed", G_CALLBACK(on_device_changed), NULL);

    // Button row.
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(box), button_box, FALSE, FALSE, 2);

    button_record = gtk_button_new_with_label("Record");
    button_pause = gtk_button_new_with_label("Pause");
    button_stop = gtk_button_new_with_label("Stop");
    button_save = gtk_button_new_with_label("Save");

    gtk_box_pack_start(GTK_BOX(button_box), button_record, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(button_box), button_pause, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(button_box), button_stop, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(button_box), button_save, TRUE, TRUE, 2);

    gtk_widget_set_sensitive(button_pause, FALSE);
    gtk_widget_set_sensitive(button_stop, FALSE);
    
    g_signal_connect(button_record, "clicked", G_CALLBACK(on_record), NULL);
    g_signal_connect(button_pause, "clicked", G_CALLBACK(on_pause), NULL);
    g_signal_connect(button_stop, "clicked", G_CALLBACK(on_stop), NULL);
    g_signal_connect(button_save, "clicked", G_CALLBACK(on_save), NULL);
    
    // Level meter.
    level_bar = gtk_level_bar_new();
    gtk_level_bar_set_min_value(GTK_LEVEL_BAR(level_bar), 0.0);
    gtk_level_bar_set_max_value(GTK_LEVEL_BAR(level_bar), 1.0);
    gtk_box_pack_start(GTK_BOX(box), level_bar, FALSE, FALSE, 2);
    
    gtk_widget_show_all(window);
    gtk_main();

    // Cleanup.
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    free(audio_buffer);
    return 0;
}

