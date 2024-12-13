#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <ArduinoJson.h>
#include <AES.h>

// WiFi Credentials
const char *ssid = "OnePlus_11R_5G";
const char *password = "12345678";

ESP8266WebServer server(80);
Servo doorServo;
const int servoPin = 5;         // GPIO 5 (D1)
const int buzzerPin = 4;        // GPIO 4 (D2)
const int ledPin = 14;          // GPIO 14 (D5) for LED PWM control
const int photoResistorPin = A0; // Analog pin for photoresistor
bool manualMode = false;        // Manual mode flag (default: false for dynamic)
int failedAttempts = 0;


// AES Decryption Key (must be 16 bytes for AES-128)
const byte aesKey[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                         0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// AES Initialization Vector (16 bytes)
const byte aesIV[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Function declarations
void openDoor();
void triggerBuzzer();
void handleUnlock();
void handleOptions();
void handleSetBrightness();
void handleSetManualMode();
void adjustBrightnessBasedOnLight();


// Function implementations
void openDoor() {
    Serial.println("Starting servo movement");
    doorServo.write(0);
    delay(1000);
    doorServo.write(180);
    delay(5000);
    doorServo.write(0);
    Serial.println("Door operation complete");
}

void triggerBuzzer() {
    Serial.println("Triggering buzzer");
    for (int i = 0; i < 5; i++) {
        digitalWrite(buzzerPin, HIGH);
        delay(500);
        digitalWrite(buzzerPin, LOW);
        delay(500);
    }
}

void handleUnlock() {
    Serial.println("Received unlock request");

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    const char *passcode = doc["passcode"];
    if (!passcode) {
        server.send(400, "text/plain", "No passcode provided");
        return;
    }

    // Convert encrypted passcode to byte array (assuming it is Base64 encoded)
    byte encryptedData[16];
    int encryptedDataLength = base64_decode((char *)encryptedData, encryptedPasscode, strlen(encryptedPasscode));

    if (encryptedDataLength != 16) {
        server.send(400, "text/plain", "Invalid encrypted passcode");
        return;
    }

    // Decrypt the passcode
    AES aes(AESKeyLength::AES_128, aesKey, aesIV);
    byte decryptedData[16];
    aes.decryptBlock(encryptedData, decryptedData);

    // Null-terminate the decrypted passcode to use it as a string
    decryptedData[15] = '\0';

    if (strcmp(passcode, "1234") == 0) {
        Serial.println("Correct passcode! Opening door...");
        server.send(200, "text/plain", "Door unlocked!");
        openDoor();
        failedAttempts = 0;
        
    } else {
        Serial.println("Invalid passcode!");
        failedAttempts++;
        // Create a JSON response with the status and the number of failed attempts
        StaticJsonDocument<200> responseDoc;
        responseDoc["status"] = "Invalid password!";
        responseDoc["failedAttempts"] = failedAttempts;

        String responseBody;
        serializeJson(responseDoc, responseBody);

        server.send(401, "application/json", responseBody);
        if (failedAttempts >= 3) {
            triggerBuzzer();
        }
    }
}

void handleSetBrightness() {
    if (!manualMode) {
        server.send(400, "text/plain", "Manual mode is not enabled. Enable it to set brightness manually.");
        return;
    }

    Serial.println("Received brightness adjustment request");

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");

    DynamicJsonDocument doc(200);

    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    int brightness = doc["brightness"];
    if (brightness < 0 || brightness > 1023) {
        server.send(400, "text/plain", "Invalid brightness value");
        return;
    }

    int pwmValue = map(brightness, 0, 1023, 0, 255);
    analogWrite(ledPin, pwmValue);

    Serial.print("Brightness manually set to: ");
    Serial.println(pwmValue);
    server.send(200, "text/plain", "Brightness adjusted manually!");
}

void handleSetManualMode() {
    Serial.println("Received manual mode toggle request");

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");

    DynamicJsonDocument doc(200);

    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    manualMode = doc["manualMode"];
    Serial.print("Manual mode set to: ");
    Serial.println(manualMode);

    server.send(200, "text/plain", "Manual mode updated!");
}

void handleOptions() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
}

void adjustBrightnessBasedOnLight() {
    if (!manualMode) {
        int lightValue = analogRead(photoResistorPin);
        static int smoothedValue = 0;

        // Exponential smoothing for light readings
        smoothedValue = 0.8 * smoothedValue + 0.2 * lightValue;

        // Adjust range if needed based on observed values
        int brightness = 255 - lightValue;
        brightness = constrain(brightness, 0, 255); // Ensure within PWM range

        analogWrite(ledPin, brightness);

        Serial.print("Dynamic mode - Smoothed light value: ");
        Serial.print(smoothedValue);
        Serial.print(", Adjusted brightness: ");
        Serial.println(brightness);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nStarting Door Lock System");

    analogWriteFreq(1000); // Set PWM frequency to 1 kHz

    // Initialize servo
    doorServo.attach(servoPin);
    doorServo.write(0);

    // Setup buzzer
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);

    // Setup LED
    pinMode(ledPin, OUTPUT);
    analogWrite(ledPin, 0);

    // Configure WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Setup server routes
    server.on("/unlock", HTTP_POST, handleUnlock);
    server.on("/unlock", HTTP_OPTIONS, handleOptions);
    server.on("/setBrightness", HTTP_POST, handleSetBrightness);
    server.on("/setBrightness", HTTP_OPTIONS, handleOptions);
    server.on("/setManualMode", HTTP_POST, handleSetManualMode);
    server.on("/setManualMode", HTTP_OPTIONS, handleOptions);

    server.on("/", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "Door Lock System Online");
    });

    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 3000) { // Check every 3 seconds
        lastCheck = millis();
        adjustBrightnessBasedOnLight();
    }
}