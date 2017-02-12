# cmd_synth

reverse polish notation synth. takes synth descriptions on stdin, and writes a wav to stdout.

# OSX usage

this will play an A note for 1 second:

````echo "440 sin" | synth 1 > tmp.wav && afplay tmp.wav````
