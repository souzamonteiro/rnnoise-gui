#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gtk/gtk.h>

// Structure representing the WAV file header.
typedef struct {
    char riff[4];             // "RIFF"
    uint32_t file_size;       // File size minus 8 bytes
    char wave[4];             // "WAVE"
    char fmt[4];              // "fmt "
    uint32_t fmt_size;        // Size of format chunk
    uint16_t format;          // Format code (1 = PCM)
    uint16_t channels;        // Number of channels
    uint32_t sample_rate;     // Sample rate (Hz)
    uint32_t byte_rate;       // Byte rate = SampleRate * Channels * BitsPerSample / 8
    uint16_t block_align;     // Block align = Channels * BitsPerSample / 8
    uint16_t bits_per_sample; // Bits per sample (e.g., 16)
    char data[4];             // "data"
    uint32_t data_size;       // Number of bytes in data section
} WavHeader;

// Show an error dialog with a given message.
static void show_error_dialog(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_ERROR,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Show an informational dialog with a given message.
static void show_info_dialog(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Read the WAV header from the file.
static int read_wav_header(FILE *file, WavHeader *header) {
    if (fread(header, sizeof(WavHeader), 1, file) != 1) {
        return 0;
    }
    return 1;
}

// Convert a WAV file to raw PCM.
static void convert_wav_to_pcm(const char *input_path, const char *output_path) {
    FILE *fin = fopen(input_path, "rb");
    if (!fin) {
        show_error_dialog("Unable to open input WAV file.");
        return;
    }

    WavHeader header;
    if (!read_wav_header(fin, &header)) {
        fclose(fin);
        show_error_dialog("Failed to read WAV file header.");
        return;
    }

    // Validate WAV format.
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 || memcmp(header.data, "data", 4) != 0) {
        fclose(fin);
        show_error_dialog("Invalid WAV file format.");
        return;
    }

    if (header.channels != 1) {
        fclose(fin);
        show_error_dialog("Only mono (1 channel) files are supported.");
        return;
    }

    if (header.sample_rate != 48000) {
        fclose(fin);
        show_error_dialog("Only 48kHz sample rate is supported.");
        return;
    }

    if (header.bits_per_sample != 16) {
        fclose(fin);
        show_error_dialog("Only 16-bit samples are supported.");
        return;
    }

    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        fclose(fin);
        show_error_dialog("Unable to create output PCM file.");
        return;
    }

    // Allocate buffer and read audio data.
    uint8_t *buffer = malloc(header.data_size);
    if (!buffer) {
        fclose(fin);
        fclose(fout);
        show_error_dialog("Memory allocation failed.");
        return;
    }

    size_t bytes_read = fread(buffer, 1, header.data_size, fin);
    if (bytes_read != header.data_size) {
        show_error_dialog("Failed to read audio data from WAV file.");
    } else {
        fwrite(buffer, 1, bytes_read, fout);
        char message[256];
        snprintf(message, sizeof(message), "Conversion complete!\n%d samples converted.",
                 bytes_read / 2);  // 2 bytes per 16-bit sample.
        show_info_dialog(message);
    }

    free(buffer);
    fclose(fin);
    fclose(fout);
}

// Handler for the "Convert" button click.
static void on_convert(GtkWidget *widget, gpointer data) {
    GtkWidget **entries = (GtkWidget **)data;
    const char *input_file = gtk_entry_get_text(GTK_ENTRY(entries[0]));
    const char *output_file = gtk_entry_get_text(GTK_ENTRY(entries[1]));

    if (strlen(input_file) == 0 || strlen(output_file) == 0) {
        show_error_dialog("Please specify both input and output file paths.");
        return;
    }

    convert_wav_to_pcm(input_file, output_file);
}

// Open file chooser to select the input WAV file.
static void on_browse_input(GtkWidget *widget, gpointer data) {
    GtkWidget *entry = (GtkWidget *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select WAV File",
                                                    NULL,
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
        gtk_entry_set_text(GTK_ENTRY(entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

// Open file chooser to select output PCM file path.
static void on_browse_output(GtkWidget *widget, gpointer data) {
    GtkWidget *entry = (GtkWidget *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Output PCM File",
                                                    NULL,
                                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

// Entry point of the application.
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Create main window.
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "WAV to PCM Converter");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create layout grid.
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(window), grid);

    // Input file selector.
    GtkWidget *input_label = gtk_label_new("Input WAV File:");
    gtk_grid_attach(GTK_GRID(grid), input_label, 0, 0, 1, 1);

    GtkWidget *input_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), input_entry, 1, 0, 1, 1);

    GtkWidget *input_button = gtk_button_new_with_label("Browse...");
    g_signal_connect(input_button, "clicked", G_CALLBACK(on_browse_input), input_entry);
    gtk_grid_attach(GTK_GRID(grid), input_button, 2, 0, 1, 1);

    // Output file selector.
    GtkWidget *output_label = gtk_label_new("Output PCM File:");
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

    // Show everything and run GTK main loop.
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
