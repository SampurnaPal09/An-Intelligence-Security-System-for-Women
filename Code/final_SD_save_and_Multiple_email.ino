#include "esp_camera.h"  // Include ESP32 camera library
#include <WiFi.h>        // Include WiFi library for internet connectivity
#include "SPI.h"         // Include SPI library (used by SD card)
#include "driver/rtc_io.h" // Include RTC GPIO functions
#include <ESP_Mail_Client.h> // Include ESP Mail Client for sending emails
#include <FS.h>             // Include file system support
#include <LittleFS.h>       // Include internal flash file system
#include <SD_MMC.h>         // Include SD_MMC for microSD card access
#include <time.h>           // Include time functions for timestamping

// WiFi credentials
const char *ssid = "**********";         // Your WiFi SSID
const char *password = "********";       // Your WiFi password

// Email configuration
#define emailSenderAccount "abcd@gmail.com"       // Sender's Gmail address
#define emailSenderPassword "XXXX XXXX XXXX XXXX" // 16-digit App Password for Gmail
#define smtpServer "smtp.gmail.com"               // Gmail SMTP server
#define smtpServerPort 465                        // SMTP over SSL port
#define emailSubject "Please Help Me. I am in Trouble" // Subject of the email
#define emailRecipient1 "xyz@gmail.com"           // Receiver 1 email address
#define emailRecipient2 "pqr@gmail.com"           // Receiver 2 email address

// RF receiver data pin
#define RF_RECEIVER_PIN 13 // Pin connected to 433MHz RF receiver output

// Define camera model as AI Thinker (ESP32-CAM)
#define CAMERA_MODEL_AI_THINKER
#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#else
#error "Camera model not selected"
#endif

SMTPSession smtp;                          // Create an SMTP session object
void smtpCallback(SMTP_Status status);     // Function declaration for email status callback

// Define photo filenames to be saved in LittleFS
const char *photoFiles[] = {"/photo1.jpg", "/photo2.jpg", "/photo3.jpg"};
const int photoCount = sizeof(photoFiles) / sizeof(photoFiles[0]); // Number of photos
framesize_t desiredResolution = FRAMESIZE_SVGA; // Set camera resolution

// Flags and timers
bool emailSent = false;                  // Flag to check if email already sent
bool lastButtonState = LOW;              // To track RF signal changes
unsigned long lastDebounceTime = 0;      // Last time RF signal changed
const unsigned long debounceDelay = 200; // Debounce delay (ms)
int sendCount = 0;                       // Number of times photos sent (not used in loop)

void setup() {
  Serial.begin(115200);                // Start Serial Monitor at 115200 baud rate
  delay(1000);                         // Give some delay before starting

  pinMode(RF_RECEIVER_PIN, INPUT_PULLDOWN); // Configure RF receiver pin with pull-down

  WiFi.begin(ssid, password);          // Connect to WiFi
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n WiFi connected");
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());      // Print local IP address

  if (!LittleFS.begin()) {            // Mount LittleFS (internal flash storage)
    Serial.println(" Error mounting LittleFS");
    return;
  }

  if (!SD_MMC.begin()) {              // Mount microSD card
    Serial.println(" SD Card Mount Failed");
  } else {
    Serial.println(" SD Card initialized");
  }

  configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); // Set timezone to IST (+5:30) using NTP
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {     // Get current time
    Serial.println(" Failed to obtain time");
  }

  // Camera Configuration
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;      // XCLK frequency
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {                  // If external RAM is available
    config.frame_size = desiredResolution;
    config.jpeg_quality = 10;          // Better quality
    config.fb_count = 1;
  } else {
    config.frame_size = desiredResolution;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) { // Initialize camera
    Serial.println(" Camera init failed");
    return;
  }

  sensor_t *s = esp_camera_sensor_get(); // Get sensor settings
  if (s != NULL) {
    s->set_vflip(s, 1);              // Flip image vertically
    Serial.println(" Vertical flip enabled");
  }

  Serial.println(" Setup complete. Waiting for RF signal...");
}

