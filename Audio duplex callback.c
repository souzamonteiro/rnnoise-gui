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
    float float_buffer[FRAME_SIZE];
    float output_buffer[FRAME_SIZE];
    static bool first_frame = true;  // To skip the first frame.

    // Process in blocks of FRAME_SIZE.
    for (ma_uint32 i = 0; i < frameCount; i += FRAME_SIZE) {
        ma_uint32 remaining = frameCount - i;
        ma_uint32 to_process = (remaining > FRAME_SIZE) ? FRAME_SIZE : remaining;

        // Mix stereo to mono and convert to float.
        for (ma_uint32 j = 0; j < to_process; j++) {
            int32_t l = in[(i + j) * 2];
            int32_t r = in[(i + j) * 2 + 1];
            float_buffer[j] = (float)(l + r / 2);  // Convert the average to float.
        }

        // Fill the remaining frame with zeros if necessary..
        for (ma_uint32 j = to_process; j < FRAME_SIZE; j++) {
            float_buffer[j] = 0.0f;
        }

        // Calculate volume for VU meter before any early return.
        float volume = 0.0f;
        for (ma_uint32 j = 0; j < to_process; j++) {
            volume += float_buffer[j] * float_buffer[j];
        }
        volume = sqrtf(volume / to_process) / 32768.0f;

        if (state->filter_enabled) {
            // RNNoise processing.
            for (ma_uint32 k = 0; k < FRAME_SIZE; k++) {
                output_buffer[k] = 0.0f;
            }
            rnnoise_process_frame(state->rnnoise_state, output_buffer, float_buffer);

            // Skip the first frame (RNNoise warm-up).
            if (first_frame) {
                first_frame = false;
                continue;
            }

            // Convert back to int16_t and expand to stereo.
            for (ma_uint32 j = 0; j < to_process; j++) {
                float sample = output_buffer[j];
                // Careful clipping.
                if (sample > 32767.0f) sample = 32767.0f;
                if (sample < -32768.0f) sample = -32768.0f;

                int16_t sample_int = (int16_t)sample;
                out[(i + j) * 2] = sample_int;
                out[(i + j) * 2 + 1] = sample_int;
            }
        } else {
            // Bypass if the filter is disabled: copy input to output.
            for (ma_uint32 j = 0; j < to_process; j++) {
                int32_t l = in[(i + j) * 2];
                int32_t r = in[(i + j) * 2 + 1];
                int16_t sample = (int16_t)((l + r) / 2);
                out[(i + j) * 2] = sample;
                out[(i + j) * 2 + 1] = sample;
            }
        }

        // Update the VU meter with the last processed block.
        VuUpdateData* vu_data = g_malloc(sizeof(VuUpdateData));
        if (vu_data) {
            vu_data->vu = state->vu_meter;
            vu_data->vol = volume;
            g_idle_add_full(G_PRIORITY_DEFAULT, update_vu_meter, vu_data, NULL);
        }
    }
}
