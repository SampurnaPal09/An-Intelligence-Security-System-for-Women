#define TINY_GSM_MODEM_SIM7600       // Define the GSM modem as SIM7600
#define TINY_GSM_USE_GPRS true        // Enable GPRS usage

#include <TinyGPS++.h>                // Include GPS parsing library
#include <TinyGsmClient.h>            // Include TinyGSM client for modem control
#include <HTTPClient.h>               // Include HTTP client for sending data to Google Sheets
#include <WiFi.h>                     // Include WiFi library

// Define ESP32 UART and button pins
#define RXD2 27                        // ESP32 TX for GSM
#define TXD2 26                        // ESP32 RX for GSM
#define powerPin 4                    // Power pin for GSM modem
#define buttonPin 5                   // Emergency button pin

#define GPS_TX_PIN 18                 // TX pin for GPS module
#define GPS_RX_PIN 19                 // RX pin for GPS module

TinyGPSPlus gps;                      // Create GPS object
float lat = 0, lng = 0;               // Variables to hold GPS latitude and longitude

// WiFi credentials
const char* ssid = "**********";      // Replace with your WiFi SSID
const char* password = "*****";       // Replace with your WiFi password
const char* scriptURL = "https://script.google.com/macros/s/___________________/exec"; // Replace with your Apps Script URL

// List of phone numbers to alert
const char* phoneNumbers[] = {
  "+91XXXXXXXXXX",
  "+91XXXXXXXXXX"               // Add more numbers as needed
};
const int totalNumbers = sizeof(phoneNumbers) / sizeof(phoneNumbers[0]); // Count of phone numbers

#define SerialAT Serial1               // Define Serial1 as GSM serial
#define SerialMon Serial              // Define Serial as monitor output
TinyGsm modem(SerialAT);              // Create modem object
bool buttonPressed = false;           // Button state flag

void setup() {
  Serial.begin(115200);               // Start Serial monitor
  Serial1.begin(9600, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN); // Begin GPS UART
  pinMode(buttonPin, INPUT_PULLUP);  // Set emergency button pin as input

  Serial.println("Fetching initial GPS fix...");
  while (!get_gps_data()) {           // Wait until GPS fix is acquired
    Serial.println("Retrying GPS fix...");
    delay(2000);
  }

  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2); // Start GSM UART
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);       // Power cycle GSM module
  delay(100);
  digitalWrite(powerPin, HIGH);
  delay(1000);
  digitalWrite(powerPin, LOW);
  delay(5000);

  Serial.println("Initializing modem...");
  if (!modem.init() || !modem.restart() || !modem.waitForNetwork()) { // Initialize modem and wait for network
    Serial.println("Modem/Network init failed!");
    return;
  }
  Serial.println("Network connected.");

  WiFi.begin(ssid, password);         // Connect to WiFi
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
}

void loop() {
  if (digitalRead(buttonPin) == HIGH) { // Check if emergency button is pressed
    delay(50);
    if (digitalRead(buttonPin) == HIGH && !buttonPressed) {
      buttonPressed = true;
      sendAlertWithGPS();             // Trigger alert sending process
    }
  } else {
    buttonPressed = false;            // Reset flag if button is released
  }
}

bool get_gps_data() {
  Serial.println("Fetching GPS data...");
  unsigned long start = millis();
  while (millis() - start < 30000) { // Wait up to 30 seconds for GPS fix
    while (Serial1.available()) gps.encode(Serial1.read()); // Parse GPS data
    if (gps.location.isValid()) {   // If valid location is found
      lat = gps.location.lat();     // Store latitude
      lng = gps.location.lng();     // Store longitude
      Serial.println("GPS Fixed.");
      return true;
    }
    delay(1000);
  }
  return false;                      // GPS fix failed
}

void sendAlertWithGPS() {
  bool gpsAvailable = get_gps_data(); // Try fetching latest GPS data

  String message = "I'm Adrija Roy. I'm in trouble.\n";
  message += "Father: +91840XXXXXXX\nMother: +91900XXXXXXX\n";

  if (gpsAvailable) {
    message += "Location: https://www.google.com/maps?q=";
    message += String(lat, 6) + "," + String(lng, 6);
  } else {
    message += "GPS not available.";
  }

  for (int i = 0; i < totalNumbers; i++) {
    modem.sendSMS(phoneNumbers[i], message.c_str()); // Send SMS
    delay(3000);
  }

  delay(5000);
  makeSequentialCalls(message);       // Initiate call sequence
}

