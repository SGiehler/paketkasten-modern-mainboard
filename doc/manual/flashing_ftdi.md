# Flashing with FTDI Adapter

This guide explains how to flash the firmware and the LittleFS filesystem to your device using an FTDI adapter and the `esptool` command-line utility.

## Prerequisites

1.  **esptool:** Ensure you have [esptool v5.1.0 or newer](https://github.com/espressif/esptool/releases/tag/v5.1.0) installed and accessible from your command line.
2.  **FTDI Adapter:** A working FTDI adapter connected to your device's programming pins.
3.  **Binaries:** Download the latest release assets from the [GitHub Releases](https://github.com/SGiehler/paketkasten-modern-mainboard/releases) page. You will need `complete-firmware.bin`.

## Flashing Procedure

1.  **Enter Bootloader Mode:**
    *   Press and hold the **Boot** button on the board.
    *   While holding the **Boot** button, press and release the **Reset** button.
    *   Release the **Boot** button. The device is now in bootloader mode and ready to be flashed.

2.  **Flash the Binaries:**
    Open your terminal or command prompt and navigate to the directory where you downloaded the release files. Run the following command, replacing `<PORT>` with the serial port of your FTDI adapter (e.g., `COM3` on Windows or `/dev/ttyUSB0` on Linux):

    **Bash:**
    ```bash
    esptool --chip esp32 --port <PORT> --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 complete-firmware.bin 
    ```

    This command will flash all the necessary components to their correct locations in the ESP32's flash memory.

After the command completes successfully, `esptool` will automatically reset the device, and it will boot into the new firmware.