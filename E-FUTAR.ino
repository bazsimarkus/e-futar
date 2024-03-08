/*
 * E-FUTAR
 * Embedded BKK FUTAR application port for ESP32 SoC with SSD1306 OLED display
 * 
 * Written by Balazs Markus
 * 
 * With the help of the program, the microcontroller displays the next buses departing from a given bus stop and the remaining time until departure, as well as the current time on an OLED display.
 * The program can handle several stops, you can switch between them with the built-in button of the development card, the loading status is indicated by the built-in white SMD LED.
 * 
 * WiFi and API key configuration required before uploading!
 * If you want to add other stops, the list of JSON files to be parsed must be modified!
 * 
 * See the ReadMe file for more information
 * 
 */

//*****************************************************************************
// @includes
//*****************************************************************************

#include <ArduinoJson.h>
#include <WiFi.h>
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include <HTTPClient.h>

//*****************************************************************************
// @defines
//*****************************************************************************

#define BAUD_RATE 115200 // serial connection speed

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define LED_OUTPUT_PIN 25

//*****************************************************************************
// @globals
//*****************************************************************************

const char apiKey[256] = "YOUR_API_KEY";

const char baseUrl[] = "https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json"; // define the base URL

bool summerTime = false; //true in summer, false in winter, in the second case it adds an hour to the queried UNIX time

struct busStop {
    const char* stopID;
    const char* stopName;
};

// Bus stop database
const busStop busStopList[] = {
    {"BKK_F04144", "Baross utca"},
    {"BKK_F04126", "János utca"},
    {"BKK_F04122", "Városközpont"}
};

int currentBusStopIndex = 0; // default bus stop

//The BusData structure is the basic data storage unit, a structure stores the data of a bus, and 10 such structures form a bus list of 10 (see bus list array)
struct BusData {
    char shortName[16]; //Line number
    char stopHeadsign[32]; //Name of the destination
    char stopHeadsignWithShortName[32]; //The line number + the name of the destination together, as posted on the buses, required for the display
    char predictedArrivalTime[32]; //Queryed predicted or scheduled arrival time (depending on whether there is an active GPS connection on the bus) in milliseconds from EPOCH time
    long predictedArrivalTimeLong; //Queryed predicted or scheduled arrival time (depending on whether there is an active GPS connection on the bus) in milliseconds from EPOCH time, in long type
    int predictedArrivalMinutesInt; //Arrival time in minutes
    char predictedArrivalMinutesString[3]; //Arrival time in minutes as a string, with an apostrophe concatenated at the end
};

struct BusData busList[10]; // List of arriving buses

char currentTime[32];
long currentTimeLong,currentTimeHours,currentTimeMinutes;
char clockTimeString[5];
uint16_t maxArraySize=0, ArraySize=0;

// HTTP CLIENT
HTTPClient http;
char resource[512];                    // http resource (the complete URL)

// OLED DISPLAY
//OLED pins to ESP32 GPIOs via this connecting:
//OLED_SDA -- GPIO4
//OLED_SCL -- GPIO15
//OLED_RST -- GPIO16
SSD1306  display(0x3c, 4, 15); //Init OLED display


//*****************************************************************************
// @functions
//*****************************************************************************

/**
 * @brief Sets up initial configurations, pins, and interrupts.
 * 
 * This function initializes the necessary configurations for the program to run properly,
 * including setting up pins, serial communication, and interrupts for button presses.
 */
void setup() {
    Serial.begin(115200);

    // Bus stop change interrupt button
    pinMode(0, INPUT_PULLUP);
    pinMode(LED_OUTPUT_PIN, OUTPUT); // The busy LED indicator on the development board
    
    setupDisplay(); // Setup SSD1306 OLED

    connectToWiFiSplashScreen();
    attachInterrupt(digitalPinToInterrupt(0), changeCurrentStop, FALLING);
}

