all: rnnoise_gui pcm_to_wav wav_to_pcm

rnnoise_gui: rnnoise_gui.c
	gcc -o rnnoise_gui rnnoise_gui.c `pkg-config --cflags --libs gtk+-3.0` -lrnnoise

pcm_to_wav: pcm_to_wav.c
	gcc -o pcm_to_wav pcm_to_wav.c `pkg-config --cflags --libs gtk+-3.0`

wav_to_pcm: wav_to_pcm.c
	gcc -o wav_to_pcm wav_to_pcm.c `pkg-config --cflags --libs gtk+-3.0`

clean:
	rm -f rnnoise_gui pcm_to_wav wav_to_pcm
