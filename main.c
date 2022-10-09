#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <opus/opus.h>

struct NXAHeader {
    uint8_t magic[4];
    uint32_t version;
    uint32_t filesize;
    uint32_t sampleRate;
    uint16_t channelCount;
    uint16_t frameBytes;
    uint16_t frameSize;
    uint16_t preSkip;
    uint32_t numSamples;
    uint32_t repeatStartSamples;
    uint32_t repeatEndSamples;
    uint32_t unknown[3];
};

typedef struct uint32_be {
    uint8_t data[4];
} uint32_be_t;

uint32_be_t make_32_be(uint32_t i) {
    uint32_be_t o;
    o.data[0] = (i >> 24) & 0xFF;
    o.data[1] = (i >> 16) & 0xFF;
    o.data[2] = (i >>  8) & 0xFF;
    o.data[3] = (i >>  0) & 0xFF;
    return o;
}

struct NXAv1FrameHeader {
    uint32_be_t length;
    uint32_be_t unknown;
};

struct OutputBuffer {
    struct OutputBuffer *next;
    uint8_t data[];
};

void printUsage(const char *progName) {
    fprintf(stderr, "Usage: %s OPTIONS\n", progName);
    const char *str =
            "Options:\n"
            "\t-r sampleRate:  Sample rate (default: 48000)\n"
            "\t-c channels:    Number of channels (default: 2)\n"
            "\t-s frameSize:   Size of a frame in samples (default: 960)\n"
            "\t-f frameBytes:  Size of an encoded frame in bytes (default: 420)\n"
            "\t-v version:     NXA version (1 or 2, higurashi uses v1, default: 1)\n"
            "\t-b repeatBegin: Start point in samples for repeat (default: 0)\n"
            "\t-e repeatEnd:   End point in samples for repeat (0 for end of file, default: 0)\n"
            "\t-i inputFile:   Path to input file of raw s16le audio (default: stdin)\n"
            "\t-o outputFile:  Path to output nxa file (default: stdout)\n";
    fputs(str, stderr);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sampleRate = 48000;
    int channels = 2;
    int frameSize = 960;
    int frameBytes = 420;
    int version = 1;
    int repeatStartSamples = 0;
    int repeatEndSamples = 0;
    FILE *input = stdin;
    FILE *output = stdout;

    int option;
    while ((option = getopt(argc, argv, "r:c:s:f:v:b:e:i:o:")) != -1) {
        switch (option) {
            case 'r': sampleRate = atoi(optarg); break;
            case 'c': channels   = atoi(optarg); break;
            case 's': frameSize  = atoi(optarg); break;
            case 'f': frameBytes = atoi(optarg); break;
            case 'v': version    = atoi(optarg); break;
            case 'b': repeatStartSamples = atoi(optarg); break;
            case 'e': repeatEndSamples   = atoi(optarg); break;
            case 'i':
                input = fopen(optarg, "rb");
                break;
            case 'o':
                output = fopen(optarg, "wb");
                break;
            case '?':
                if (strchr("rcsfvbeio", optopt)) {
                    fprintf(stderr, "Option %c requires an argument\n", optopt);
                    printUsage(argv[0]);
                }
                fprintf(stderr, "Unknown option %c\n", optopt);
                printUsage(argv[0]);
        }
    }

    if (!input || !output) {
        fprintf(stderr, "Couldn't open %s file!\n", input ? "output" : "input");
        printUsage(argv[0]);
    }

    if (isatty(fileno(input)) || isatty(fileno(output))) {
        printUsage(argv[0]);
    }

    int sampleSize = channels * sizeof(short);
    float framesPerSecond = (float)sampleRate / (float)frameSize;
    float bitsPerSecond = framesPerSecond * frameBytes * 8;
    int frameHeaderBytes = version == 1 ? 8 : 0;

    int err;
    OpusEncoder *enc = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_AUDIO, &err);
    opus_encoder_ctl(enc, OPUS_SET_VBR(0));
    opus_encoder_ctl(enc, OPUS_SET_BITRATE((int)bitsPerSecond));

    void *inputBuffer = malloc(frameSize * sampleSize);
    struct OutputBuffer *head = NULL, *tail = NULL;
    int frames = 0;
    int numSamples = 0;

    while (1) {
        memset(inputBuffer, 0, frameSize * sampleSize);
        unsigned long amt = fread(inputBuffer, sampleSize, frameSize, input);
        if (amt == 0) { break; }
        frames++;
        numSamples += amt;

        struct OutputBuffer *tmp = malloc(sizeof(struct OutputBuffer) + frameBytes);
        tmp->next = NULL;
        if (!head) { head = tmp; }
        if (tail) { tail->next = tmp; }
        tail = tmp;

        int err = opus_encode(enc, inputBuffer, frameSize, tmp->data, frameBytes);
        if (err != frameBytes) {
            fprintf(stderr, "Encoder failed: %d\n", err);
            return EXIT_FAILURE;
        }
    }

    struct NXAHeader header = {
            .magic = "NXA1",
            .version = version,
            .filesize = sizeof(struct NXAHeader) + (frameHeaderBytes + frameBytes) * frames,
            .sampleRate = sampleRate,
            .channelCount = channels,
            .frameBytes = frameBytes,
            .frameSize = frameSize,
            .preSkip = 0,
            .numSamples = numSamples,
            .repeatStartSamples = repeatStartSamples,
            .repeatEndSamples = repeatEndSamples ? repeatEndSamples : numSamples,
            .unknown = {0}
    };

    fwrite(&header, sizeof(header), 1, output);
    for (struct OutputBuffer *frame = head; frame; frame = frame->next) {
        if (version == 1) {
            struct NXAv1FrameHeader frameHeader = {
                    .length = make_32_be(frameBytes),
                    .unknown = make_32_be(0)
            };
            fwrite(&frameHeader, sizeof(frameHeader), 1, output);
        }
        fwrite(frame->data, frameBytes, 1, output);
    }

    return 0;
}