/**
 * @brief The main loop function.
 * 
 * This function is the main loop of the program, where it continuously executes the necessary tasks
 * such as fetching bus data, updating the display, and handling interruptions.
 */
void loop() {
    clearBusList();
    sprintf(resource, "%s?stopId=%s&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60&key=%s", baseUrl, busStopList[currentBusStopIndex].stopID, apiKey);
    httpGetBusData(resource);
    drawBusListToDisplay();
    delay(2000);     // a little delay so that it doesn't do quieries too often
}

// -------- INTERRUPT CALLBACKS --------

/**
 * @brief Interrupt callback function, changes the HTTP resource for the next bus stop.
 * 
 * This function is called when the interrupt button is pressed, and it changes the HTTP resource
 * to fetch bus data for the next bus stop in the list.
 */
void changeCurrentStop() {
    digitalWrite(LED_OUTPUT_PIN, HIGH);
    int numStops = sizeof(busStopList) / sizeof(busStopList[0]);
    if(currentBusStopIndex < (numStops - 1)) currentBusStopIndex += 1;
    else currentBusStopIndex = 0;
}

// -------- NETWORK FUNCTIONS --------

/**
 * @brief Fetches bus data from the API using HTTP GET request.
 * 
 * @param inputResource The HTTP resource to fetch bus data.
 * 
 * This function sends an HTTP GET request to the provided resource to fetch bus data
 * for the current bus stop, then calls the JSON parsing function.
 */
void httpGetBusData(char* inputResource){
    if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[HTTP] begin...\n");
    if (http.begin(inputResource)) {
      Serial.print("[HTTP] GET...\n");
      int httpCode = http.GET();
      
      if (httpCode > 0) {
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          parseReponseContent(payload.c_str());
        }
      } else {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();
    } else {
      Serial.printf("[HTTP} Unable to connect\n");
    }
  } else {
    Serial.println("WiFi Disconnected");
    connectToWiFiSplashScreen();
  }
}

// -------- DATA MANIPULATION AND PARSING FUNCTIONS --------

/**
 * @brief Converts seconds to minutes.
 * 
 * @param secondsLong The time in seconds.
 * @return int The time converted to minutes.
 * 
 * This function converts the provided time in seconds to minutes and returns the result, for us to be able to copy it to predictedArrivalMinutesInt.
 */
int secondsToMinutes(long secondsLong) {
    long minutesLong;
    int minutesInt;
    minutesLong = secondsLong/60;
    minutesInt = (int)minutesLong;
    return minutesInt;
}

/**
 * @brief Converts arrival minutes to a string representation.
 * 
 * @param arrivalMinutes The arrival time in minutes.
 * @param arrivalString Pointer to a character array to store the resulting string.
 * 
 * This function converts the provided arrival time in minutes to a string representation
 * with an apostrophe concatenated at the end and stores it in the specified character array.
 */
void arrivalMinutesToString(int arrivalMinutes, char* arrivalString) {
    if(arrivalMinutes < 1) {
        for(int k=0; k<3; k++) arrivalString[k] = ' '; // it must be cleared, so that if anything is left out of the previous cycle, it will be deleted
        arrivalString[0]= '-';
    }
    else {
        if(arrivalMinutes<10) {
            for(int k=0; k<3; k++) arrivalString[k] = ' '; // it must be cleared, so that if anything is left out of the previous cycle, it will be deleted
            arrivalString[0]=arrivalMinutes + '0';
            arrivalString[1]= '\'';
            arrivalString[2]= '\0'; // we shorten the string because the apostrophes should be in a line on the right side
        }
        else {
            for(int k=0; k<3; k++) arrivalString[k] = ' '; // it must be cleared, so that if anything is left out of the previous cycle, it will be deleted
            arrivalString[0]=(arrivalMinutes/10) + '0';
            arrivalString[1]=(arrivalMinutes%10) + '0';
            arrivalString[2]= '\'';
        }
    }
}

