#ifndef TINYCLJ_VECTOR_HANDHELD_CONFIG_H
#define TINYCLJ_VECTOR_HANDHELD_CONFIG_H

/*
 * Board profile for the ESP32 vector handheld project.
 *
 * This file is intentionally pin-focused and game-specific. Keep it as the
 * single source of truth for wiring, then include it from display/input/sound
 * drivers.
 */

/* ----------------------------- Display (ST7789) --------------------------- */
#define VG_DISP_WIDTH 320
#define VG_DISP_HEIGHT 240

#define VG_PIN_TFT_SCLK 18
#define VG_PIN_TFT_MOSI 23
#define VG_PIN_TFT_CS 5
#define VG_PIN_TFT_DC 16
#define VG_PIN_TFT_RST 17
#define VG_PIN_TFT_BL 4

/* SPI clock for ST7789. 40 MHz is usually stable on short wires. */
#define VG_TFT_SPI_HZ 40000000

/* ------------------------------- Input ------------------------------------ */
#define VG_PIN_ENC_A 32
#define VG_PIN_ENC_B 33
#define VG_PIN_ENC_SW 25

#define VG_PIN_BTN_UP 26
#define VG_PIN_BTN_DOWN 27
#define VG_PIN_BTN_LEFT 14
#define VG_PIN_BTN_RIGHT 12

/* Optional action buttons; set to -1 if not assembled. */
#define VG_PIN_BTN_A 13
#define VG_PIN_BTN_B -1

/* Active-low buttons with internal pullups. */
#define VG_BUTTON_ACTIVE_LOW 1

/* ------------------------------- Audio ------------------------------------ */
#define VG_PIN_PIEZO_1 21
#define VG_PIN_PIEZO_2 22

/*
 * LEDC uses timer + channel pairs.
 * Use separate channels to keep two independent voices.
 */
#define VG_SOUND_LEDC_TIMER 0
#define VG_SOUND_LEDC_CH1 0
#define VG_SOUND_LEDC_CH2 1

/* -------------------------- Battery monitoring ---------------------------- */
#define VG_PIN_BAT_ADC 35
#define VG_BAT_ADC_SAMPLES 16

/*
 * Divider ratio as millivolt scaling:
 * batt_mv = adc_mv * VG_BAT_DIV_NUM / VG_BAT_DIV_DEN
 * Example for 100k/100k divider => x2.
 */
#define VG_BAT_DIV_NUM 2
#define VG_BAT_DIV_DEN 1

/* ------------------------------ Timing ------------------------------------ */
#define VG_TARGET_FPS 60
#define VG_FRAME_MS (1000 / VG_TARGET_FPS)
#define VG_SOUND_TICK_MS 1

#endif /* TINYCLJ_VECTOR_HANDHELD_CONFIG_H */
