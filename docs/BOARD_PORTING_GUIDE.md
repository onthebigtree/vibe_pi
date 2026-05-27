# Board Porting Guide

How to add support for a new ESP32 display board.

## Prerequisites

- PlatformIO installed
- The target board in hand
- Pin mapping documentation (from manufacturer or schematic)

## Steps

### 1. Create Board Definition

Copy the template:

```bash
cp -r firmware/boards/_template firmware/boards/your_board_name
mv firmware/boards/your_board_name/board_def.h.template firmware/boards/your_board_name/board_def.h
```

Fill in `board_def.h`:
- Set display dimensions, shape, pin mappings
- Set touch IC pins (if applicable)
- Set capabilities (PSRAM, IMU, mic, etc.)
- Set the `BSP_BOARD_INFO` struct

### 2. Check Existing Drivers

Look in `firmware/drivers/display/` and `firmware/drivers/touch/` for your display and touch IC.

**Supported display drivers:**
| Driver IC | File | Interface |
|-----------|------|-----------|
| CO5300 | `co5300_driver.h` | QSPI |
| GC9A01 | `gc9a01_driver.h` | SPI |

**Supported touch drivers:**
| Touch IC | File | Interface |
|----------|------|-----------|
| CST9217 | `cst9217_driver.h` | I2C |
| CST816S | `cst816_driver.h` | I2C |

If your IC isn't listed, you'll need to write a new driver implementing `DisplayHAL` or `TouchHAL` (see `firmware/hal/`).

### 3. Register in Board Registry

Edit `firmware/hal/board_registry.cpp`:

```cpp
#elif defined(BOARD_YOUR_BOARD_NAME)
    #include "boards/your_board_name/board_def.h"
```

And add the display/touch driver factory calls.

### 4. Add PlatformIO Environment

Add to `firmware/platformio.ini`:

```ini
[env:your-board-name]
extends = esp32s3  ; or appropriate variant
board = esp32-s3-devkitc-1
build_flags =
    ${esp32s3.build_flags}
    -DBOARD_YOUR_BOARD_NAME
```

### 5. Build and Test

```bash
cd firmware
pio run -e your-board-name
pio run -e your-board-name -t upload
pio device monitor
```

Verify:
- [ ] Display initializes (check serial output)
- [ ] Touch works (if applicable)
- [ ] WiFi connects
- [ ] OOBE flow completes
- [ ] Dashboard displays correctly

### 6. Update boards.json

Add your board entry to `boards.json` in the project root.

### 7. Submit PR

Include:
- All new/modified files
- A photo of the board running Vibe Pi
- Which features you tested (see testing matrix)
- Set `"status": "community"` in boards.json

## Layout Tips

The UI system uses proportional sizing via `pct_w()` / `pct_h()` helpers from `hal/board.h`. If your screen is a very different size (e.g. 170x320 rectangular), you may need to adjust widget placement in the UI pages. The `theme.h` macros like `ARC_OUTER_SIZE` already scale proportionally.

For rectangular screens, the `scr_round()` function returns `false`, which UI code can use to switch layout strategies.