/**
 * @brief Converts Unix epoch time (1709905849875) to human-readable hours and minutes.
 * 
 * This function converts the Unix epoch time (in milliseconds) retrieved from the API
 * into human-readable hours and minutes in 24-hour format (14:50) and saves it into clockTimeString[5]
 * for displaying it on the OLED display.
 */
void convertUnixTimeToHoursMinutesString() {
    if(summerTime==true) {
        currentTimeHours = (currentTimeLong % 86400) / 3600;
        currentTimeMinutes = (currentTimeLong % 3600) / 60;
    }
    else {
        currentTimeHours = ((((currentTimeLong % 86400) / 3600)+1)%24); // we need mod24 because it showed 24:05 at night due to winter time
        currentTimeMinutes = (currentTimeLong % 3600) / 60;
    }
    if(currentTimeHours<10) {
        for(int k=0; k<4; k++) clockTimeString[k] = ' '; // must be cleared before it changes from two digits to one digits
        clockTimeString[0] = currentTimeHours + '0'; // conversion to char
        clockTimeString[1] = ':';
        clockTimeString[2] = (currentTimeMinutes/10) + '0';
        clockTimeString[3] = (currentTimeMinutes%10) + '0';
    }
    else {
        for(int k=0; k<4; k++) clockTimeString[k] = ' '; // it must be cleared, so that if anything is left out of the previous cycle, it will be deleted
        clockTimeString[0] = (currentTimeHours/10) + '0'; // conversion to char
        clockTimeString[1] = (currentTimeHours%10) + '0';
        clockTimeString[2] = ':';
        clockTimeString[3] = (currentTimeMinutes/10) + '0';
        clockTimeString[4] = (currentTimeMinutes%10) + '0';
    }
}

/**
 * @brief Clears the bus list array.
 * 
 * This function clears the bus list array to prepare it for storing new bus data.
 */
void clearBusList() {
    //First we reset the bus list and then copy the appropriate number of departing buses that we defined in MaxArraySize, the rest remain zero
    for(int t=0; t<10; t++) {
        strcpy(busList[t].shortName," ");
        busList[t].stopHeadsign[0] = '\0';
        busList[t].stopHeadsignWithShortName[0] = '\0';
        busList[t].predictedArrivalTime[0] = '\0';
        busList[t].predictedArrivalTimeLong = 0;
        busList[t].predictedArrivalMinutesInt = 0;
        busList[t].predictedArrivalMinutesString[0] = '\0';
    }
}

/**
 * @brief Parses the response content received from the API.
 * 
 * @param jsonString The JSON string response from the API.
 * @return bool Returns true if parsing is successful, false otherwise.
 * 
 * This function parses the JSON response received from the API and populates
 * the bus list in the busList array with the retrieved bus data.
 */
