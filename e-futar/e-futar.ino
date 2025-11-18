/*
 * E-FUTAR
 * Embedded BKK FUTAR application for ESP32 with SSD1306 OLED display
 * Can be used for the cities of Budapest, Hungary and Tatabanya, Hungary
 * 
 * Written by Balazs Markus
 * 
 * Displays upcoming bus departures and current time on an OLED display.
 * Multiple stops supported - switch between them using the built-in button.
 * Built-in LED indicates loading status.
 * 
 * WiFi and API key configuration required before uploading!
 * See the configuration section below to customize stops and settings.
 */

//*****************************************************************************
// Includes
//*****************************************************************************

#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`

//*****************************************************************************
// USER CONFIGURATION - Modify these settings before uploading
//*****************************************************************************

// WiFi Settings
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// FUTAR API Configuration
const char apiKey[256] = "YOUR_API_KEY"; // Add your API key here
const char baseUrl[] = "https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json"; // For the city of Tatabanya, use "https://go.tbusz.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json"

// Time Settings
bool summerTime = false; // true in summer, false in winter - adds an hour to the queried UNIX time

// You don't have to modify this, this is required for the bus stop list definition
struct Stop { 
    const char* stopId;
    const char* stopName;
};

// Here you can add your bus stops - you can switch between them using the built-in button. The first parameter is the stop ID, and the second parameter is the name of the bus stop.
// For the city of Budapest, the stop IDs usualy look like "BKK_F04144", and for the city of Tatabanya, the stop IDs usually look like "tbusz_SP17210".
const Stop busStopList[] = { 
    {"BKK_F04144", "Baross utca"},
    {"BKK_F04126", "János utca"},
    {"BKK_F04122", "Városközpont"}
};

//*****************************************************************************
// Hardware Configuration
//*****************************************************************************

#define BAUD_RATE 115200        // Serial connection speed (for debugging if needed)
#define LED_PIN 25              // Built-in LED indicator
#define BUTTON_PIN 0            // Built-in button for stop switching

// OLED Display Configuration
// OLED pins to ESP32 GPIOs via this connection:
// OLED_SDA -- GPIO4
// OLED_SCL -- GPIO15
// OLED_RST -- GPIO16
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define OLED_ADDRESS 0x3c

//*****************************************************************************
// Application Constants
//*****************************************************************************

#define UPDATE_INTERVAL_MS 2000  // Delay between API queries (milliseconds)
#define MAX_DEPARTURES 3         // Number of departures to show on display
#define TOTAL_DEPARTURES 10      // Total departures to fetch from API

//*****************************************************************************
// Data Structures
//*****************************************************************************

struct StopTime {
    char shortName[16];              // Route number (e.g., "35", "148")
    char stopHeadsign[32];           // Destination name
    char displayText[48];            // Combined format: "35 - Destination"
    long arrivalTime;                // Predicted/scheduled arrival in seconds (Unix epoch)
    int arrivalMinutes;              // Minutes until arrival
    char arrivalMinutesDisplay[4];  // Formatted for display (e.g., "5'")
};

//*****************************************************************************
// Global Variables
//*****************************************************************************

const int numStops = sizeof(busStopList) / sizeof(busStopList[0]);
int currentStopIndex = 0;

// Departure data
StopTime departures[TOTAL_DEPARTURES];
int numDepartures = 0;

// Time tracking
long currentTime = 0;
char clockDisplay[6];

// Network & Display
HTTPClient http;
SSD1306 display(OLED_ADDRESS, OLED_SDA, OLED_SCL);

//*****************************************************************************
// Display Functions
//*****************************************************************************

/**
 * @brief Initializes the SSD1306 OLED display.
 * 
 * Sets up GPIO16 for display reset, initializes the display controller,
 * and configures default font and orientation.
 */
void initDisplay() {
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);    // Set GPIO16 low to reset OLED
    delay(50);
    digitalWrite(OLED_RST, HIGH);   // While OLED is running, must set GPIO16 high
    
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
}

/**
 * @brief Displays the initial splash screen during WiFi connection.
 * 
 * Shows the application name, connection status, and author information.
 */