void makeSequentialCalls(String alertMessage) {
  for (int i = 0; i < totalNumbers; i++) {
    String number = phoneNumbers[i];
    SerialAT.print("ATD"); SerialAT.print(number); SerialAT.println(";"); // Dial number

    String callStatus = "Not Answered";
    String duration = "00:00";
    bool callAnswered = false;
    bool callStarted = false;
    bool callEnded = false;
    bool isRinging = false;
    unsigned long callStart = 0, callEnd = 0;

    unsigned long timeout = millis();
    while (millis() - timeout < 60000) { // Wait for 1 minute max
      SerialAT.println("AT+CLCC");      // Check call status
      delay(1000);

      while (SerialAT.available()) {
        String line = SerialAT.readStringUntil('\n');
        if (line.indexOf("+CLCC:") != -1) {
          if (line.indexOf(",0,3,") != -1) {
            isRinging = true;
            callStatus = "Ringing";
          } else if (line.indexOf(",0,0,") != -1) {
            if (isRinging) {
              callAnswered = true;
              callStatus = "Answered";
              callStart = millis();
              callStarted = true;
              Serial.println("Call Answered.");
            }
          } else if (line.indexOf(",6") != -1 || line.indexOf(",0,6,") != -1) {
            callStatus = "Rejected";
            duration = "00:00";
            callEnded = true;
            break;
          }
        }
      }

      if (callStarted) {
        while (true) {
          SerialAT.println("AT+CLCC");
          delay(1000);
          bool stillActive = false;

          while (SerialAT.available()) {
            String check = SerialAT.readStringUntil('\n');
            if (check.indexOf("+CLCC:") != -1 && check.indexOf(",0,0,") != -1) {
              stillActive = true;
            }
          }

          if (!stillActive) {
            callEnd = millis();
            unsigned long secs = (callEnd - callStart) / 1000;
            int min = secs / 60;
            int sec = secs % 60;
            duration = (min < 10 ? "0" : "") + String(min) + ":" + (sec < 10 ? "0" : "") + String(sec);
            callEnded = true;
            break;
          }
        }
        break;
      }

      if (callEnded) break;
    }

    if (!callAnswered && callStatus != "Rejected") {
      callStatus = "Not Answered";
      duration = "00:00";
    }

    SerialAT.println("ATH"); // Hang up the call
    delay(2000);

    sendToGoogleSheet(number, "Sent", callStatus, duration); // Log details
  }
}

String urlencode(String str) {
  String encoded = "";
  char c, code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      encoded += '%';
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      encoded += char(code0 < 10 ? code0 + '0' : code0 - 10 + 'A');
      encoded += char(code1 < 10 ? code1 + '0' : code1 - 10 + 'A');
    }
  }
  return encoded; // Return URL-encoded string
}

void sendToGoogleSheet(String receiver, String smsStatus, String callStatus, String duration) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    String locationURL = "https://www.google.com/maps?q=" + String(lat, 6) + "," + String(lng, 6);
    String url = String(scriptURL) + "?";
    url += "date=" + urlencode(String(__DATE__));
    url += "&time=" + urlencode(String(__TIME__));
    url += "&sender_name=" + urlencode("Adrija Roy");
    url += "&sender_phone=" + urlencode("+91XXXXXXXXXX");
    url += "&receiver_phone=" + urlencode(receiver);
    url += "&sms_status=" + urlencode(smsStatus);
    url += "&call_status=" + urlencode(callStatus);
    url += "&call_duration=" + urlencode(duration);
    url += "&location=" + urlencode(locationURL);

    Serial.println("\xF0\x9F\x93\xA4 Sending to Google Sheet:");
    Serial.println(url);
    http.begin(url); // Send HTTP GET request
    int httpCode = http.GET();
    if (httpCode >= 200) {
      Serial.println("\xE2\x9C\x85 Successfully stored in Google Sheet.");
    } else {
      Serial.print("\xE2\x9D\x8C HTTP Error: ");
      Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("\xE2\x9D\x8C WiFi not connected!");
  }
}
