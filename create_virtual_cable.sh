#!/bin/sh

#sudo apt install pulseaudio-utils

pactl load-module module-null-sink sink_name="Audio Denoiser"
#pactl unload-module module-null-sink