void showConnectingSplash() {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 0, "E-FUTÁR");
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 28, "Csatlakozás");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 52, "Írta: Márkus Balázs");
    display.display();
}

/**
 * @brief Animates connecting dots on the splash screen.
 * 
 * @param dotX Reference to the X position of the next dot to draw.
 *             Automatically wraps and redraws splash when reaching screen edge.
 * 
 * Creates a loading animation by drawing dots that move across the screen
 * while attempting to connect to WiFi.
 */
void animateConnecting(int& dotX) {
    display.setFont(ArialMT_Plain_16);
    display.drawString(dotX, 28, ".");
    display.display();
    dotX += 4;
    
    if (dotX > 126) {
        dotX = 88;
        showConnectingSplash();
    }
}

/**
 * @brief Draws the complete departure information to the OLED display.
 * 
 * Displays:
 * - Stop name (top left) and current time (top right)
 * - Up to 3 upcoming departures with route numbers, destinations, and arrival times
 * - "No departure found" message if no buses are scheduled within 60 minutes
 * 
 * Additional fonts available at: http://oleddisplay.squix.ch/
 */
void displayDepartures() {
    display.clear();
    
    // Header: Stop name and current time
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, busStopList[currentStopIndex].stopName);
    
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(128, 0, clockDisplay);
    
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    
    // Departures or "no departures" message
    if (numDepartures == 0) {
        display.drawString(0, 20, "Nem található indulás");
        display.drawString(0, 34, "60 percen belül.");
    } else {
        // Display up to MAX_DEPARTURES buses
        int displayCount = min(numDepartures, MAX_DEPARTURES);
        
        for (int i = 0; i < displayCount; i++) {
            int yPos = 20 + (i * 14);
            display.drawString(0, yPos, departures[i].displayText);
            
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, yPos, departures[i].arrivalMinutesDisplay);
            display.setTextAlignment(TEXT_ALIGN_LEFT);
        }
    }
    
    display.display();
}

//*****************************************************************************
// Network Functions
//*****************************************************************************

/**
 * @brief Connects to the configured WiFi network.
 * 
 * Attempts to connect to WiFi using the credentials defined in WIFI_SSID
 * and WIFI_PASSWORD. Displays an animated splash screen while connecting.
 * The built-in LED is turned off during connection attempts.
 */
void connectToWiFi() {
    digitalWrite(LED_PIN, LOW);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    showConnectingSplash();
    
    // Animate dots until connected
    int dotX = 88;
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        animateConnecting(dotX);
    }
}

/**
 * @brief Fetches departure data from the BKK FUTAR API.
 * 
 * @return bool Returns true if data was successfully fetched and parsed, false otherwise.
 * 
 * Constructs the API URL with current stop ID and parameters, sends HTTP GET request,
 * and passes the response to the JSON parser. If WiFi connection is lost, attempts
 * to reconnect automatically.
 */
bool fetchDepartureData() {
    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
        return false;
    }
    
    // Build the complete API URL with parameters
    char url[512];
    snprintf(url, sizeof(url), 
             "%s?stopId=%s&onlyDepartures=onlyDepartures&limit=%d&minutesBefore=0&minutesAfter=60&key=%s",
             baseUrl, busStopList[currentStopIndex].stopId, TOTAL_DEPARTURES, apiKey);
    
    if (!http.begin(url)) {
        return false;
    }
    
    int httpCode = http.GET();
    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        success = parseApiResponse(payload.c_str());
    }
    
    http.end();
    return success;
}

//*****************************************************************************
// Data Processing Functions
//*****************************************************************************

/**
 * @brief Clears all departure data in the global array.
 * 
 * Resets all fields in the departures array to empty/zero values,
 * preparing it for new data from the API.
 */
void clearDepartureData() {
    for (int i = 0; i < TOTAL_DEPARTURES; i++) {
        departures[i].shortName[0] = '\0';
        departures[i].stopHeadsign[0] = '\0';
        departures[i].displayText[0] = '\0';
        departures[i].arrivalTime = 0;
        departures[i].arrivalMinutes = 0;
        departures[i].arrivalMinutesDisplay[0] = '\0';
    }
    numDepartures = 0;
}

