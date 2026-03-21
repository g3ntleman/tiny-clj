#include "tinyclj_idf_display.h"

#if defined(TINYCLJ_WITH_TINY_FX) && TINYCLJ_WITH_TINY_FX

#include <string.h>

#include "panel.h"
#include "panel_esp_lcd.h"
#include "vector_handheld_config.h"

#if defined(ESP_PLATFORM) && defined(__has_include)
#if __has_include(<driver/gpio.h>) && __has_include(<driver/spi_master.h>) && __has_include(<esp_lcd_io_spi.h>) && __has_include(<esp_lcd_panel_vendor.h>)
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_vendor.h>
#define TINYCLJ_HAVE_IDF_DISPLAY_HEADERS 1
#else
#define TINYCLJ_HAVE_IDF_DISPLAY_HEADERS 0
#endif
#elif defined(ESP_PLATFORM)
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_vendor.h>
#define TINYCLJ_HAVE_IDF_DISPLAY_HEADERS 1
#else
#define TINYCLJ_HAVE_IDF_DISPLAY_HEADERS 0
#endif

#if !TINYCLJ_HAVE_IDF_DISPLAY_HEADERS
static TinycljIdfDisplay g_tinyclj_idf_display = {0};

bool tinyclj_idf_display_init(TinycljIdfDisplay *display) {
    (void)display;
    return false;
}

bool tinyclj_idf_display_bootstrap(void) {
    return false;
}

TinycljIdfDisplay *tinyclj_idf_display_get(void) {
    return g_tinyclj_idf_display.initialized ? &g_tinyclj_idf_display : NULL;
}
#else

static TinycljIdfDisplay g_tinyclj_idf_display = {0};

static bool tinyclj_idf_display_backlight_on(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VG_PIN_TFT_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        return false;
    }
    return gpio_set_level(VG_PIN_TFT_BL, 1) == ESP_OK;
}

bool tinyclj_idf_display_init(TinycljIdfDisplay *display) {
    if (!display) {
        return false;
    }
    memset(display, 0, sizeof(*display));

    spi_bus_config_t buscfg = {
        .sclk_io_num = VG_PIN_TFT_SCLK,
        .mosi_io_num = VG_PIN_TFT_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = VG_DISP_WIDTH * 40 * (int)sizeof(uint16_t),
    };
    if (spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = VG_PIN_TFT_DC,
        .cs_gpio_num = VG_PIN_TFT_CS,
        .pclk_hz = VG_TFT_SPI_HZ,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = VG_PIN_TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle) != ESP_OK) {
        return false;
    }

    display->panel_io_handle = io_handle;
    display->panel_handle = panel_handle;
    vg_esp_lcd_panel_init_handle(&display->panel, panel_handle);
    if (!vg_panel_reset(&display->panel.panel) ||
        !vg_panel_init(&display->panel.panel) ||
        !vg_panel_set_display_enabled(&display->panel.panel, true)) {
        return false;
    }
    if (!tinyclj_idf_display_backlight_on()) {
        return false;
    }
    display->initialized = true;
    return true;
}

bool tinyclj_idf_display_bootstrap(void) {
    if (g_tinyclj_idf_display.initialized) {
        return true;
    }
    return tinyclj_idf_display_init(&g_tinyclj_idf_display);
}

TinycljIdfDisplay *tinyclj_idf_display_get(void) {
    return g_tinyclj_idf_display.initialized ? &g_tinyclj_idf_display : NULL;
}
#endif

#endif
