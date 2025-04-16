all: rnnoise_gui pcm_to_wav wav_to_pcm recorder audio_recorder audio_filter audio_denoiser rnnoise_audio

rnnoise_gui: rnnoise_gui.c
	gcc -o rnnoise_gui rnnoise_gui.c `pkg-config --cflags --libs gtk+-3.0` -lrnnoise

pcm_to_wav: pcm_to_wav.c
	gcc -o pcm_to_wav pcm_to_wav.c `pkg-config --cflags --libs gtk+-3.0`

wav_to_pcm: wav_to_pcm.c
	gcc -o wav_to_pcm wav_to_pcm.c `pkg-config --cflags --libs gtk+-3.0`

recorder: recorder.c
	gcc recorder.c -o recorder `pkg-config --cflags --libs gtk+-3.0` -lm -ldl -lpthread

audio_recorder: audio_recorder.c
	gcc audio_recorder.c -o audio_recorder `pkg-config --cflags --libs gtk+-3.0` -lm -ldl -lpthread

audio_filter: audio_filter.c
	gcc audio_filter.c -o audio_filter `pkg-config --cflags --libs gtk+-3.0` -lm -ldl -lpthread -lrnnoise

audio_denoiser: audio_denoiser.c
	gcc audio_denoiser.c -o audio_denoiser `pkg-config --cflags --libs gtk+-3.0` -lm -ldl -lpthread -lrnnoise

rnnoise_audio: rnnoise_audio.c
	gcc rnnoise_audio.c -o rnnoise_audio `pkg-config --cflags --libs gtk+-3.0` -lm -ldl -lpthread -lrnnoise

clean:
	rm -f rnnoise_gui pcm_to_wav wav_to_pcm recorder audio_recorder audio_denoiser rnnoise_audio
