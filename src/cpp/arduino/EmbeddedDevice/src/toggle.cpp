#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// WiFi credentials - update these with your network details
const char* ssid = "Kevin Box";
const char* password = "abcdefg1";

// Server configuration
WiFiServer server(80);  // HTTP server on port 80
String header;

// LED Pin definitions for Arduino Nano ESP32
// Use predefined constants from Arduino core
// RGB LED is active-low (LOW = ON, HIGH = OFF)
const int LED_BUILTIN_PIN = LED_BUILTIN;  // Built-in LED
#ifdef LED_RED
const int LED_RED_PIN = LED_RED;      // RGB Red (GPIO 14)
const int LED_GREEN_PIN = LED_GREEN;  // RGB Green (GPIO 15)
const int LED_BLUE_PIN = LED_BLUE;    // RGB Blue (GPIO 16)
#else
// Fallback if constants not defined (shouldn't happen on Nano ESP32)
const int LED_RED_PIN = 14;   // RGB Red
const int LED_GREEN_PIN = 15; // RGB Green  
const int LED_BLUE_PIN = 16;  // RGB Blue
#endif

// LED state tracking
struct LEDState {
  bool builtin;
  bool red;
  bool green;
  bool blue;
} ledState = {false, false, false, false};

// Function declarations
void setupWiFi();
void handleClientRequest();
String createJSONResponse(const String& status, const String& message, const LEDState& state);
String createIPResponse();
void setLED(int pin, bool state);
void setRGBLED(int pin, bool state);
void setAllLEDs(bool state);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize LED pins
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  
  // Turn off all LEDs initially
  setAllLEDs(false);
  
  Serial.println("Initializing WiFi...");
  setupWiFi();
  
  // Start the server
  server.begin();
  Serial.println("HTTP Server started");
  Serial.print("Device IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("\nUse curl commands to control LEDs:");
  Serial.println("  curl http://" + WiFi.localIP().toString() + "/ip          # Get IP address");
  Serial.println("  curl http://" + WiFi.localIP().toString() + "/status       # Get LED status");
  Serial.println("  curl http://" + WiFi.localIP().toString() + "/led/builtin?state=on");
  Serial.println("  curl http://" + WiFi.localIP().toString() + "/led/rgb?r=1&g=0&b=1");
  Serial.println("  curl http://" + WiFi.localIP().toString() + "/led/all?state=on");
}

void loop() {
  handleClientRequest();
  delay(10); // Small delay for stability
}

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected!");
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed. Please check credentials.");
    Serial.println("The device will continue to retry...");
  }
}