/**
 * @brief Formats arrival time in minutes as a display string.
 * 
 * @param minutes The number of minutes until arrival.
 * @param buffer Character array to store the formatted string (minimum 4 bytes).
 * 
 * Formats the minutes into a display string with an apostrophe:
 * - Less than 1 minute: "-"
 * - 1-9 minutes: "5'" 
 * - 10+ minutes: "15'"
 */
void formatArrivalMinutes(int minutes, char* buffer) {
    if (minutes < 1) {
        strcpy(buffer, "-");
    } else if (minutes < 10) {
        sprintf(buffer, "%d'", minutes);
    } else {
        sprintf(buffer, "%d'", minutes);
    }
}

/**
 * @brief Updates the current time and formats it for display.
 * 
 * @param apiCurrentTime Unix epoch time in seconds received from the API.
 * 
 * Converts Unix epoch time to hours and minutes in 24-hour format.
 * Applies timezone adjustment based on the summerTime setting:
 * - Summer time (DST): Uses time as-is
 * - Winter time: Adds 1 hour to the queried time
 */
void updateCurrentTime(long apiCurrentTime) {
    currentTime = apiCurrentTime;
    
    long hours, minutes;
    if (summerTime) {
        hours = (currentTime % 86400) / 3600;
        minutes = (currentTime % 3600) / 60;
    } else {
        // Add 1 hour for winter time, wrap at 24 hours
        hours = ((currentTime % 86400) / 3600 + 1) % 24;
        minutes = (currentTime % 3600) / 60;
    }
    
    sprintf(clockDisplay, "%ld:%02ld", hours, minutes);
}

/**
 * @brief Truncates the display text to fit within display width constraints.
 * 
 * @param displayText The full text string to potentially truncate.
 * @param shortNameLength Length of the route number (to calculate available space).
 * 
 * Ensures the departure minute display on the right side is always readable by
 * truncating long destination names. The maximum length is calculated based on:
 * - Display width: 128 pixels
 * - Right-side reserved for minutes: ~20 pixels (3 characters)
 * - Character width: ~6 pixels for ArialMT_Plain_10
 * - Available characters: ~26, minus route number length and " - " separator
 */
void truncateDisplayText(char* displayText, int shortNameLength) {
    // Reserve space for: route number + " - " (3 chars) + minutes display on right (3 chars + margin)
    // Maximum total characters that fit: ~26 characters
    const int maxDisplayLength = 26;
    int maxLength = maxDisplayLength;
    
    // Ensure text doesn't overflow
    if (strlen(displayText) > maxLength) {
        displayText[maxLength] = '\0';
    }
}

/**
 * @brief Parses the JSON response from the BKK FUTAR API.
 * 
 * @param jsonString The JSON string received from the API.
 * @return bool Returns true if parsing was successful, false otherwise.
 * 
 * Extracts the following information from the API response:
 * - Current time from the server
 * - List of upcoming departures (stopTimes array)
 * - For each departure: route number, destination, predicted/scheduled arrival time
 * 
 * The function handles both predictedArrivalTime (when GPS is available) and
 * arrivalTime (scheduled time) fields. Route information is extracted from the
 * references section using tripId and routeId lookups.
 * 
 * At night or off-peak times, the stopTimes array may contain fewer than 10 buses.
 * The function adapts to the actual array size to prevent accessing invalid data.
 * 
 * Long destination names are automatically truncated to prevent overlap with the
 * arrival time display on the right side of the screen.
 */
