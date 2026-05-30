#include "board.h"

// ── Compile-time board selection ──
#if defined(BOARD_WAVESHARE_175_AMOLED)
    #include "boards/waveshare_175_amoled/board_def.h"
#else
    #error "No board selected. Define BOARD_xxx in platformio.ini build_flags."
#endif

// ── Driver includes ──
#if defined(BOARD_WAVESHARE_175_AMOLED)
    #include "drivers/display/co5300_driver.h"
    #include "drivers/touch/cst9217_driver.h"
#endif

static DisplayConfig g_disp_config;

const BoardInfo &board_get_info() {
    return BSP_BOARD_INFO;
}

DisplayHAL *board_create_display() {
    DisplayHAL *drv = nullptr;

#if defined(BOARD_WAVESHARE_175_AMOLED)
    drv = new CO5300Driver(BSP_QSPI_CS, BSP_QSPI_SCK,
                           BSP_QSPI_D0, BSP_QSPI_D1, BSP_QSPI_D2, BSP_QSPI_D3,
                           BSP_DISP_RST, BSP_DISP_TE);
#endif

    if (drv) {
        g_disp_config = drv->config();
    }
    return drv;
}

TouchHAL *board_create_touch() {
#if defined(BOARD_WAVESHARE_175_AMOLED)
    return new CST9217Driver(BSP_TOUCH_SDA, BSP_TOUCH_SCL, BSP_TOUCH_INT, BSP_TOUCH_RST,
                             BSP_DISPLAY_WIDTH, BSP_DISPLAY_HEIGHT);
#else
    return nullptr;
#endif
}

const DisplayConfig &board_display_config() {
    return g_disp_config;
}
