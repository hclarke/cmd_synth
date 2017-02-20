# cmd_synth

reverse polish notation synth. takes synth descriptions on stdin, and writes a wav to stdout.

## compiling:

it's just one c file.

```gcc main.c -o synth```

## example usage:

this will play an A note for 1 second, and output a wav file:

```echo "440 sin" | synth 1 > out.wav```

to display all of the commands available:

```synth --help```

## example inputs

A440:

```
440 sin
```

middle C (9 half-steps down from A440):

```
440 -9 exp mul sin
```

A440 with vibrato (20 Hz oscilation between 400 and 480 Hz):

```
440 40 20 sin mul add sin
```

vibrato as a function:

```
[ sin mul add sin ] @vibrato
440 40 20 vibrato
```