bool parseReponseContent(const char* jsonString) {
    const size_t BUFFER_SIZE = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 3*JSON_OBJECT_SIZE(4) + 2*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(8) + JSON_OBJECT_SIZE(9) + JSON_OBJECT_SIZE(10) + JSON_OBJECT_SIZE(12) + 2*JSON_OBJECT_SIZE(13) + JSON_OBJECT_SIZE(14) + JSON_OBJECT_SIZE(16) + JSON_OBJECT_SIZE(17) + 670;

    DynamicJsonDocument jsonDocument(BUFFER_SIZE);

    DeserializationError error = deserializeJson(jsonDocument, jsonString);

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
        return false;
    }

    JsonObject root = jsonDocument.as<JsonObject>();
    // at night, the "stoptimes" doesn't always contain 10 buses, so I have to cast it into arrays and then scan the size
    JsonArray nestedArray = root["data"]["entry"]["stopTimes"].as<JsonArray>();

    // at night, the "stoptimes" doesn't always contain 10 buses, only those that depart within half an hour, so we need to look at the size of the array, because if we refer to something that isn't there, it throws a Guru CPU Error
    Serial.print("Size of stopTimes: ");
    Serial.println(nestedArray.size());

    ArraySize=nestedArray.size();

    if(ArraySize<3) {
        maxArraySize = ArraySize;
    }
    else {
        maxArraySize=3;
    }

    String currentTimeString = root["currentTime"];
    strncpy(currentTime,currentTimeString.c_str(),10);
    currentTimeLong = atol(currentTime);
    convertUnixTimeToHoursMinutesString();

    for(int i=0; i<maxArraySize; i++) {
        JsonObject actualBus = root["data"]["entry"]["stopTimes"][i];

        if (actualBus.containsKey("predictedArrivalTime"))
        {
            Serial.println("Predicted");
            // they are in milliseconds, which can only be stored in long long, but we can't do that so we cut off the first 10 numbers (with names the first 25 characters), to fit the display for sure so we get a resolution of one seconds, so that we can store the times in plain long
            strncpy(busList[i].stopHeadsign, root["data"]["entry"]["stopTimes"][i]["stopHeadsign"],20
                   );
            String predictedArrivalTimeString = root["data"]["entry"]["stopTimes"][i]["predictedArrivalTime"];
            strncpy(busList[i].predictedArrivalTime, predictedArrivalTimeString.c_str(),10);
            busList[i].predictedArrivalTimeLong = atol(busList[i].predictedArrivalTime);
            busList[i].predictedArrivalMinutesInt = secondsToMinutes(busList[i].predictedArrivalTimeLong-currentTimeLong);  // subtract the current time and then convert it from second to minute
            arrivalMinutesToString(busList[i].predictedArrivalMinutesInt,busList[i].predictedArrivalMinutesString);

            char tripId[32];
            char routeId[32];
            char shortName[16];
            strcpy(tripId,root["data"]["entry"]["stopTimes"][i]["tripId"]);
            strcpy(routeId,root["data"]["references"]["trips"][tripId]["routeId"]);
            strcpy(shortName,root["data"]["references"]["routes"][routeId]["shortName"]);
            strcpy(busList[i].shortName,shortName);
            strcpy(busList[i].stopHeadsignWithShortName,busList[i].shortName);
            strcat(busList[i].stopHeadsignWithShortName," - ");
            strcat(busList[i].stopHeadsignWithShortName,busList[i].stopHeadsign);
            Serial.print("tripId=");
            Serial.println(tripId);
            Serial.print("shortName=");
            Serial.println(shortName);
            Serial.print("routeId=");
            Serial.println(routeId);
            Serial.print("shortName=");
            Serial.println(shortName);

            // the LED will only turn off if we have been able to overwrite it successfully
            digitalWrite(LED_OUTPUT_PIN, LOW);
        }
        else if(actualBus.containsKey("arrivalTime")) {
            // predticted time is NOT valid
            Serial.println("Arrival");
            strncpy(busList[i].stopHeadsign, root["data"]["entry"]["stopTimes"][i]["stopHeadsign"],20);
            String arrivalTimeString = root["data"]["entry"]["stopTimes"][i]["arrivalTime"];
            strncpy(busList[i].predictedArrivalTime, arrivalTimeString.c_str(),10);
            busList[i].predictedArrivalTimeLong = atol(busList[i].predictedArrivalTime); // cast from string to long
            busList[i].predictedArrivalMinutesInt = secondsToMinutes(busList[i].predictedArrivalTimeLong-currentTimeLong);  // subtract the current time and then convert it from second to minute
            arrivalMinutesToString(busList[i].predictedArrivalMinutesInt,busList[i].predictedArrivalMinutesString);

            char tripId[32];
            char routeId[32];
            char shortName[16];
            strcpy(tripId,root["data"]["entry"]["stopTimes"][i]["tripId"]);
            strcpy(routeId,root["data"]["references"]["trips"][tripId]["routeId"]);
            strcpy(shortName,root["data"]["references"]["routes"][routeId]["shortName"]);
            strcpy(busList[i].shortName,shortName);
            strcpy(busList[i].stopHeadsignWithShortName,busList[i].shortName);
            strcat(busList[i].stopHeadsignWithShortName," - ");
            strcat(busList[i].stopHeadsignWithShortName,busList[i].stopHeadsign);
            Serial.print("tripId=");
            Serial.println(tripId);
            Serial.print("shortName=");
            Serial.println(shortName);
            Serial.print("routeId=");
            Serial.println(routeId);
            Serial.print("shortName=");
            Serial.println(shortName);

            // the LED will only turn off if we have been able to overwrite it successfully
            digitalWrite(LED_OUTPUT_PIN, LOW);
        }
        else {
            Serial.println("noInfo");
        }
    }
    if(maxArraySize==0) {
        // if no start is found in the next 60 minutes then the LED will not go off so let's just turn it off
        digitalWrite(LED_OUTPUT_PIN, LOW);
    }
    return true;
}