bool parseApiResponse(const char* jsonString) {
    const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 
                               3*JSON_OBJECT_SIZE(4) + 2*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(7) + 
                               JSON_OBJECT_SIZE(8) + JSON_OBJECT_SIZE(9) + JSON_OBJECT_SIZE(10) + 
                               JSON_OBJECT_SIZE(12) + 2*JSON_OBJECT_SIZE(13) + JSON_OBJECT_SIZE(14) + 
                               JSON_OBJECT_SIZE(16) + JSON_OBJECT_SIZE(17) + 670;
    
    DynamicJsonDocument doc(bufferSize);
    
    if (deserializeJson(doc, jsonString)) {
        return false;
    }
    
    JsonObject root = doc.as<JsonObject>();
    
    // Extract current time (first 10 characters = seconds, remaining = milliseconds)
    long apiCurrentTime = root["currentTime"].as<String>().substring(0, 10).toInt();
    updateCurrentTime(apiCurrentTime);
    
    // Extract stop times array - size varies depending on time of day
    JsonArray stopTimes = root["data"]["entry"]["stopTimes"].as<JsonArray>();
    numDepartures = min((int)stopTimes.size(), TOTAL_DEPARTURES);
    
    for (int i = 0; i < numDepartures; i++) {
        JsonObject stopTime = stopTimes[i];
        
        // Get arrival time - prefer predicted (GPS) over scheduled
        long arrivalTime;
        if (stopTime.containsKey("predictedArrivalTime")) {
            // Bus has active GPS connection
            arrivalTime = stopTime["predictedArrivalTime"].as<String>().substring(0, 10).toInt();
        } else if (stopTime.containsKey("arrivalTime")) {
            // Scheduled arrival time only
            arrivalTime = stopTime["arrivalTime"].as<String>().substring(0, 10).toInt();
        } else {
            continue; // Skip this entry if no time information available
        }
        
        departures[i].arrivalTime = arrivalTime;
        departures[i].arrivalMinutes = (arrivalTime - currentTime) / 60;
        
        // Get destination
        strncpy(departures[i].stopHeadsign, stopTime["stopHeadsign"], sizeof(departures[i].stopHeadsign) - 1);
        
        // Get route short name from references section
        // Path: stopTimes[i].tripId -> trips[tripId].routeId -> routes[routeId].shortName
        const char* tripId = stopTime["tripId"];
        const char* routeId = root["data"]["references"]["trips"][tripId]["routeId"];
        const char* shortName = root["data"]["references"]["routes"][routeId]["shortName"];
        strncpy(departures[i].shortName, shortName, sizeof(departures[i].shortName) - 1);
        
        // Format display text: "35 - Destination"
        snprintf(departures[i].displayText, sizeof(departures[i].displayText), 
                "%s - %s", departures[i].shortName, departures[i].stopHeadsign);
        
        // Truncate if too long to prevent overlap with arrival minutes on the right
        truncateDisplayText(departures[i].displayText, strlen(departures[i].shortName));
        
        // Format arrival minutes for display
        formatArrivalMinutes(departures[i].arrivalMinutes, departures[i].arrivalMinutesDisplay);
    }
    
    // Turn off LED indicator when data is successfully loaded
    digitalWrite(LED_PIN, LOW);
    return true;
}

//*****************************************************************************
// Interrupt Handler
//*****************************************************************************

/**
 * @brief Interrupt callback function for the stop change button.
 * 
 * Changes to the next bus stop in the busStopList array when the button is pressed.
 * Wraps around to the first stop after reaching the last one.
 * The LED is turned on to indicate that new data is being loaded.
 */
void IRAM_ATTR handleStopChange() {
    digitalWrite(LED_PIN, HIGH);
    currentStopIndex = (currentStopIndex + 1) % numStops;
}

//*****************************************************************************
// Setup & Loop
//*****************************************************************************

/**
 * @brief Sets up initial configurations, pins, and interrupts.
 * 
 * Initializes:
 * - GPIO pins for button input and LED output
 * - OLED display
 * - WiFi connection
 * - Interrupt for stop change button
 */
void setup() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    
    initDisplay();
    connectToWiFi();
    
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleStopChange, FALLING);
}

/**
 * @brief Main program loop.
 * 
 * Continuously:
 * 1. Clears old departure data
 * 2. Fetches new data from the API for the current stop
 * 3. Updates the display with the new information
 * 4. Waits for UPDATE_INTERVAL_MS before repeating
 * 
 * The loop runs indefinitely, keeping the display updated with current bus times.
 */
void loop() {
    clearDepartureData();
    fetchDepartureData();
    displayDepartures();
    delay(UPDATE_INTERVAL_MS);
}