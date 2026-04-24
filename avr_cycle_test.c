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
#define COMPRESSED_CAPACITY 256u

#ifndef AVR_CYCLE_WINDOW_BITS
#define AVR_CYCLE_WINDOW_BITS 8
#endif

#ifndef AVR_CYCLE_LOOKAHEAD_BITS
#define AVR_CYCLE_LOOKAHEAD_BITS 4
#endif

#ifndef AVR_CYCLE_DECODER_INPUT_BUFFER_SIZE
#define AVR_CYCLE_DECODER_INPUT_BUFFER_SIZE 256
#endif

#ifndef AVR_CYCLE_MCU_NAME
#define AVR_CYCLE_MCU_NAME "atmega328p"
#endif

#if HEATSHRINK_DYNAMIC_ALLOC
void avr_cycle_alloc_reset(void);
#endif

/* simavr reads target parameters from the ELF and can automatically
 * generate a VCD trace file from metadata embedded by these macros. */
AVR_MCU(F_CPU, AVR_CYCLE_MCU_NAME);
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

static uint8_t generated_compressed[COMPRESSED_CAPACITY];
static size_t generated_compressed_len = 0;

#if HEATSHRINK_DYNAMIC_ALLOC
static heatshrink_encoder *hse_ptr;
static heatshrink_decoder *hsd_ptr;
#else
static union {
    heatshrink_encoder enc;
    heatshrink_decoder dec;
} hs_state;
#endif

static heatshrink_encoder *get_encoder(void) {
#if HEATSHRINK_DYNAMIC_ALLOC
    if (hse_ptr == NULL) {
        hse_ptr = heatshrink_encoder_alloc(AVR_CYCLE_WINDOW_BITS, AVR_CYCLE_LOOKAHEAD_BITS);
    }
    return hse_ptr;
#else
    return &hs_state.enc;
#endif
}

static heatshrink_decoder *get_decoder(void) {
#if HEATSHRINK_DYNAMIC_ALLOC
    if (hsd_ptr == NULL) {
        hsd_ptr = heatshrink_decoder_alloc(AVR_CYCLE_DECODER_INPUT_BUFFER_SIZE,
                                           AVR_CYCLE_WINDOW_BITS,
                                           AVR_CYCLE_LOOKAHEAD_BITS);
    }
    return hsd_ptr;
#else
    return &hs_state.dec;
#endif
}

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
    uint8_t output_chunk[16];
    size_t sunk = 0;
    size_t produced = 0;
    heatshrink_encoder *hse = get_encoder();
    if (hse == NULL) {
        return 14;
    }

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
                                        output_chunk,
                                        sizeof(output_chunk),
                                        &output_size);
            for (size_t i = 0; i < output_size; i++) {
                if (produced >= ARRAY_LEN(generated_compressed)) {
                    return 13;
                }
                generated_compressed[produced++] = output_chunk[i];
            }
            if (poll_res == HSER_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSER_POLL_MORE) {
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
                                        output_chunk,
                                        sizeof(output_chunk),
                                        &output_size);
            for (size_t i = 0; i < output_size; i++) {
                if (produced >= ARRAY_LEN(generated_compressed)) {
                    return 13;
                }
                generated_compressed[produced++] = output_chunk[i];
            }
            if (poll_res == HSER_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSER_POLL_MORE) {
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

    if (produced == 0) {
        return 5;
    }

    generated_compressed_len = produced;
    return 0;
}

static uint8_t run_decompress_test(void) {
    uint8_t output_chunk[16];
    size_t sunk = 0;
    size_t produced = 0;
    heatshrink_decoder *hsd = get_decoder();
    if (hsd == NULL) {
        return 15;
    }

    heatshrink_decoder_reset(hsd);

    while (sunk < generated_compressed_len) {
        size_t input_size = 0;
        HSD_sink_res sink_res =
            heatshrink_decoder_sink(hsd,
                                    generated_compressed + sunk,
                                    generated_compressed_len - sunk,
                                    &input_size);
        if ((sink_res != HSDR_SINK_OK && sink_res != HSDR_SINK_FULL) || input_size == 0) {
            return 7;
        }
        sunk += input_size;

        while (1) {
            size_t output_size = 0;
            HSD_poll_res poll_res =
                heatshrink_decoder_poll(hsd,
                                        output_chunk,
                                        sizeof(output_chunk),
                                        &output_size);
            for (size_t i = 0; i < output_size; i++) {
                if (produced >= INPUT_LEN) {
                    return 11;
                }
                if (output_chunk[i] != pgm_read_byte(&sample_plaintext[produced])) {
                    return 12;
                }
                produced++;
            }
            if (poll_res == HSDR_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSDR_POLL_MORE) {
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
                                        output_chunk,
                                        sizeof(output_chunk),
                                        &output_size);
            for (size_t i = 0; i < output_size; i++) {
                if (produced >= INPUT_LEN) {
                    return 11;
                }
                if (output_chunk[i] != pgm_read_byte(&sample_plaintext[produced])) {
                    return 12;
                }
                produced++;
            }
            if (poll_res == HSDR_POLL_EMPTY) {
                break;
            }
            if (poll_res != HSDR_POLL_MORE) {
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
    return 0;
}

int main(void) {
#if HEATSHRINK_DYNAMIC_ALLOC
    avr_cycle_alloc_reset();
#endif
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
#if HEATSHRINK_DYNAMIC_ALLOC
    if (hse_ptr != NULL) heatshrink_encoder_free(hse_ptr);
    if (hsd_ptr != NULL) heatshrink_decoder_free(hsd_ptr);
#endif
    phase_marker = 5;
    trace_settle();
    stop_simulation();
    return 0;
}