void handleClientRequest() {
  WiFiClient client = server.available();
  
  if (!client) {
    return;
  }
  
  Serial.println("New client connected");
  String currentLine = "";
  header = "";  // Clear header for new request
  
  // Read request line
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      Serial.write(c);
      header += c;
      
      if (c == '\n') {
        if (currentLine.length() == 0) {
          // Parse the request
          if (header.indexOf("GET /led/builtin") >= 0) {
            // Control built-in LED
            if (header.indexOf("state=on") >= 0) {
              setLED(LED_BUILTIN_PIN, true);
              ledState.builtin = true;
              client.println(createJSONResponse("success", "Built-in LED turned ON", ledState));
            } else if (header.indexOf("state=off") >= 0) {
              setLED(LED_BUILTIN_PIN, false);
              ledState.builtin = false;
              client.println(createJSONResponse("success", "Built-in LED turned OFF", ledState));
            } else {
              client.println(createJSONResponse("error", "Invalid state parameter. Use state=on or state=off", ledState));
            }
          }
          else if (header.indexOf("GET /led/rgb") >= 0) {
            // Control RGB LED (active-low, so LOW = ON)
            bool r = ledState.red, g = ledState.green, b = ledState.blue;
            
            if (header.indexOf("r=1") >= 0) r = true; else if (header.indexOf("r=0") >= 0) r = false;
            if (header.indexOf("g=1") >= 0) g = true; else if (header.indexOf("g=0") >= 0) g = false;
            if (header.indexOf("b=1") >= 0) b = true; else if (header.indexOf("b=0") >= 0) b = false;
            
            setRGBLED(LED_RED_PIN, r);
            setRGBLED(LED_GREEN_PIN, g);
            setRGBLED(LED_BLUE_PIN, b);
            
            ledState.red = r;
            ledState.green = g;
            ledState.blue = b;
            
            client.println(createJSONResponse("success", "RGB LED updated", ledState));
          }
          else if (header.indexOf("GET /led/all") >= 0) {
            // Control all LEDs
            if (header.indexOf("state=on") >= 0) {
              setAllLEDs(true);
              ledState.builtin = true;
              ledState.red = true;
              ledState.green = true;
              ledState.blue = true;
              client.println(createJSONResponse("success", "All LEDs turned ON", ledState));
            } else if (header.indexOf("state=off") >= 0) {
              setAllLEDs(false);
              ledState.builtin = false;
              ledState.red = false;
              ledState.green = false;
              ledState.blue = false;
              client.println(createJSONResponse("success", "All LEDs turned OFF", ledState));
            } else {
              client.println(createJSONResponse("error", "Invalid state parameter. Use state=on or state=off", ledState));
            }
          }
          else if (header.indexOf("GET /status") >= 0) {
            // Get current status
            client.println(createJSONResponse("success", "Current LED status", ledState));
          }
          else if (header.indexOf("GET /ip") >= 0) {
            // Get device IP address
            client.println(createIPResponse());
          }
          else if (header.indexOf("GET /") >= 0) {
            // Root endpoint - show API documentation
            StaticJsonDocument<512> doc;
            doc["api"] = "LED Control API";
            doc["endpoints"]["GET /status"] = "Get current LED status";
            doc["endpoints"]["GET /ip"] = "Get device IP address";
            doc["endpoints"]["GET /led/builtin?state=on|off"] = "Control built-in LED";
            doc["endpoints"]["GET /led/rgb?r=0|1&g=0|1&b=0|1"] = "Control RGB LED";
            doc["endpoints"]["GET /led/all?state=on|off"] = "Control all LEDs";
            doc["ip"] = WiFi.localIP().toString();
            
            String jsonBody;
            serializeJsonPretty(doc, jsonBody);
            
            String httpResponse = "HTTP/1.1 200 OK\r\n";
            httpResponse += "Content-Type: application/json\r\n\r\n";
            httpResponse += jsonBody;
            client.println(httpResponse);
          }
          else {
            client.println(createJSONResponse("error", "Unknown endpoint", ledState));
          }
          
          break;
        } else {
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }
  }
  
  header = "";
  client.stop();
  Serial.println("Client disconnected");
}

String createJSONResponse(const String& status, const String& message, const LEDState& state) {
  // Allocate JSON document (adjust size if adding more fields)
  StaticJsonDocument<512> doc;
  
  // Build JSON object
  doc["status"] = status;
  doc["message"] = message;
  doc["ip"] = WiFi.localIP().toString();  // Include IP in all responses
  doc["leds"]["builtin"] = state.builtin;
  doc["leds"]["red"] = state.red;
  doc["leds"]["green"] = state.green;
  doc["leds"]["blue"] = state.blue;
  doc["timestamp"] = millis();
  
  // Serialize JSON to string
  String jsonBody;
  serializeJsonPretty(doc, jsonBody);
  
  // Build HTTP response
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: application/json\r\n";
  response += "Access-Control-Allow-Origin: *\r\n";
  response += "Connection: close\r\n\r\n";
  response += jsonBody;
  
  return response;
}

String createIPResponse() {
  // Simple IP address response
  StaticJsonDocument<256> doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  
  String jsonBody;
  serializeJsonPretty(doc, jsonBody);
  
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: application/json\r\n";
  response += "Access-Control-Allow-Origin: *\r\n";
  response += "Connection: close\r\n\r\n";
  response += jsonBody;
  
  return response;
}

void setLED(int pin, bool state) {
  // Regular LEDs: HIGH = ON, LOW = OFF
  digitalWrite(pin, state ? HIGH : LOW);
}

void setRGBLED(int pin, bool state) {
  // RGB LED is active-low: LOW = ON, HIGH = OFF
  digitalWrite(pin, state ? LOW : HIGH);
}

void setAllLEDs(bool state) {
  setLED(LED_BUILTIN_PIN, state);
  setRGBLED(LED_RED_PIN, state);
  setRGBLED(LED_GREEN_PIN, state);
  setRGBLED(LED_BLUE_PIN, state);
}