# E-FUTÁR / E-FUTAR ![BKK logo](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/bkk_icon.png)

**Embedded FUTÁR application for ESP32 with SSD1306 OLED display**

A real-time public transportation display that shows upcoming bus/tram/metro departures on a compact OLED screen. Originally developed for Budapest's BKK FUTÁR system, now compatible with multiple Hungarian cities using the FUTÁR platform.

## Features

- 📱 **Real-time departure information** from the FUTÁR API
- 🚌 **Multiple stop support** - Switch between configured stops with a button press
- ⏰ **Current time display** with automatic timezone adjustment
- 🔄 **Automatic updates** every 2 seconds
- 💡 **LED status indicator** shows loading state
- 🌐 **WiFi connectivity** with animated connection screen
- 🎯 **Compact display** - Shows next 3 departures with route numbers and arrival times

## Supported Cities

This application works with any city using the FUTÁR public transportation system:

- **Budapest** (BKK FUTÁR) - `https://futar.bkk.hu/api/...`
- **Tatabánya** (TBUSZ) - `https://go.tbusz.hu/api/...`
- Other cities using the FUTÁR platform (check your local transit authority)

## Hardware Requirements

### Recommended Development Board
- **Heltec WiFi LoRa 32 V2/V3** (tested and recommended)
  - ESP32-based SoC
  - Built-in SSD1306 OLED display (128x64 pixels)
  - Built-in push button (GPIO 0)
  - Built-in LED indicator (GPIO 25)

### Compatible Hardware
Any ESP32 development board with:
- ESP32 SoC (dual-core, WiFi enabled)
- SSD1306 OLED display (128x64 pixels, I2C interface)
- At least one GPIO pin for button input (with pull-up resistor)
- Optional: LED indicator

### Pin Configuration
Default pins for Heltec WiFi LoRa 32 V2:
- **OLED_SDA**: GPIO 4
- **OLED_SCL**: GPIO 15
- **OLED_RST**: GPIO 16
- **Button**: GPIO 0 (built-in)
- **LED**: GPIO 25 (built-in)

*For other ESP32 boards, simply modify these pin definitions in the code's Hardware Configuration section.*

## Screenshots

**Boot screen - WiFi connection**

![Boot screen](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/screenshot1.jpg)

**Main display - Departure list**

![Bus list screen](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/screenshot2.jpg)

## How It Works

The program follows a round-robin architecture with interrupt-driven stop switching:

1. **Power On**: Device connects to the configured WiFi network
2. **API Query**: Sends HTTP GET requests to the FUTÁR API every 2 seconds
3. **JSON Parsing**: Extracts departure data (route numbers, destinations, arrival times)
4. **Display Update**: Shows next 3 departures with minutes until arrival
5. **Stop Switching**: Press the built-in button to cycle through configured stops

### Data Flow

```
ESP32 → WiFi → FUTÁR API → JSON Response → Parse Data → OLED Display
                              ↓
                    currentTime (Unix epoch)
                    stopTimes[] (departures)
                    routes{} (line numbers)
                    trips{} (destinations)
```

The API returns predicted arrival times (when GPS tracking is available) or scheduled times as fallback. Times are displayed in minutes with an apostrophe (e.g., "5'", "15'").

### Flowchart

Simplified program operation:

