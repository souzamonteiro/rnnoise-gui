all: rnnoise_gui pcm_to_wav wav_to_pcm recorder audio_recorder rnnoise_audio_recorder

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

clean:
	rm -f rnnoise_gui pcm_to_wav wav_to_pcm recorder audio_recorder
