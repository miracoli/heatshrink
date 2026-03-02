#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>

#include "heatshrink_decoder.h"
#include "heatshrink_encoder.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define INPUT_LEN 68u
#define COMPRESSED_CAPACITY 128u

#define AVR_LABEL(name) __asm__ __volatile__(".global " #name "\n" #name ":\n" ::: "memory")

static const uint8_t sample_plaintext[INPUT_LEN] PROGMEM =
    "This is AVR test data. This is AVR test data. This is AVR test data.";

static const uint8_t sample_compressed[29] PROGMEM = {
    0xaa, 0x5a, 0x2d, 0x37, 0x39, 0x00, 0x08, 0xa8,
    0x35, 0x6a, 0x94, 0x82, 0xe9, 0x65, 0xb9, 0xdd,
    0x24, 0x16, 0x4b, 0x0d, 0xd2, 0xc3, 0x2e, 0x90,
    0x05, 0xbc, 0x2d, 0xe1, 0x6c,
};

volatile uint8_t test_status;

static uint8_t run_compress_test(void) {
    uint8_t plaintext[INPUT_LEN];
    uint8_t compressed[COMPRESSED_CAPACITY];
    size_t sunk = 0;
    size_t produced = 0;
    heatshrink_encoder hse;

    memcpy_P(plaintext, sample_plaintext, INPUT_LEN);
    heatshrink_encoder_reset(&hse);

    while (sunk < INPUT_LEN) {
        size_t input_size = 0;
        HSE_sink_res sink_res = heatshrink_encoder_sink(
            &hse, plaintext + sunk, INPUT_LEN - sunk, &input_size);
        if (sink_res != HSER_SINK_OK || input_size == 0) {
            return 1;
        }
        sunk += input_size;

        while (1) {
            size_t output_size = 0;
            HSE_poll_res poll_res = heatshrink_encoder_poll(
                &hse, compressed + produced, COMPRESSED_CAPACITY - produced,
                &output_size);
            produced += output_size;
            if (poll_res == HSER_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSER_POLL_MORE || produced >= COMPRESSED_CAPACITY) {
                return 2;
            }
        }
    }

    while (1) {
        HSE_finish_res fin = heatshrink_encoder_finish(&hse);
        while (1) {
            size_t output_size = 0;
            HSE_poll_res poll_res = heatshrink_encoder_poll(
                &hse, compressed + produced, COMPRESSED_CAPACITY - produced,
                &output_size);
            produced += output_size;
            if (poll_res == HSER_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSER_POLL_MORE || produced >= COMPRESSED_CAPACITY) {
                return 3;
            }
        }
        if (fin == HSER_FINISH_DONE) {
            break;
        }
        if (fin != HSER_FINISH_MORE) {
            return 4;
        }
    }

    if (produced != ARRAY_LEN(sample_compressed)) {
        return 5;
    }
    for (size_t i = 0; i < produced; i++) {
        if (compressed[i] != pgm_read_byte(&sample_compressed[i])) {
            return 6;
        }
    }

    return 0;
}

static uint8_t run_decompress_test(void) {
    uint8_t compressed[ARRAY_LEN(sample_compressed)];
    uint8_t output[INPUT_LEN];
    size_t sunk = 0;
    size_t produced = 0;
    heatshrink_decoder hsd;

    memcpy_P(compressed, sample_compressed, ARRAY_LEN(sample_compressed));
    heatshrink_decoder_reset(&hsd);

    while (sunk < ARRAY_LEN(sample_compressed)) {
        size_t input_size = 0;
        HSD_sink_res sink_res = heatshrink_decoder_sink(
            &hsd, compressed + sunk, ARRAY_LEN(sample_compressed) - sunk,
            &input_size);
        if ((sink_res != HSDR_SINK_OK && sink_res != HSDR_SINK_FULL) ||
            input_size == 0) {
            return 7;
        }
        sunk += input_size;

        while (1) {
            size_t output_size = 0;
            HSD_poll_res poll_res = heatshrink_decoder_poll(
                &hsd, output + produced, INPUT_LEN - produced, &output_size);
            produced += output_size;
            if (poll_res == HSDR_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSDR_POLL_MORE || produced > INPUT_LEN) {
                return 8;
            }
        }
    }

    while (1) {
        HSD_finish_res fin = heatshrink_decoder_finish(&hsd);
        while (1) {
            size_t output_size = 0;
            HSD_poll_res poll_res = heatshrink_decoder_poll(
                &hsd, output + produced, INPUT_LEN - produced, &output_size);
            produced += output_size;
            if (poll_res == HSDR_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSDR_POLL_MORE || produced > INPUT_LEN) {
                return 9;
            }
        }
        if (fin == HSDR_FINISH_DONE) {
            break;
        }
        if (fin != HSDR_FINISH_MORE) {
            return 10;
        }
    }

    if (produced != INPUT_LEN) {
        return 11;
    }
    for (size_t i = 0; i < INPUT_LEN; i++) {
        if (output[i] != pgm_read_byte(&sample_plaintext[i])) {
            return 12;
        }
    }

    return 0;
}

int main(void) {
    test_status = 0xff;

    AVR_LABEL(compress_start);
    uint8_t res = run_compress_test();
    AVR_LABEL(compress_end);
    if (res != 0) {
        test_status = res;
        goto test_fail;
    }

    AVR_LABEL(decompress_start);
    res = run_decompress_test();
    AVR_LABEL(decompress_end);

    test_status = res;
    if (res == 0) {
        __asm__ __volatile__(".global test_ok\n"
                             "test_ok:\n" ::: "memory");
        __asm__ __volatile__("break");
    }

test_fail:
    __asm__ __volatile__(".global test_fail\n"
                         "test_fail:\n" ::: "memory");
    __asm__ __volatile__("break");
    return 0;
}
