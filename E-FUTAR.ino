/*
 * E-FUTAR
 * Embedded BKK FUTAR application port for ESP32 SoC with SSD1306 OLED display
 * 
 * Written by Balazs Markus
 * 
 * With the help of the program, the microcontroller displays the next buses departing from a given bus stop and the remaining time until departure, as well as the current time on an OLED display.
 * The program can handle several stops, you can switch between them with the built-in button of the development card, the loading status is indicated by the built-in white SMD LED.
 * 
 * WiFi configuration required before uploading!
 * If you want to add other stops, the list of JSON files to be parsed must be modified!
 * 
 * See the ReadMe file for more information
 * 
 */

#include <ArduinoJson.h>
#include <WiFi.h>
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include <HTTPClient.h>

bool summerTime = false; //true in summer, false in winter, in the second case it adds an hour to the queried UNIX time

enum busStop {Baross, Janos, Varoskozpont}; // The enum that distinguishes the three preprogrammed stops, we switch between them with the interrupt of the control button, and in the main loop, the HTTP request we send to the server is determined based on this value
enum busStop currentStop = Baross; // default bus stop

bool busStopChangeButtonPressed = false; // debounce

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
char apiKey[256] = "YOUR_API_KEY";

HTTPClient http;

//OLED pins to ESP32 GPIOs via this connecting:
//OLED_SDA -- GPIO4
//OLED_SCL -- GPIO15
//OLED_RST -- GPIO16
SSD1306  display(0x3c, 4, 15); //Init OLED display

// Strings to store the stop name, and the link to the API
char stopName[128];
char resource[512];                    // http resource
const unsigned long BAUD_RATE = 115200;                 // serial connection speed

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

//Converts the the arrival time that we got in long type, and a resolution of seconds to int type, with a resolution of one minute, to be able to copy it to predictedArrivalMinutesInt
int SecondsToMinutes(long secondsLong) {
    long minutesLong;
    int minutesInt;
    minutesLong = secondsLong/60;
    minutesInt = (int)minutesLong;
    return minutesInt;
}

//To print the arrival time, the time we first converted to int must be converted to char, and the apostrophe must be placed at the end, this is what the function does
void ArrivalMinutesToString(int arrivalMinutes, char* arrivalString) {
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

struct BusData busList[10];
char currentTime[32];
long currentTimeLong,currentTimeHours,currentTimeMinutes;

char clockTimeString[5];

void ConvertTime() {
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
    Serial.print("The current time: ");
    Serial.print(currentTimeHours);
    Serial.print(":");
    Serial.println(currentTimeMinutes);
}

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

void setStop() {
    char baseUrl[] = "https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json"; // define the base URL

    if (currentStop == Baross) {
        strcpy(stopName, "Baross utca");
        sprintf(resource, "%s?stopId=BKK_F04144&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60&key=%s", baseUrl, apiKey);
    }
    if (currentStop == Janos) {
        strcpy(stopName, "János utca");
        sprintf(resource, "%s?stopId=BKK_F04126&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60&key=%s", baseUrl, apiKey);
    }
    if (currentStop == Varoskozpont) {
        strcpy(stopName, "Városközpont");
        sprintf(resource, "%s?stopId=BKK_F04122&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60&key=%s", baseUrl, apiKey);
    }
}

void changeCurrentStop() {
    digitalWrite(25, HIGH);
    if(currentStop==Baross) currentStop = Janos;
    else if(currentStop==Janos) currentStop = Varoskozpont;
    else if(currentStop==Varoskozpont) currentStop = Baross;
}


// ARDUINO entry point #1: runs once when you press reset or power the board
void setup() {
    Serial.begin(115200);
    pinMode(0, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(0), changeCurrentStop, FALLING);
    pinMode(25, OUTPUT); //the busy LED on the motherboard
    digitalWrite(25, LOW);
    setupDisplay();
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 0, "E-FUTÁR");
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 28, "Csatlakozás"); // "Connecting"
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 52, "Írta: Márkus Balázs"); // "Written by Balazs Markus"
    display.display();
    // attempt to connect to Wifi network:

    Serial.print("Connecting to ");
    Serial.println(ssid);

    connectToWiFi();
}

