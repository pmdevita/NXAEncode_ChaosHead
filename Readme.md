# NX Opus Encoder for Chaos;Head NOAH

Quick hack/proof of concept to get an Opus encoder working for the Nintendo Switch release of Chaos;Head NOAH. It 
builds off previous work by [tellowkrinkle](https://gist.github.com/tellowkrinkle/91423d561d8976be418ba770b9499bb3) and
[masagrator and Nazosan](https://github.com/masagrator/NXGameScripts/blob/main/Made%20In%20Abyss/OpusEncoder/NXAEnc.c).

The format seems largely the same as that used in Made in Abyss, with the inclusion of a new section for information 
about looping. A quick attempt was made to leave feature parity for both, but it's unfinished.

You can compile on Linux with `gcc main.c -lopus -o nxaencode` or with cmake, Mac should also work like this too. You'll need to have 
the Opus libraries installed.

Windows should be compatible, but I'm not familiar enough with the tooling to get it to work.

## Usage

```
Usage: ./nxaencode OPTIONS
Options:
        -r sampleRate:  Sample rate (default: 48000)
        -c channels:    Number of channels (default: 2)
        -s frameSize:   Size of a frame in samples (default: 960)
        -f frameBytes:  Size of an encoded frame in bytes (default: 420)
        -b repeatBegin: Start point in samples for repeat (default: 0)
        -e repeatEnd:   End point in samples for repeat (0 for end of file, default: 0)
        -i inputFile:   Path to input file of raw s16le audio (default: stdin)
        -o outputFile:  Path to output opus file (default: stdout)
```