![E-FUTAR flowchart](https://raw.githubusercontent.com/bazsimarkus/E-FUTAR/master/docs/EFUTAR_flowchart.svg)

## Installation & Setup

### 1. Install Arduino IDE

Download and install the Arduino IDE from:
https://www.arduino.cc/en/software

### 2. Install ESP32 Board Support

For **Heltec boards**, follow the installation guide:
https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series

For **generic ESP32 boards**, add this URL to Arduino IDE preferences:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then install "ESP32" boards from the Board Manager.

### 3. Install Required Libraries

Install these libraries via Arduino IDE Library Manager (Sketch → Include Library → Manage Libraries):

| Library | Author | Version |
|---------|--------|---------|
| **ArduinoJson** | Benoit Blanchon | 7.x or higher |
| **ESP8266 and ESP32 OLED driver for SSD1306** | ThingPulse | 4.x or higher |

The following libraries are included with ESP32 board support:
- WiFi
- HTTPClient
- Wire

### 4. Configure the Code

Open `e-futar.ino` and modify the **USER CONFIGURATION** section:

#### a) WiFi Settings
```cpp
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

#### b) API Configuration

**For Budapest (BKK):**
```cpp
const char apiKey[256] = "YOUR_BKK_API_KEY";
const char baseUrl[] = "https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json";
```

**For Tatabánya (TBUSZ):**
```cpp
const char apiKey[256] = "YOUR_TBUSZ_API_KEY"; // Obtain from tbusz.hu
const char baseUrl[] = "https://go.tbusz.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json";
```

*Note: The API key shown is for demonstration purposes. You can request your own API key from the respective transit authority.*

#### c) Time Settings
```cpp
bool summerTime = false; // Set to true during daylight saving time (March-October)
```

#### d) Configure Your Bus Stops

Find your stop IDs using the interactive map:
- **Budapest**: http://futar.bkk.hu/
- **Tatabánya**: https://go.tbusz.hu/

Click on a stop → Select any route → The stop ID appears in the URL

**Budapest stop IDs** start with `BKK_` (e.g., `BKK_F04144`)  
**Tatabánya stop IDs** start with `tbusz_` (e.g., `tbusz_SP17210`)

```cpp
const Stop busStopList[] = {
    {"BKK_F04144", "Baross utca"},      // First stop (default)
    {"BKK_F04126", "János utca"},       // Second stop
    {"BKK_F04122", "Városközpont"}      // Third stop
    // Add more stops as needed
};
```

### 5. Select Your Board

In Arduino IDE:
1. Go to **Tools → Board**
2. For Heltec: Select **"WiFi LoRa 32(V2)"** under "Heltec ESP32 Arduino"
3. For generic ESP32: Select your specific board model

Board settings for Heltec WiFi LoRa 32 V2 (leave everything on the default settings in the Arduino IDE):
```
FQBN: Heltec-esp32:esp32:wifi_lora_32_V2
Board: WiFi LoRa 32(V2)
Upload Speed: 921600
Flash Frequency: 80MHz
Flash Mode: QIO
Flash Size: 8MB
Partition Scheme: Default 8MB
```

### 6. Upload the Code

1. Connect your ESP32 board via USB
2. Select the correct **Port** under Tools → Port
3. Click **Upload** (or press Ctrl+U)

**Expected memory usage:**
- Program storage: ~890 KB / 3.3 MB (27%)
- Dynamic memory: ~42 KB / 327 KB (12%)

### 7. First Run

After upload:
1. The device will show the boot screen "E-FUTÁR"
2. Wait for WiFi connection (animated dots)
3. The main screen will display with stop name, current time, and next 3 departures

## Usage

### Switching Between Stops

Press the **built-in button** (GPIO 0 on Heltec boards) to cycle through configured stops:

![Button location](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/changeStops.jpg)

- **Blue square**: Button to change stops
- **Black square**: Status LED (lights up during data loading)

When pressed:
1. LED turns ON (loading new data)
2. Display updates with new stop name
3. Departure list refreshes for the new stop
4. LED turns OFF when complete

### Display Information

```
┌────────────────────────────────┐
│ Baross utca           14:23    │  ← Stop name and current time
├────────────────────────────────┤
│ 35 - Csepel, Csillagt...   5'  │  ← Route + Destination | Minutes
│ 148 - Csepel, Sorok...    12'  │
│ 224 - Boráros tér H       18'  │
└────────────────────────────────┘
```

**Display elements:**
- **Top left**: Stop name
- **Top right**: Current time (HH:MM format)
- **Lines 2-4**: Next 3 departures
  - Route number
  - Destination (auto-truncated if too long)
  - Minutes until arrival (or "-" if departing)

If no buses are found within 60 minutes:
```
Nem található indulás
60 percen belül.
```

## API Reference

### Endpoint Structure

```
GET {baseUrl}?stopId={stopId}&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60&key={apiKey}
```

**Parameters:**
- `stopId`: Unique identifier for the stop
- `onlyDepartures`: Only show departing vehicles
- `limit`: Maximum number of results (10)
- `minutesBefore`: Look back time (0 = only future departures)
- `minutesAfter`: Look ahead time (60 minutes)
- `key`: Your API key

### JSON Response Structure

The API returns a complex JSON with nested references:

```json
{
  "currentTime": 1763493705506,
  "data": {
    "entry": {
      "stopId": "BKK_F04144",
      "stopTimes": [
        {
          "tripId": "BKK_D062532447",
          "stopHeadsign": "Boráros tér H",
          "predictedArrivalTime": 1763493725,
          "arrivalTime": 1763493600
        }
      ]
    },
    "references": {
      "routes": {
        "BKK_2240": {"shortName": "224"}
      },
      "trips": {
        "BKK_D062532447": {"routeId": "BKK_2240"}
      }
    }
  }
}
```

**Full API documentation:**
- BKK: [https://opendata.bkk.hu/](https://opendata.bkk.hu/)

## Customization

### Changing Hardware Pins

If using a different ESP32 board, modify these definitions:

```cpp
// OLED Display Configuration
#define OLED_SDA 4      // Your SDA pin
#define OLED_SCL 15     // Your SCL pin
#define OLED_RST 16     // Your RST pin
#define OLED_ADDRESS 0x3c

// Other Hardware
#define LED_PIN 25      // Your LED pin (optional)
#define BUTTON_PIN 0    // Your button pin
```

### Adjusting Update Interval

```cpp
#define UPDATE_INTERVAL_MS 2000  // Time between API queries (milliseconds)
```

### Changing Number of Displayed Departures

```cpp
#define MAX_DEPARTURES 3  // Number shown on display (max 3 recommended)
```

### Display Fonts

The code uses fonts from the OLED library. Available fonts:
http://oleddisplay.squix.ch/

Current fonts used:
- `ArialMT_Plain_24` - Boot screen title
- `ArialMT_Plain_16` - Headers and connecting message
- `ArialMT_Plain_10` - Departure list and time

## Troubleshooting

### WiFi Connection Issues
- Verify SSID and password are correct
- Check WiFi signal strength at device location
- Ensure 2.4 GHz WiFi is enabled (ESP32 doesn't support 5 GHz)

### Display Shows "Nem található indulás"
- Verify stop ID is correct
- Check if stop has any scheduled services
- Try increasing `minutesAfter` parameter to 120
- Confirm API key is valid

### Wrong Time Displayed
- Adjust the `summerTime` variable based on current season
- Time is calculated from API's Unix epoch timestamp

### Compilation Errors
- Ensure all required libraries are installed
- Check Arduino IDE board selection matches your hardware
- Verify ESP32 board support package is up to date

### Upload Fails
- Hold down the BOOT button during upload (some boards)
- Try reducing upload speed (Tools → Upload Speed → 115200)
- Check USB cable supports data transfer (not charge-only)

## Memory Considerations

Typical memory usage for Heltec WiFi LoRa 32 V2:

```
Sketch uses 891,865 bytes (26%) of program storage space. Maximum is 3,342,336 bytes.
Global variables use 41,704 bytes (12%) of dynamic memory, leaving 285,976 bytes for local variables.
```

The code is optimized for ESP32's resources and should run comfortably with room for expansion.

## Development History

This project was originally developed in 2018-2019 during my university studies in Budapest, Hungary. At that time, I had limited experience with professional coding practices and the code reflected that learning phase.

In 2025, I revisited and completely refactored the codebase to meet modern software engineering standards while maintaining full backward compatibility. The functionality remains identical, but the code is now:
- More maintainable and readable
- Better documented
- Properly structured with clear separation of concerns
- Following consistent naming conventions

The FUTÁR system has since expanded beyond Budapest, making this project useful for multiple Hungarian cities.

## Future Development Possibilities

- 🔧 **Web-based configuration** - Set up WiFi and stops via captive portal
- 📱 **Mobile app integration** - Configure device from smartphone
- ⏰ **Departure alerts** - Buzzer notification for specific routes
- 📊 **Statistics tracking** - Log delays and service patterns
- 🌍 **Multi-language support** - English/Hungarian display options
- 🔋 **Battery operation** - Deep sleep mode for portable use

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for:
- Bug fixes
- New features
- Documentation improvements
- Support for additional cities using FUTÁR

## Acknowledgments

- **BKK (Budapesti Közlekedési Központ)** - For providing the open FUTÁR API
- **György Balássy** - His blog posts on BKK FUTÁR API integration were invaluable during initial development: https://balassygyorgy.wordpress.com/2016/02/02/bkk-futar-microsoft-bandre-2-bkk-futar-api/
- **Heltec Automation** - For the excellent development board
- **ThingPulse** - For the SSD1306 OLED library

## Author

**Balazs Markus**

- GitHub: [@bazsimarkus](https://github.com/bazsimarkus)
- Project Link: [https://github.com/bazsimarkus/e-futar](https://github.com/bazsimarkus/e-futar)

---

Made with ❤️ in Budapest, Hungary