// Main loop
void loop() {

    setStop();
    clearBusList();

    if (WiFi.status() == WL_CONNECTED) {
    

    Serial.print("[HTTP] begin...\n");
    if (http.begin(resource)) {
      Serial.print("[HTTP] GET...\n");
      int httpCode = http.GET();
      
      if (httpCode > 0) {
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          if (readReponseContent(payload.c_str())) {
                printBusData();
          }
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
    connectToWiFi();
  }
    
    // from there the graphic part
    drawList();

    // a little delay so that it doesn't do quieries too often
    wait();
}

void drawList() {
    // Pringting the list to the screen
    // Additional fonts are available at http://oleddisplay.squix.ch/
   
    display.clear();

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, stopName);
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

uint16_t maxArraySize=0, ArraySize=0;

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

bool readReponseContent(const char* jsonString) {
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

    ConvertTime();

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
            busList[i].predictedArrivalMinutesInt = SecondsToMinutes(busList[i].predictedArrivalTimeLong-currentTimeLong);  // subtract the current time and then convert it from second to minute
            ArrivalMinutesToString(busList[i].predictedArrivalMinutesInt,busList[i].predictedArrivalMinutesString);

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
            digitalWrite(25, LOW);
        }
        else if(actualBus.containsKey("arrivalTime")) {
            // predticted time is NOT valid
            Serial.println("Arrival");
            strncpy(busList[i].stopHeadsign, root["data"]["entry"]["stopTimes"][i]["stopHeadsign"],20);
            String arrivalTimeString = root["data"]["entry"]["stopTimes"][i]["arrivalTime"];
            strncpy(busList[i].predictedArrivalTime, arrivalTimeString.c_str(),10);
            busList[i].predictedArrivalTimeLong = atol(busList[i].predictedArrivalTime); // cast from string to long
            busList[i].predictedArrivalMinutesInt = SecondsToMinutes(busList[i].predictedArrivalTimeLong-currentTimeLong);  // subtract the current time and then convert it from second to minute
            ArrivalMinutesToString(busList[i].predictedArrivalMinutesInt,busList[i].predictedArrivalMinutesString);

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
            digitalWrite(25, LOW);
        }
        else {
            Serial.println("noInfo");
        }
    }
    if(maxArraySize==0) {
        // if no start is found in the next 60 minutes then the LED will not go off so let's just turn it off
        digitalWrite(25, LOW);
    }
    return true;
}


// Print the data extracted from the JSON
void printBusData() {
    for(uint16_t i=0; i<maxArraySize; i++) {
        Serial.print("shortName = ");
        Serial.println(busList[i].shortName);
        Serial.print("stopHeadsign = ");
        Serial.println(busList[i].stopHeadsign);
        Serial.print("predictedArrivalTime = ");
        Serial.println(busList[i].predictedArrivalTime);
        Serial.print("predictedArrivalTimeLong = ");
        Serial.println(busList[i].predictedArrivalTimeLong);
        Serial.print("predictedArrivalMinutesInt = ");
        Serial.println(busList[i].predictedArrivalMinutesInt);
    }
}


void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  
  int attempts = 0;
  //On the BOOT screen, the dots are animated, just like in a Serial message, until you are connected to Wi-Fi
  int x = 88;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(300);
    Serial.println("Attempting to connect to WiFi...");
    attempts++;

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

// Wait a little, so that we don't do queries too often
void wait() {
    Serial.println("Wait 2 seconds");
    delay(2000);
}

void printWifiStatus() {
    // print the SSID of the network you're attached to:
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print your WiFi device's IP address:
    IPAddress ip = WiFi.localIP();
    Serial.print("IP Address: ");
    Serial.println(ip);

    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
}