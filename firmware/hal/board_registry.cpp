#include "board.h"

// ── Compile-time board selection ──
#if defined(BOARD_WAVESHARE_175_AMOLED)
    #include "boards/waveshare_175_amoled/board_def.h"
#elif defined(BOARD_GC9A01_ROUND_GENERIC)
    #include "boards/gc9a01_round_generic/board_def.h"
#else
    #error "No board selected. Define BOARD_xxx in platformio.ini build_flags."
#endif

// ── Display driver includes (selected per board) ──
#if defined(BOARD_WAVESHARE_175_AMOLED)
    #include "drivers/display/co5300_driver.h"
    #include "drivers/touch/cst9217_driver.h"
#elif defined(BOARD_GC9A01_ROUND_GENERIC)
    #include "drivers/display/gc9a01_driver.h"
    #include "drivers/touch/cst816_driver.h"
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
#elif defined(BOARD_GC9A01_ROUND_GENERIC)
    drv = new GC9A01Driver(BSP_SPI_MOSI, BSP_SPI_SCK, BSP_SPI_CS,
                           BSP_SPI_DC, BSP_DISP_RST, BSP_DISP_BL);
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
#elif defined(BOARD_GC9A01_ROUND_GENERIC)
    return new CST816Driver(BSP_TOUCH_SDA, BSP_TOUCH_SCL, BSP_TOUCH_INT, BSP_TOUCH_RST,
                            BSP_DISPLAY_WIDTH, BSP_DISPLAY_HEIGHT);
#else
    return nullptr;
#endif
}

const DisplayConfig &board_display_config() {
    return g_disp_config;
}
