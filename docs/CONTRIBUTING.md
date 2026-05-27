# Contributing to Vibe Pi

Thanks for your interest! Here's how to contribute.

## Types of Contributions

### Adding a New Board

See [BOARD_PORTING_GUIDE.md](BOARD_PORTING_GUIDE.md) for step-by-step instructions.

### Adding a Collector (Host Agent)

1. Create `host/src/collectors/your_tool.py`
2. Implement `BaseCollector` (see `base.py` for the interface)
3. Register in `host/src/collectors/__init__.py`
4. Add a toggle in `host/src/config.py`

### Bug Fixes

1. Check existing issues first
2. Create a branch from `main`
3. Include serial output or logs if relevant
4. One fix per PR

## Development Setup

### Firmware

```bash
pip install platformio
cd firmware
pio run -e waveshare-175-amoled  # or your board
```

### Host Agent

```bash
cd host
pip install -e '.[dev,web]'
ruff check src/        # lint
pytest tests/          # test
```

## Code Style

- **Firmware (C++)**: 4-space indent, snake_case functions, PascalCase classes
- **Host (Python)**: ruff formatter, type hints, 100 char line limit
- **Commits**: conventional commits style (`feat:`, `fix:`, `docs:`, `board:`)

## Pull Request Checklist

- [ ] Compiles on all supported boards (`pio run`)
- [ ] No new ruff warnings for Python code
- [ ] Updated `boards.json` if adding a board
- [ ] Updated `README.md` board table if adding a board
- [ ] Tested on real hardware (or noted as untested)
