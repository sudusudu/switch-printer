# switch-printer

Nintendo Switch NRO: 3D printer controller via USB-OTG + WiFi web interface.

## Features

- USB-OTG connection to 3D printers via CH340 serial chip (250000 baud)
- Local console UI with Unicode progress bars and temperature display
- Mainsail-style Web dashboard with SVG ring gauges (port 8080)
- G-code file upload, print control (start/pause/resume/cancel)
- Manual jog controls (X/Y/Z) via gamepad and Web UI
- G91 relative positioning for safe incremental moves
- CSRF protection (POST for mutating APIs) + X-Auth-Token authentication
- Streaming file upload with atomic rename (tmp → final)

## Requirements

- Nintendo Switch with Atmosphere CFW
- USB-OTG cable (USB-C to USB-A adapter)
- CH340-based 3D printer

## Build

Requires [devkitPro](https://devkitpro.org/) with devkitA64 and libnx.

```bash
export DEVKITPRO=/opt/devkitpro
make
```

Output: `build/switch_printer.nro`

## Configuration

Edit `source/config.h` before building to set printer bed size, baud rate, etc.

## Usage

1. Copy `switch_printer.nro` to `sdmc:/switch/` on your Switch
2. Connect printer via USB-OTG cable
3. Launch via hbmenu
4. Press [+] to scan for printer
5. Access Web UI at `http://<switch-ip>:8080` (token shown on Switch screen)
6. Upload G-code files via Web UI ("Upload & Print" button)

## Controls

| Button | Action |
|--------|--------|
| [+] | Connect / Re-scan printer |
| [-] | Disconnect printer (auto-cancels print) |
| A | Pause / Resume print |
| B | Cancel print |
| X | Exit |

## License

MIT
