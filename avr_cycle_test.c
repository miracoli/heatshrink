#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <avr/avr_mcu_section.h>

#include "heatshrink_decoder.h"
#include "heatshrink_encoder.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define COMPRESSED_CAPACITY 128u

/* simavr reads target parameters from the ELF and can automatically
 * generate a VCD trace file from metadata embedded by these macros. */
AVR_MCU(F_CPU, "atmega328p");
AVR_MCU_VCD_FILE("avr_cycle_trace.vcd", 1000);

/* A small phase marker is enough to delimit the measured regions.
 * The runner converts VCD timestamps back into cycles. */
#define phase_marker GPIOR0
#define test_status GPIOR1

const struct avr_mmcu_vcd_trace_t _mytrace[] _MMCU_ = {
    { AVR_MCU_VCD_SYMBOL("phase_marker"), .what = (void *)&phase_marker, },
    { AVR_MCU_VCD_SYMBOL("test_status"),  .what = (void *)&test_status,  },
};

static const uint8_t sample_plaintext[] PROGMEM =
    "This is AVR test data. This is AVR test data. This is AVR test data.";

#define INPUT_LEN ((size_t)(sizeof(sample_plaintext) - 1u))

static const uint8_t sample_compressed[] PROGMEM = {
    0xaa, 0x5a, 0x2d, 0x37, 0x39, 0x00, 0x08, 0xa8,
    0x35, 0x6a, 0x94, 0x82, 0xe9, 0x65, 0xb9, 0xdd,
    0x24, 0x16, 0x4b, 0x0d, 0xd2, 0xc3, 0x2e, 0x90,
    0x05, 0xbc, 0x2d, 0xe1, 0x6c,
};

/* Reuse the same SRAM region for encoder and decoder state.
 *
 * With HEATSHRINK_DYNAMIC_ALLOC=0, the encoder can be fairly large on AVR due
 * to its static buffer and optional index. Compression and decompression are
 * run sequentially here, so a union avoids paying for both states at once.
 */
static union {
    heatshrink_encoder enc;
    heatshrink_decoder dec;
} hs_state;

/* Older simavr examples referenced a helper named trace_settle(), but recent
 * packaged headers don't expose that symbol. Keep a local equivalent that
 * burns a couple of cycles so VCD edge transitions are clearly separated. */
static void trace_settle(void) {
    for (volatile uint8_t i = 0; i < 32; i++) {
        __asm__ __volatile__("nop" ::: "memory");
    }
}

static void stop_simulation(void) {
    cli();
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    sleep_cpu();
    for (;;) {
    }
}

static uint8_t run_compress_test(void) {
    uint8_t plaintext[INPUT_LEN];
    uint8_t compressed[COMPRESSED_CAPACITY];
    size_t sunk = 0;
    size_t produced = 0;
    heatshrink_encoder *hse = &hs_state.enc;

    memcpy_P(plaintext, sample_plaintext, INPUT_LEN);
    heatshrink_encoder_reset(hse);

    while (sunk < INPUT_LEN) {
        size_t input_size = 0;
        HSE_sink_res sink_res =
            heatshrink_encoder_sink(hse, plaintext + sunk, INPUT_LEN - sunk, &input_size);
        if (sink_res != HSER_SINK_OK || input_size == 0) {
            return 1;
        }
        sunk += input_size;

        while (1) {
            size_t output_size = 0;
            HSE_poll_res poll_res =
                heatshrink_encoder_poll(hse,
                                        compressed + produced,
                                        COMPRESSED_CAPACITY - produced,
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
        HSE_finish_res fin = heatshrink_encoder_finish(hse);
        while (1) {
            size_t output_size = 0;
            HSE_poll_res poll_res =
                heatshrink_encoder_poll(hse,
                                        compressed + produced,
                                        COMPRESSED_CAPACITY - produced,
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

    return 0;
}

static uint8_t run_decompress_test(void) {
    uint8_t compressed[ARRAY_LEN(sample_compressed)];
    uint8_t output[INPUT_LEN];
    size_t sunk = 0;
    size_t produced = 0;
    heatshrink_decoder *hsd = &hs_state.dec;

    memcpy_P(compressed, sample_compressed, ARRAY_LEN(sample_compressed));
    heatshrink_decoder_reset(hsd);

    while (sunk < ARRAY_LEN(sample_compressed)) {
        size_t input_size = 0;
        HSD_sink_res sink_res =
            heatshrink_decoder_sink(hsd,
                                    compressed + sunk,
                                    ARRAY_LEN(sample_compressed) - sunk,
                                    &input_size);
        if ((sink_res != HSDR_SINK_OK && sink_res != HSDR_SINK_FULL) || input_size == 0) {
            return 7;
        }
        sunk += input_size;

        while (1) {
            size_t output_size = 0;
            HSD_poll_res poll_res =
                heatshrink_decoder_poll(hsd,
                                        output + produced,
                                        INPUT_LEN - produced,
                                        &output_size);
            produced += output_size;
            if (poll_res == HSDR_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSDR_POLL_MORE || produced >= INPUT_LEN) {
                return 8;
            }
        }
    }

    while (1) {
        HSD_finish_res fin = heatshrink_decoder_finish(hsd);
        while (1) {
            size_t output_size = 0;
            HSD_poll_res poll_res =
                heatshrink_decoder_poll(hsd,
                                        output + produced,
                                        INPUT_LEN - produced,
                                        &output_size);
            produced += output_size;
            if (poll_res == HSDR_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSDR_POLL_MORE || produced >= INPUT_LEN) {
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
    phase_marker = 0;

    phase_marker = 1;
    uint8_t compress_res = run_compress_test();
    phase_marker = 2;
    trace_settle();

    phase_marker = 3;
    uint8_t decompress_res = run_decompress_test();
    phase_marker = 4;
    trace_settle();

    test_status = (compress_res != 0) ? compress_res : decompress_res;
    phase_marker = 5;
    trace_settle();
    stop_simulation();
    return 0;
}
