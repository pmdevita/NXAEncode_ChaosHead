/*Work based on code from here:
https://gist.github.com/tellowkrinkle/91423d561d8976be418ba770b9499bb3
https://github.com/masagrator/NXGameScripts/blob/main/Made%20In%20Abyss/OpusEncoder/NXAEnc.c
Input file must be raw PCM audio:
- 48000 Hz
- S16LE
*/

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <opus/opus.h>

#define SECTION_START_HEADER 0x80000001
#define SECTION_LOOP_HEADER 0x80000003
#define SECTION_END_HEADER 0x80000004

struct NXAHeader {
    uint32_t header;
    uint32_t chunksize;
    uint8_t version;
    uint8_t channelCount;
    uint16_t frameSize;
    uint32_t sampleRate;
    uint16_t dataOffset;
    uint32_t unknown;
    uint32_t MAGIC2;
    uint32_t eachChunkDataSize;
};

struct NXAHeader_Loop {
    uint32_t header;
    uint32_t magic;
    uint32_t loopFlag;
    uint32_t totalSamples;
    uint32_t startSample;
    uint32_t endSample;
    uint32_t padding[10];
};

struct NXAHeader_Final {
    uint32_t header;
    uint32_t streamSize;
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
    uint32_be_t dataSize;
    uint32_t hash;
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
            "\t-b repeatBegin: Start point in samples for repeat (default: 0)\n"
            "\t-e repeatEnd:   End point in samples for repeat (0 for end of file, default: 0)\n"
            "\t-i inputFile:   Path to input file of raw s16le audio (default: stdin)\n"
            "\t-o outputFile:  Path to output opus file (default: stdout)\n";
    fputs(str, stderr);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sampleRate = 48000;
    int channels = 2;
    int frameSize = 960;
    int frameBytes = 240;
    int version = 0;
    int loopFlag = 0;
    int repeatStartSamples = 0;
    int repeatEndSamples = 0;
    // C;H always has the loop section enabled but MIB did not use it
    int enableLoopSection = 1;

    FILE *input = stdin;
    FILE *output = stdout;

    int option;
    while ((option = getopt(argc, argv, "r:c:s:f:v:b:e:i:o:")) != -1) {
        switch (option) {
            case 'r': sampleRate = atoi(optarg); break;
            case 'c': channels   = atoi(optarg); break;
            case 's': frameSize  = atoi(optarg); break;
            case 'f': frameBytes = atoi(optarg); break;
            case 'b':
                loopFlag = 1;
                repeatStartSamples = atoi(optarg);
                break;
            case 'e':
                loopFlag = 1;
                repeatEndSamples   = atoi(optarg);
                break;
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
    int frameHeaderBytes = version == 0 ? 8 : 0;

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

    struct NXAHeader first_section = {
        .header = SECTION_START_HEADER,
        .chunksize = 24,
        .version = version,
        .dataOffset = enableLoopSection ? 0x60 : 0x20,
        .MAGIC2 = 0x00000020,
        .sampleRate = sampleRate,
        .channelCount = channels,
        .frameSize = 0,
//      .eachChunkDataSize = 0x78,  // I think the below is equivalent but not a magic number
        .eachChunkDataSize = frameBytes / 2,
    };

    // Chaos;Head NOAH is largely the same as Made in Abyss, except for this loop section
    struct NXAHeader_Loop loop_section = {
        .header = SECTION_LOOP_HEADER,
        .magic = 0x00000038,
        .loopFlag = loopFlag ? 0x00000100 : 0x0,
        .totalSamples = numSamples,
        .startSample = repeatStartSamples,
        .endSample = repeatEndSamples,
        .padding = 0
    };

    // Again, very similar to MIB
    struct NXAHeader_Final last_section = {
        .header = SECTION_END_HEADER,
        .streamSize = 0,    // We seek back and write this after finishing the opus stream
    };

    fwrite(&first_section, sizeof(first_section), 1, output);
    fwrite(&loop_section, sizeof(loop_section), 1, output);
    fwrite(&last_section, sizeof(last_section), 1, output);
    size_t offset = ftell(output);
    for (struct OutputBuffer *frame = head; frame; frame = frame->next) {
        struct NXAv1FrameHeader frameHeader = {
                .dataSize = make_32_be(frameBytes)
        };
        fwrite(&frameHeader, sizeof(frameHeader), 1, output);
        fwrite(frame->data, frameBytes, 1, output);
    }
    size_t stream_size = ftell(output) - offset;
    // Seek stream back to beginning to write in new stream size
    fseek(output, (enableLoopSection ? 0x60 : 0x20) + 4, 0);
    fwrite(&stream_size, sizeof(uint32_t), 1, output);
    fclose(output);

    printf("Finished. Bitrate: %0.f kbps", bitsPerSecond/1000);

    return 0;
}