// -------- DISPLAY AND WIFI FUNCTIONS --------

/**
 * @brief Initializes the OLED display.
 * 
 * This function initializes the OLED display with the appropriate settings.
 */
void setupDisplay() {
    pinMode(16,OUTPUT);
    digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
    delay(50);
    digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high
    // Initializing the UI will init the display too.
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
}

/**
 * @brief Connects to WiFi and displays a splash screen while connecting.
 * 
 * This function connects to the configured WiFi network and displays a splash screen
 * on the OLED display while attempting to connect.
 */
void connectToWiFiSplashScreen() {
  digitalWrite(LED_OUTPUT_PIN, LOW);
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 0, "E-FUTÁR");
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 28, "Csatlakozás"); // "Connecting"
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 52, "Írta: Márkus Balázs"); // "Written by Balazs Markus"
  display.display();

  //On the BOOT screen, the dots are animated, just like in a Serial message, until you are connected to Wi-Fi
  int x = 88;
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.println("Attempting to connect to WiFi...");
    display.setFont(ArialMT_Plain_16);
    display.drawString(x, 28, ".");
    display.display();
    x=x+4;
    if(x>126) {
        x=88;
        //redraw the display when the dots reach the edge
        display.clear();
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.setFont(ArialMT_Plain_24);
        display.drawString(0, 0, "E-FUTÁR");
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 28, "Csatlakozás"); // "Connecting"
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 52, "Írta: Márkus Balázs"); // "Written by Balazs Markus"
        display.display();
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi successfully");
  } else {
    Serial.println("Failed to connect to WiFi");
  }
}

/**
 * @brief Draws the contents of the bus list array to the display.
 * 
 * This function draws the bus data for the first three buses from the bus list array onto the OLED display.
 */
void drawBusListToDisplay() {
    // Printing the list to the screen
    // Additional fonts are available at http://oleddisplay.squix.ch/
    display.clear();

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, busStopList[currentBusStopIndex].stopName);
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(128, 0, clockTimeString);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    if(busList[0].stopHeadsign[0]=='\0') {
        display.drawString(0, 20, "Nem található indulás"); // "No departure found"
        display.drawString(0, 34, "60 percen belül."); // "in 60 minutes"
    }
    else {
        display.drawString(0, 20, busList[0].stopHeadsignWithShortName);
        display.drawString(0, 34, busList[1].stopHeadsignWithShortName);
        display.drawString(0, 48, busList[2].stopHeadsignWithShortName);
        display.setTextAlignment(TEXT_ALIGN_RIGHT);
        display.drawString(128, 20, busList[0].predictedArrivalMinutesString); //At 116 the apostrophe is sticking out so you have to put the minute numbers at 115! Tested with 88 '
        display.drawString(128, 34, busList[1].predictedArrivalMinutesString);
        display.drawString(128, 48, busList[2].predictedArrivalMinutesString);
    }
    display.display();
}