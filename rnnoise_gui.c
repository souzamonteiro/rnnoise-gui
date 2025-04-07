/**
 * @file
 * @brief Noise reduction tool using RNNoise and GTK.
 *
 * This application processes WAV files to reduce background noise
 * using the RNNoise library, providing a simple GTK interface.
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
 * @brief Struct holding all GTK widgets for the application.
 */
typedef struct {
    GtkWidget *window;
    GtkWidget *input_entry;
    GtkWidget *output_entry;
    GtkWidget *status_label;
    GtkWidget *progress_bar;
} AppWidgets;

/**
 * @brief Struct representing the WAV file header.
 */
typedef struct {
    char riff[4];                  // "RIFF"
    uint32_t file_size;            // Total file size minus 8 bytes
    char wave[4];                  // "WAVE"
    char fmt[4];                   // "fmt "
    uint32_t fmt_size;             // Format chunk size (usually 16)
    uint16_t format;               // Audio format (1 = PCM)
    uint16_t channels;             // Number of audio channels
    uint32_t sample_rate;          // Sampling rate (Hz)
    uint32_t byte_rate;            // Bytes per second
    uint16_t block_align;          // Block alignment (bytes per sample frame)
    uint16_t bits_per_sample;      // Bits per sample (usually 16)
    char data[4];                  // "data"
    uint32_t data_size;            // Size of audio data in bytes
} WavHeader;

/**
 * @brief Show an error message dialog.
 * @param parent Parent GTK window.
 * @param message Message to display.
 */