void loop() {
  int reading = digitalRead(RF_RECEIVER_PIN); // Read RF signal

  if (reading != lastButtonState) {           // Detect signal change
    lastDebounceTime = millis();              // Reset debounce timer
  }

  if ((millis() - lastDebounceTime) > debounceDelay) { // If signal is stable
    if (reading == HIGH && !emailSent) {     // If RF trigger and email not yet sent
      for (int i = 0; i < 3; i++) {          // Take 3 rounds of pictures
        Serial.printf(" RF signal received! Attempt %d - Capturing & Saving...\n", i + 1);
        for (int j = 0; j < photoCount; j++) {
          captureAndSave(j);                // Capture and save each photo
        }
        sendPhotos();                       // Send photos via email
        clearLittleFS();                    // Clear photos from internal storage
        delay(5000);                        // Small delay between rounds
      }
      emailSent = true;                     // Set flag so photos aren't sent again
    } else if (reading == LOW) {
      emailSent = false;                    // Reset flag if signal goes low
    }
  }
  lastButtonState = reading;                // Store current RF state
}

// Get timestamp string for filename
String getTimeStamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(random(10000, 99999));   // Return random number if time not available
  }
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y_%m_%d--%H_%M", &timeinfo);
  return String(buffer);                   // Return formatted timestamp
}

// Capture image and save to LittleFS and SD card
void captureAndSave(int index) {
  camera_fb_t *fb = esp_camera_fb_get(); // Take photo
  if (!fb) {
    Serial.println(" Camera capture failed");
    delay(1000);
    ESP.restart();                        // Restart if camera fails
  }

  String timestamp = getTimeStamp();     // Get timestamp
  String fsPath = String("/photo") + (index + 1) + ".jpg"; // File path for LittleFS
  String sdPath = "/photo_" + timestamp + "_" + (index + 1) + ".jpg"; // File path for SD

  File file = LittleFS.open(fsPath, FILE_WRITE); // Save to LittleFS
  if (file) {
    file.write(fb->buf, fb->len);
    Serial.printf(" Saved to LittleFS: %s\n", fsPath.c_str());
    file.close();
  } else {
    Serial.printf(" Failed to save to LittleFS: %s\n", fsPath.c_str());
  }

  file = SD_MMC.open(sdPath, FILE_WRITE); // Save to SD card
  if (file) {
    file.write(fb->buf, fb->len);
    Serial.printf(" Saved to SD Card: %s\n", sdPath.c_str());
    file.close();
  } else {
    Serial.printf(" Failed to save to SD Card: %s\n", sdPath.c_str());
  }

  esp_camera_fb_return(fb);              // Return the buffer to free memory
}

// Send saved photos via email
void sendPhotos() {
  smtp.debug(1);                         // Enable debug output
  smtp.callback(smtpCallback);          // Set callback for status

  Session_Config config;
  config.server.host_name = smtpServer;
  config.server.port = smtpServerPort;
  config.login.email = emailSenderAccount;
  config.login.password = emailSenderPassword;

  SMTP_Message message;
  message.sender.name = "Help Me, I am Adrija Sen";
  message.sender.email = emailSenderAccount;
  message.subject = emailSubject;
  message.addRecipient("Recipient", emailRecipient1);
  message.addRecipient("Recipient", emailRecipient2);
  // You can add more recipients here if needed

  // Message content
  String htmlMsg = "<div style=\"color:#ff0000;\"><h2>I am Adrija Sen. Please help me I am in trouble.<br>My Father's Phone Number: +91 840XXXXXXX<br>My Mother's Phone Number: +91 900XXXXXXX<br>My Phone Number: +91 721XXXXXXX</h2><p>~ Sent from Adrija Sen.</p></div>";
  message.html.content = htmlMsg.c_str();
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  // Attach images
  for (int i = 0; i < photoCount; i++) {
    SMTP_Attachment att;
    att.descr.filename = String("photo") + (i + 1) + ".jpg";
    att.descr.mime = "image/jpeg";
    att.file.path = photoFiles[i];
    att.file.storage_type = esp_mail_file_storage_type_flash;
    att.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;
    message.addAttachment(att);
  }

  // Send the email
  if (!smtp.connect(&config) || !MailClient.sendMail(&smtp, &message, true)) {
    Serial.println(" Error sending Email: " + smtp.errorReason());
  } else {
    Serial.println(" Email sent successfully!");
  }
}

// Delete captured files from LittleFS after sending
void clearLittleFS() {
  for (int i = 0; i < photoCount; i++) {
    if (LittleFS.exists(photoFiles[i])) {
      LittleFS.remove(photoFiles[i]);
      Serial.printf(" Deleted %s from LittleFS\n", photoFiles[i]);
    }
  }
}

// Callback function to show email sending status
void smtpCallback(SMTP_Status status) {
  if (status.success()) {
    Serial.println(" Email sent successfully [Callback]");
  } else {
    Serial.println(" SMTP sending in queue...");
  }
}
