#pragma once

// QCC Bluetooth I2S bridge configuration.
// Format: 0 = Philips/I2S, 1 = MSB/left-justified, 2 = PCM short.
#define BT_I2S_SAMPLE_RATE 96000
#define BT_I2S_BITS 32
#define BT_I2S_RX_MASTER false
#define BT_I2S_USE_MCLK false
#define BT_I2S_FORMAT 0
#define BT_I2S_BCLK_INV false
#define BT_I2S_WS_INV false

// For 24/32-bit RX slots: 16 = upper 16 bits, 8 = middle 16 bits, 0 = lower 16 bits.
#define BT_I2S_32_SHIFT 16

// Visual-only gain for the BT spectrum analyzer path. Audio output is not changed.
#define BT_SPECTRUM_GAIN 2