static void show_error_dialog(GtkWidget *parent, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_ERROR,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/**
 * @brief Show an informational message dialog.
 * @param parent Parent GTK window.
 * @param message Message to display.
 */
static void show_info_dialog(GtkWidget *parent, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/**
 * @brief Read a WAV header from a file.
 * @param file Pointer to the opened WAV file.
 * @param header Pointer to a WavHeader struct to fill.
 * @return 1 on success, 0 on failure.
 */
static int read_wav_header(FILE *file, WavHeader *header) {
    if (fread(header, sizeof(WavHeader), 1, file) != 1) {
        return 0;
    }
    return 1;
}

/**
 * @brief Write a WAV header to a file.
 * @param file Pointer to the opened WAV file.
 * @param header Pointer to a WavHeader struct to write.
 * @return 1 on success, 0 on failure.
 */
static int write_wav_header(FILE *file, WavHeader *header) {
    if (fwrite(header, sizeof(WavHeader), 1, file) != 1) {
        return 0;
    }
    return 1;
}

/**
 * @brief Callback for the "Browse Input" button.
 * Opens a file dialog to choose an input WAV file.
 * @param widget The GTK widget triggering the callback.
 * @param data Pointer to the AppWidgets struct.
 */
static void on_browse_input(GtkWidget *widget, gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select input WAV file",
                                                   GTK_WINDOW(widgets->window),
                                                   GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL,
                                                   "_Open", GTK_RESPONSE_ACCEPT,
                                                   NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WAV Files");
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(widgets->input_entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

/**
 * @brief Callback for the "Browse Output" button.
 * Opens a file dialog to choose an output WAV file.
 * @param widget The GTK widget triggering the callback.
 * @param data Pointer to the AppWidgets struct.
 */
static void on_browse_output(GtkWidget *widget, gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select output WAV file",
                                                  GTK_WINDOW(widgets->window),
                                                  GTK_FILE_CHOOSER_ACTION_SAVE,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WAV Files");
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(widgets->output_entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

/**
 * @brief Perform noise reduction on a WAV file using RNNoise.
 * @param data Pointer to the AppWidgets struct.
 * @return FALSE to remove the source from the main loop after execution.
 */
static gboolean process_audio(gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    const char *input_file = gtk_entry_get_text(GTK_ENTRY(widgets->input_entry));
    const char *output_file = gtk_entry_get_text(GTK_ENTRY(widgets->output_entry));

    // Validate file paths.
    if (!input_file || !output_file || strlen(input_file) == 0 || strlen(output_file) == 0) {
        show_error_dialog(widgets->window, "Please specify both input and output files.");
        return G_SOURCE_REMOVE;
    }

    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        show_error_dialog(widgets->window, "Could not open the input file.");
        return G_SOURCE_REMOVE;
    }

    WavHeader header;
    if (!read_wav_header(fin, &header)) {
        fclose(fin);
        show_error_dialog(widgets->window, "Failed to read the input WAV file header.");
        return G_SOURCE_REMOVE;
    }

    // Validate WAV header contents.
    if (memcmp(header.riff, "RIFF", 4) != 0 ||
        memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 ||
        memcmp(header.data, "data", 4) != 0) {
        fclose(fin);
        show_error_dialog(widgets->window, "Invalid input WAV file.");
        return G_SOURCE_REMOVE;
    }

    // Only 48kHz, 16-bit, mono files are supported.
    if (header.channels != 1 || header.sample_rate != SAMPLE_RATE || header.bits_per_sample != 16) {
        fclose(fin);
        show_error_dialog(widgets->window, "Only mono 16-bit 48kHz WAV files are supported.");
        return G_SOURCE_REMOVE;
    }

    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fclose(fin);
        show_error_dialog(widgets->window, "Could not create the output file.");
        return G_SOURCE_REMOVE;
    }

    if (!write_wav_header(fout, &header)) {
        fclose(fin);
        fclose(fout);
        show_error_dialog(widgets->window, "Failed to write the output WAV file header.");
        return G_SOURCE_REMOVE;
    }

    // Create RNNoise state.
    DenoiseState *st = rnnoise_create(NULL);
    if (!st) {
        fclose(fin);
        fclose(fout);
        show_error_dialog(widgets->window, "Failed to initialize RNNoise.");
        return G_SOURCE_REMOVE;
    }

    gtk_label_set_text(GTK_LABEL(widgets->status_label), "Processing...");
    gtk_widget_set_sensitive(widgets->window, FALSE);

    int16_t tmp[FRAME_SIZE];  // Temporary buffer for raw PCM data
    float x[FRAME_SIZE];      // Float buffer for RNNoise processing
    size_t total_samples = header.data_size / sizeof(int16_t);
    size_t processed_samples = 0;
    gboolean first = TRUE;

    // Process each frame of audio.
    while (!feof(fin)) {
        size_t read = fread(tmp, sizeof(int16_t), FRAME_SIZE, fin);
        if (read == 0) break;

        // Convert PCM to float.
        for (size_t i = 0; i < read; i++) {
            x[i] = (float)tmp[i];
        }

        // Zero padding for last frame.
        for (size_t i = read; i < FRAME_SIZE; i++) {
            x[i] = 0.0f;
        }

        // Apply RNNoise.
        rnnoise_process_frame(st, x, x);

        // Convert float back to PCM.
        for (size_t i = 0; i < read; i++) {
            float sample = x[i];
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            tmp[i] = (int16_t)sample;
        }

        // Skip first frame (RNNoise warm-up).
        if (!first) {
            fwrite(tmp, sizeof(int16_t), read, fout);
        }
        first = FALSE;

        // Update progress.
        processed_samples += read;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(widgets->progress_bar),
                                      (double)processed_samples / total_samples);

        while (gtk_events_pending()) gtk_main_iteration();  // Allow GTK UI to update.
    }

    // Cleanup.
    rnnoise_destroy(st);
    fclose(fin);
    fclose(fout);

    gtk_label_set_text(GTK_LABEL(widgets->status_label), "Done!");
    gtk_widget_set_sensitive(widgets->window, TRUE);
    show_info_dialog(widgets->window, "Processing completed successfully!");

    return G_SOURCE_REMOVE;
}

/**
 * @brief Callback for the "Process" button.
 * Starts the audio processing in an idle function.
 * @param widget The GTK widget triggering the callback.
 * @param data Pointer to the AppWidgets struct.
 */
static void on_process(GtkWidget *widget, gpointer data) {
    g_idle_add(process_audio, data);
}

/**
 * @brief Main entry point. Initializes GTK, creates UI, and starts main loop.
 * @param argc Argument count.
 * @param argv Argument values.
 * @return Exit code.
 */
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Declare widget container structure.
    AppWidgets widgets;

    // Create main window.
    widgets.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(widgets.window), "RNNoise Noise Remover");
    gtk_window_set_default_size(GTK_WINDOW(widgets.window), 400, 200);
    gtk_container_set_border_width(GTK_CONTAINER(widgets.window), 10);
    g_signal_connect(widgets.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(widgets.window), grid);

    // Input file selector.
    GtkWidget *input_label = gtk_label_new("Input WAV file:");
    gtk_grid_attach(GTK_GRID(grid), input_label, 0, 0, 1, 1);

    widgets.input_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), widgets.input_entry, 1, 0, 1, 1);

    GtkWidget *input_button = gtk_button_new_with_label("Browse...");
    g_signal_connect(input_button, "clicked", G_CALLBACK(on_browse_input), &widgets);
    gtk_grid_attach(GTK_GRID(grid), input_button, 2, 0, 1, 1);

    // Output file selector.
    GtkWidget *output_label = gtk_label_new("Output WAV file:");
    gtk_grid_attach(GTK_GRID(grid), output_label, 0, 1, 1, 1);

    widgets.output_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), widgets.output_entry, 1, 1, 1, 1);

    GtkWidget *output_button = gtk_button_new_with_label("Waiting...");
    g_signal_connect(output_button, "clicked", G_CALLBACK(on_browse_output), &widgets);
    gtk_grid_attach(GTK_GRID(grid), output_button, 2, 1, 1, 1);

    // Progress bar.
    widgets.progress_bar = gtk_progress_bar_new();
    gtk_grid_attach(GTK_GRID(grid), widgets.progress_bar, 0, 2, 3, 1);

    // Status label.
    widgets.status_label = gtk_label_new("Waiting...");
    gtk_grid_attach(GTK_GRID(grid), widgets.status_label, 0, 3, 3, 1);

    // Process button.
    GtkWidget *process_button = gtk_button_new_with_label("Process");
    g_signal_connect(process_button, "clicked", G_CALLBACK(on_process), &widgets);
    gtk_grid_attach(GTK_GRID(grid), process_button, 0, 4, 3, 1);

    gtk_widget_show_all(widgets.window);
    gtk_main();

    return 0;
}
