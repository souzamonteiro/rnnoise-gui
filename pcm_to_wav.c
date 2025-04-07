#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gtk/gtk.h>

// Structure representing a standard WAV file header.
typedef struct {
    char riff[4];              // "RIFF"
    uint32_t file_size;        // Total file size - 8 bytes
    char wave[4];              // "WAVE"
    char fmt[4];               // "fmt "
    uint32_t fmt_size;         // Size of the fmt subchunk (16 for PCM)
    uint16_t format;           // Audio format (1 = PCM)
    uint16_t channels;         // Number of channels (1 = mono)
    uint32_t sample_rate;      // Sample rate (e.g., 48000)
    uint32_t byte_rate;        // Byte rate = sample_rate * channels * bits_per_sample / 8
    uint16_t block_align;      // Block alignment = channels * bits_per_sample / 8
    uint16_t bits_per_sample;  // Bits per sample (e.g., 16)
    char data[4];              // "data"
    uint32_t data_size;        // Size of the audio data in bytes
} WavHeader;

// Show an error message in a GTK dialog.
static void show_error_dialog(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_OK,
                                            "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Show an informational message in a GTK dialog.
static void show_info_dialog(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_OK,
                                            "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Create a WAV header based on the provided audio data size.
static void create_wav_header(WavHeader *header, uint32_t data_size) {
    memcpy(header->riff, "RIFF", 4);
    header->file_size = data_size + sizeof(WavHeader) - 8;
    memcpy(header->wave, "WAVE", 4);
    memcpy(header->fmt, "fmt ", 4);
    header->fmt_size = 16;
    header->format = 1;            // PCM format.
    header->channels = 1;          // Mono audio.
    header->sample_rate = 48000;   // 48kHz.
    header->bits_per_sample = 16;  // 16-bit audio.
    header->byte_rate = header->sample_rate * header->channels * header->bits_per_sample / 8;
    header->block_align = header->channels * header->bits_per_sample / 8;
    memcpy(header->data, "data", 4);
    header->data_size = data_size;
}

// Convert a raw PCM file to a valid WAV file.
static void convert_pcm_to_wav(const char *input_path, const char *output_path) {
    FILE *fin = fopen(input_path, "rb");
    if (!fin) {
        show_error_dialog("Unable to open the input PCM file.");
        return;
    }

    // Get the input file size.
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fin);
        show_error_dialog("Input PCM file is empty or invalid.");
        return;
    }

    // Ensure the file size is even (16-bit = 2 bytes/sample).
    if (file_size % 2 != 0) {
        fclose(fin);
        show_error_dialog("Invalid PCM file size (must be multiple of 2 for 16-bit audio).");
        return;
    }

    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        fclose(fin);
        show_error_dialog("Unable to create the output WAV file.");
        return;
    }

    // Create and write the WAV header.
    WavHeader header;
    create_wav_header(&header, file_size);
    if (fwrite(&header, sizeof(WavHeader), 1, fout) != 1) {
        fclose(fin);
        fclose(fout);
        show_error_dialog("Failed to write WAV header.");
        return;
    }

    // Read PCM data and write it to the WAV file.
    uint8_t *buffer = malloc(file_size);
    if (!buffer) {
        fclose(fin);
        fclose(fout);
        show_error_dialog("Memory allocation failed.");
        return;
    }

    size_t bytes_read = fread(buffer, 1, file_size, fin);
    if (bytes_read != file_size) {
        show_error_dialog("Failed to read PCM data from input file.");
    } else {
        fwrite(buffer, 1, bytes_read, fout);
        char message[256];
        snprintf(message, sizeof(message), "Conversion complete!\n%d samples converted.",
                bytes_read / 2);  // 2 bytes per sample for 16-bit.
        show_info_dialog(message);
    }

    free(buffer);
    fclose(fin);
    fclose(fout);
}

// Handle the "Convert" button click.
static void on_convert(GtkWidget *widget, gpointer data) {
    GtkWidget **entries = (GtkWidget **)data;
    const char *input_file = gtk_entry_get_text(GTK_ENTRY(entries[0]));
    const char *output_file = gtk_entry_get_text(GTK_ENTRY(entries[1]));

    if (strlen(input_file) == 0 || strlen(output_file) == 0) {
        show_error_dialog("Please specify both input and output files.");
        return;
    }

    convert_pcm_to_wav(input_file, output_file);
}

// Handle "Browse..." button for selecting input file.
static void on_browse_input(GtkWidget *widget, gpointer data) {
    GtkWidget *entry = (GtkWidget *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select PCM File",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

// Handle "Browse..." button for selecting output file.
static void on_browse_output(GtkWidget *widget, gpointer data) {
    GtkWidget *entry = (GtkWidget *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Output WAV File",
                                                  NULL,
                                                  GTK_FILE_CHOOSER_ACTION_SAVE,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Save", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WAV files");
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

// Main application entry point.
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "PCM to WAV Converter");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(window), grid);

    // Input file.
    GtkWidget *input_label = gtk_label_new("Input PCM file:");
    gtk_grid_attach(GTK_GRID(grid), input_label, 0, 0, 1, 1);

    GtkWidget *input_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), input_entry, 1, 0, 1, 1);

    GtkWidget *input_button = gtk_button_new_with_label("Browse...");
    g_signal_connect(input_button, "clicked", G_CALLBACK(on_browse_input), input_entry);
    gtk_grid_attach(GTK_GRID(grid), input_button, 2, 0, 1, 1);

    // Output file.
    GtkWidget *output_label = gtk_label_new("Output WAV file:");
    gtk_grid_attach(GTK_GRID(grid), output_label, 0, 1, 1, 1);

    GtkWidget *output_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), output_entry, 1, 1, 1, 1);

    GtkWidget *output_button = gtk_button_new_with_label("Browse...");
    g_signal_connect(output_button, "clicked", G_CALLBACK(on_browse_output), output_entry);
    gtk_grid_attach(GTK_GRID(grid), output_button, 2, 1, 1, 1);

    // Convert button.
    GtkWidget *convert_button = gtk_button_new_with_label("Convert");
    GtkWidget *entries[2] = {input_entry, output_entry};
    g_signal_connect(convert_button, "clicked", G_CALLBACK(on_convert), entries);
    gtk_grid_attach(GTK_GRID(grid), convert_button, 0, 2, 3, 1);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
