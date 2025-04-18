# WaterMe

WaterMe is an ESP32-based automatic plant watering system that supports both scheduled and manual watering modes. It's designed to be reliable, configurable, and easy to maintain via a web interface.

## Features

- 📅 **Time-Based Watering**: Configure morning and evening schedules.
- 🔘 **Manual Control**: Turn on/off valves anytime from the web UI.
- 🌐 **Wi-Fi Enabled**: Connects to your home Wi-Fi network.
- 🌍 **Web Interface**: Easily update watering times, durations, and Blynk settings.
- 🔄 **Independent Control**: Each valve (upper and lower lines) can be configured separately.
- 🕒 **NTP Time Sync**: Always stays on time with internet-based synchronization.
- 🧠 **Smart Logic**: Prevents back-to-back watering and allows skipping next schedule.
- 🧾 **Logs**: Maintains last 10 watering events in memory.
- 🚀 **OTA Updates**: Upload firmware updates directly via the web interface.

## Hardware Used

- ESP32 development board
- 2-channel Relay module (active HIGH)
- Two 230V AC solenoid valves
- 5V power supply for ESP32 and relays

> ⚠️ No water pump is used in this setup. Water flows through gravity or existing pressure.

## Configuration

After powering up:

1. Connect ESP32 to your home Wi-Fi.
2. Access the device's IP on your browser.
3. Set watering schedules, durations for both valves, and Blynk credentials (if needed).
4. Optionally, trigger manual watering or skip the next schedule.

## Development

- Platform: ESP32 using PlatformIO in VSCode
- OTA: Enabled through web interface
- Time Sync: NTP
- Libraries used: `ESPAsyncWebServer`, `ArduinoJson`, `Preferences`, etc.

## Future Improvements

- Add blynk integration.
- Have configurable number of water lines support.

## License

MIT License – feel free to modify and distribute.
