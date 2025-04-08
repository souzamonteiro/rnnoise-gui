#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <gtk/gtk.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SAMPLES (48000 * 300)
#define SAMPLE_RATE 48000
#define CHANNELS 1

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

GtkWidget *button_record, *button_pause, *button_stop, *button_save;

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

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
    if (!is_recording || is_paused || pInput == NULL) return;

    size_t samples_to_copy = frameCount * CHANNELS;
    if (audio_buffer_pos + samples_to_copy > MAX_SAMPLES)
        samples_to_copy = MAX_SAMPLES - audio_buffer_pos;

    memcpy(audio_buffer + audio_buffer_pos, pInput, samples_to_copy * sizeof(int16_t));
    audio_buffer_pos += samples_to_copy;
}

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

void on_pause(GtkButton *btn, gpointer user_data) {
    is_paused = !is_paused;
    gtk_button_set_label(GTK_BUTTON(button_pause), is_paused ? "Resume" : "Pause");
}

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

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    audio_buffer = (int16_t *)malloc(sizeof(int16_t) * MAX_SAMPLES);

    ma_result result;
    ma_context_config ctxConfig = ma_context_config_init();
    result = ma_context_init(NULL, 0, &ctxConfig, &context);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "Failed to init context\n");
        return -1;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16;
    deviceConfig.capture.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;

    result = ma_device_init(&context, &deviceConfig, &device);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "Failed to init capture device\n");
        return -2;
    }

    // GTK UI.
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Gravador de Áudio");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 100);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(window), box);

    button_record = gtk_button_new_with_label("Record");
    button_pause = gtk_button_new_with_label("Pause");
    button_stop = gtk_button_new_with_label("Stop");
    button_save = gtk_button_new_with_label("Save");

    gtk_box_pack_start(GTK_BOX(box), button_record, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(box), button_pause, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(box), button_stop, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(box), button_save, TRUE, TRUE, 2);

    gtk_widget_set_sensitive(button_pause, FALSE);
    gtk_widget_set_sensitive(button_stop, FALSE);

    g_signal_connect(button_record, "clicked", G_CALLBACK(on_record), NULL);
    g_signal_connect(button_pause, "clicked", G_CALLBACK(on_pause), NULL);
    g_signal_connect(button_stop, "clicked", G_CALLBACK(on_stop), NULL);
    g_signal_connect(button_save, "clicked", G_CALLBACK(on_save), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    // Cleanup.
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    free(audio_buffer);
    return 0;
}

