/*
 * ESP32-C6 XIAO Weather Display
 * Displays temperature and rainfall on 150 Neopixels
 * Uses FastLED library and OpenWeatherMap API
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <Preferences.h>

// LED Configuration
#define NUM_LEDS 150
#define DATA_PIN D9
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// Reset button pin
#define RESET_BUTTON_PIN D10

CRGB leds[NUM_LEDS];

// Preferences for storing settings
Preferences preferences;

// WiFi Configuration (will be loaded from preferences)
String WIFI_SSID = "";
String WIFI_PASSWORD = "";

// OpenWeatherMap Configuration (will be loaded from preferences)
String OPEN_WEATHER_MAP_API_KEY = "e28ba1db8ba3c57983a446d6afbcb55b"; // Default API key
String OPEN_WEATHER_MAP_LOCATION_ID = "";
const String OPEN_WEATHER_MAP_UNITS = "imperial"; // Use imperial for Fahrenheit

// Configuration portal
WebServer server(80);
const char* AP_SSID = "ESP32-Weather-Config";
const char* AP_PASSWORD = "config123"; // Password for config portal
bool configMode = false;

// Weather data
float currentTemp = 0;
bool isRaining = false;
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 600000; // 10 minutes in milliseconds

// Rain animation variables
#define NUM_BALLS 6
float tCycle[NUM_BALLS];
int pos[NUM_BALLS];
unsigned long tLast[NUM_BALLS];
float h[NUM_BALLS];
float GRAVITY[NUM_BALLS] = {2.0, 0.4, 0.6, 0.3, 1.2, 0.1};
int COR[NUM_BALLS];
int pMax = 0;
float george = 0;
const int RAIN_LED_COUNT = 60; // Use bottom 60 LEDs for rain (matching original)
const float RAIN_TIME_SCALE = 5000.0; // Larger value slows raindrops (doubled again to slow by 1/2)

// Rain bounce variables
bool rainBouncing[NUM_BALLS] = {false, false, false, false, false, false};
float rainBounceVelocity[NUM_BALLS] = {0, 0, 0, 0, 0, 0};
float rainBounceHeight[NUM_BALLS] = {0, 0, 0, 0, 0, 0};
unsigned long rainBounceStartTime[NUM_BALLS] = {0, 0, 0, 0, 0, 0};
int rainBounceWaterLevel[NUM_BALLS] = {0, 0, 0, 0, 0, 0}; // Water level at time of impact
const float RAIN_BOUNCE_COEFFICIENT = 0.25f; // Bounce energy retention for rain (smaller bounce)

// Display mode: 0 = temperature, 1 = rain, 2 = color palette
int displayMode = 2; // Start with color palette
unsigned long modeSwitchTime = 0;
const unsigned long TEMP_DISPLAY_INTERVAL = 120000; // Show temp twice every 2 minutes (120 seconds)
const unsigned long TEMP_DISPLAY_DURATION = 30000; // Show temp for 30 seconds each time
int tempDisplayCount = 0; // Track how many temp displays in current cycle
unsigned long lastTempCycleStart = 0;

// Temporary test mode - set to true to force rain display for testing
const bool TEST_RAIN_MODE = true; // Set to false to disable after testing
unsigned long testRainStartTime = 0;
const unsigned long TEST_RAIN_DURATION = 15000; // Show rain for 15 seconds

// Temperature falling animation variables
int tempDisplayState = 0; // 0 = showing tens, 1 = showing ones, 2 = done
unsigned long tempStateTime = 0;
int tempTensCount = 0;
int tempOnesCount = 0;
int tempCurrentStack = 0;
float tempH = 0;
unsigned long tempTLast = 0;
int tempPos = 0;
int tempPMax = 0;
int lastDisplayedTemp = -1; // Track last displayed temperature to detect changes
bool tempInitialized = false; // Track if temperature display has been initialized
const float TEMP_GRAVITY = 0.250f; // Slower gravity for temperature drops (halved again from 0.375)

// Bounce physics variables
bool tempBouncing = false; // Is current stack bouncing?
float tempBounceVelocity = 0; // Current bounce velocity (positive = upward)
float tempBounceHeight = 0; // Current bounce height from landing position
unsigned long tempBounceStartTime = 0; // When bounce started
int tempBounceCount = 0; // Number of bounces
const float BOUNCE_COEFFICIENT = 0.8f; // Bounce energy retention (0.8 = loses 20% each bounce) - increased for stronger bounce
const float BOUNCE_DAMPING = 0.9f; // Additional damping factor - reduced damping for stronger bounce
const int MAX_BOUNCES = 3; // Maximum number of bounces before settling

// Color palette display variables
unsigned long paletteStartTime = 0;
int currentPaletteIndex = 0;
int targetPaletteIndex = 0;
float paletteBlendProgress = 0.0;
const float PALETTE_TRANSITION_SPEED = 0.0001; // Slow transition (adjust for speed)
unsigned long lastPaletteChange = 0;
const unsigned long PALETTE_CHANGE_INTERVAL = 120000; // Change palette every 2 minutes
int paletteOffset = 0; // For animation

bool resetPaletteState = false; // Force palette re-init when switching modes

// Star animation variables removed

// Function to load settings from preferences
void loadSettings() {
  preferences.begin("weather", true); // true = read-only
  
  WIFI_SSID = preferences.getString("wifi_ssid", "");
  WIFI_PASSWORD = preferences.getString("wifi_pass", "");
  OPEN_WEATHER_MAP_LOCATION_ID = preferences.getString("location_id", "");
  String savedKey = preferences.getString("api_key", "");
  if (savedKey.length() > 0) {
    OPEN_WEATHER_MAP_API_KEY = savedKey;
  }
  
  preferences.end();
  
  Serial.println("Loaded settings:");
  Serial.print("WiFi SSID: ");
  Serial.println(WIFI_SSID.length() > 0 ? WIFI_SSID : "(not set)");
  Serial.print("Location ID: ");
  Serial.println(OPEN_WEATHER_MAP_LOCATION_ID.length() > 0 ? OPEN_WEATHER_MAP_LOCATION_ID : "(not set)");
}

// Function to clear all saved settings
void clearSettings() {
  preferences.begin("weather", false); // false = read-write
  preferences.clear(); // Clear all keys in the "weather" namespace
  preferences.end();
  
  // Reset global variables
  WIFI_SSID = "";
  WIFI_PASSWORD = "";
  OPEN_WEATHER_MAP_LOCATION_ID = "";
  
  Serial.println("All settings cleared!");
}

// Function to save settings to preferences
void saveSettings(String ssid, String password, String locationId, String apiKey) {
  preferences.begin("weather", false); // false = read-write
  
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.putString("location_id", locationId);
  if (apiKey.length() > 0) {
    preferences.putString("api_key", apiKey);
  }
  
  preferences.end();
  
  // Update global variables
  WIFI_SSID = ssid;
  WIFI_PASSWORD = password;
  OPEN_WEATHER_MAP_LOCATION_ID = locationId;
  if (apiKey.length() > 0) {
    OPEN_WEATHER_MAP_API_KEY = apiKey;
  }
  
  Serial.println("Settings saved!");
}

// HTML form for configuration
String getConfigHTML() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 Weather Config</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f5f5f5; }";
  html += "h1 { color: #333; text-align: center; }";
  html += "form { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "label { display: block; margin-top: 15px; margin-bottom: 5px; color: #555; font-weight: bold; }";
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; font-size: 16px; }";
  html += "input[type='submit'] { width: 100%; padding: 15px; margin-top: 20px; background: #4CAF50; color: white; border: none; border-radius: 5px; font-size: 18px; cursor: pointer; }";
  html += "input[type='submit']:hover { background: #45a049; }";
  html += ".info { background: #e7f3ff; padding: 10px; border-radius: 5px; margin-bottom: 15px; font-size: 14px; color: #0066cc; }";
  html += "</style></head><body>";
  html += "<h1>🌤️ Weather Display Configuration</h1>";
  html += "<div class='info'>Connect your device to WiFi and configure weather location.</div>";
  html += "<form action='/save' method='POST'>";
  html += "<label for='ssid'>WiFi Network Name (SSID):</label>";
  html += "<input type='text' id='ssid' name='ssid' required placeholder='Your WiFi network name'>";
  html += "<label for='password'>WiFi Password:</label>";
  html += "<input type='password' id='password' name='password' placeholder='Your WiFi password'>";
  html += "<label for='location'>Weather Location ID:</label>";
  html += "<input type='text' id='location' name='location' required placeholder='e.g., 5879400 (Anchorage, AK)'>";
  html += "<div class='info' style='margin-top: 10px;'>Find your location ID at <a href='https://openweathermap.org/find' target='_blank'>openweathermap.org/find</a></div>";
  html += "<label for='apikey'>API Key (optional, leave blank to use default):</label>";
  html += "<input type='text' id='apikey' name='apikey' placeholder='Your OpenWeatherMap API key'>";
  html += "<input type='submit' value='Save Configuration'>";
  html += "</form></body></html>";
  return html;
}

// Handle root page
void handleRoot() {
  server.send(200, "text/html", getConfigHTML());
}

// Handle save configuration
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("location")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String location = server.arg("location");
    String apiKey = server.arg("apikey");
    
    saveSettings(ssid, password, location, apiKey);
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Configuration Saved</title>";
    html += "<style>body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; text-align: center; }";
    html += "h1 { color: #4CAF50; } .info { background: #e7f3ff; padding: 20px; border-radius: 5px; margin: 20px 0; }</style></head><body>";
    html += "<h1>✅ Configuration Saved!</h1>";
    html += "<div class='info'>The device will restart and connect to your WiFi network.<br>You can close this page.</div>";
    html += "<p>Restarting in 5 seconds...</p></body></html>";
    
    server.send(200, "text/html", html);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing required fields");
  }
}

// Start configuration portal (AP mode)
void startConfigPortal() {
  configMode = true;
  Serial.println("Starting configuration portal...");
  
  // Start AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  
  server.begin();
  Serial.println("Configuration portal started!");
  Serial.print("Connect to WiFi: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Then open: http://");
  Serial.println(IP);
  
  // Flash LEDs to indicate config mode
  for(int i = 0; i < 3; i++) {
    for(int j = 0; j < NUM_LEDS; j++) {
      leds[j] = CRGB(255, 255, 0); // Yellow
    }
    FastLED.show();
    delay(200);
    FastLED.clear();
    FastLED.show();
    delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Initialize reset button pin (INPUT_PULLUP - button connects to ground)
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  // Check reset button immediately on startup
  // Button pressed = LOW (connected to ground)
  delay(50); // Small delay for button debounce
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("Reset button pressed - clearing all settings!");
    
    // Flash LEDs red to indicate reset
    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(100);
    for(int i = 0; i < 5; i++) {
      for(int j = 0; j < NUM_LEDS; j++) {
        leds[j] = CRGB(255, 0, 0); // Red
      }
      FastLED.show();
      delay(200);
      FastLED.clear();
      FastLED.show();
      delay(200);
    }
    
    // Clear all settings
    clearSettings();
    
    Serial.println("Settings cleared! Starting configuration portal...");
    delay(500);
    
    // Start config portal
    startConfigPortal();
    return; // Exit setup, stay in config mode
  }
  
  // Initialize FastLED
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  FastLED.clear();
  FastLED.show();
  
  // Test LEDs - flash all red, then green, then blue
  Serial.println("Testing LEDs...");
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(255, 0, 0); // Red
  }
  FastLED.show();
  delay(500);
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 255, 0); // Green
  }
  FastLED.show();
  delay(500);
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 0, 255); // Blue
  }
  FastLED.show();
  delay(500);
  FastLED.clear();
  FastLED.show();
  Serial.println("LED test complete");
  
  // Load saved settings
  loadSettings();
  
  // Check if configuration is needed
  if (WIFI_SSID.length() == 0 || OPEN_WEATHER_MAP_LOCATION_ID.length() == 0) {
    Serial.println("Configuration needed - starting config portal");
    startConfigPortal();
    // Will stay in config mode until settings are saved
  } else {
  // Initialize rain animation variables
  for(int i = 0; i < NUM_BALLS; i++) {
    tLast[i] = millis();
    h[i] = 0;
    COR[i] = random(2, 12);
    // GRAVITY is already initialized with array values
  }
    
    // Connect to WiFi
    connectWiFi();
    
    // Test rain display
    Serial.println("Testing rain display...");
    testRainDisplay();
    delay(1000);
    
    // Get initial weather data
    Serial.println("Fetching initial weather data...");
    updateWeather();
    Serial.print("Initial weather fetch complete. Temp: ");
    Serial.print(currentTemp);
    Serial.print("°F, Raining: ");
    Serial.println(isRaining);
    
    // Initialize test rain mode if enabled
    if (TEST_RAIN_MODE) {
      testRainStartTime = millis();
      displayMode = 1; // Force rain mode
      isRaining = true; // Force rain state
      Serial.println("TEST MODE: Rain display will show for 15 seconds");
    }
    
    Serial.println("Setup complete!");
  }
}

void loop() {
  // If in config mode, handle web server requests
  if (configMode) {
    server.handleClient();
    
    // Flash yellow LED every 2 seconds to indicate config mode
    static unsigned long lastFlash = 0;
    if (millis() - lastFlash > 2000) {
      for(int j = 0; j < NUM_LEDS; j++) {
        leds[j] = CRGB(255, 255, 0); // Yellow
      }
      FastLED.show();
      delay(100);
      FastLED.clear();
      FastLED.show();
      lastFlash = millis();
    }
    return;
  }
  
  // Normal operation mode
  static unsigned long lastLoopDebug = 0;
  if (millis() - lastLoopDebug > 5000) {
    Serial.print("Loop - currentTemp: ");
    Serial.print(currentTemp);
    Serial.print(", isRaining: ");
    Serial.print(isRaining);
    Serial.print(", displayMode: ");
    Serial.print(displayMode);
    Serial.print(", WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
    lastLoopDebug = millis();
  }
  
  // Check if we need to update weather
  if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL) {
    Serial.println("Updating weather...");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      connectWiFi();
    }
    updateWeather();
    lastWeatherUpdate = millis();
    Serial.print("Weather update complete. Temp: ");
    Serial.println(currentTemp);
  }
  
  // Check test rain mode
  if (TEST_RAIN_MODE && testRainStartTime > 0) {
    if (millis() - testRainStartTime < TEST_RAIN_DURATION) {
      // Force rain display during test period
      displayMode = 1;
      isRaining = true;
      displayRain();
      return; // Skip normal mode switching during test
    } else {
      // Test period over, reset
      testRainStartTime = 0;
      Serial.println("TEST MODE: Rain test period complete, returning to normal operation");
    }
  }
  
  // Mode management: Show temp twice every 2 minutes, color palette the rest of the time
  unsigned long currentTime = millis();
  
  // Initialize lastTempCycleStart on first run
  static bool cycleInitialized = false;
  if (!cycleInitialized) {
    lastTempCycleStart = currentTime;
    cycleInitialized = true;
  }
  
  // Static flags for tracking temperature display calls
  static bool firstCallDone = false;
  static bool secondCallDone = false;
  
  // Check if it's time for temperature display cycle (every 2 minutes)
  if (currentTime - lastTempCycleStart >= TEMP_DISPLAY_INTERVAL) {
    // Start new temperature cycle
    lastTempCycleStart = currentTime;
    modeSwitchTime = currentTime;
    displayMode = 0; // Switch to temperature
    // Reset flags for new cycle
    firstCallDone = false;
    secondCallDone = false;
    Serial.println("Starting temperature display cycle");
  }
  
  // If in temperature mode, call displayTemperature() twice
  if (displayMode == 0) {
    
    if (!firstCallDone) {
      // First call - run until complete
      if (displayTemperature()) {
        // First display complete, clear and prepare for second
        firstCallDone = true;
        delay(500); // Brief pause between displays
        Serial.println("First temperature display complete");
      }
    } else if (!secondCallDone) {
      // Second call - run until complete
      if (displayTemperature()) {
        // Second display complete, switch to rain if raining, otherwise palette
        secondCallDone = true;
        if (isRaining) {
          displayMode = 1; // Switch to rain mode
          Serial.println("Second temperature display complete, switching to rain display");
        } else {
          displayMode = 2; // Switch to color palette
          Serial.println("Second temperature display complete, switching to color palette");
        }
        modeSwitchTime = currentTime;
        FastLED.clear();
        FastLED.show();
      }
    } else {
      // Both displays done, switch to rain if raining, otherwise palette
      if (isRaining) {
        displayMode = 1; // Switch to rain mode
      } else {
        displayMode = 2; // Switch to color palette
      }
    }
  }
  
  // Clear and reset state when switching modes to prevent conflicts
  static int lastDisplayMode = -1;
  if (lastDisplayMode != displayMode) {
    FastLED.clear();
    FastLED.show();
    // Reset temperature display state when switching away from it
    if (lastDisplayMode == 0) {
      tempDisplayState = 0;
      tempCurrentStack = 0;
      tempPMax = 0;
      tempInitialized = false;
    }
    // Reset rain state when switching away from it
    // NOTE: pMax and george are NOT reset - they persist so water level accumulates
    if (lastDisplayMode == 1) {
      // Only reset drop positions and bounce states, keep pMax and george for accumulation
      for(int i = 0; i < NUM_BALLS; i++) {
        tLast[i] = millis();
        h[i] = 0;
        COR[i] = random(2, 12);
        rainBouncing[i] = false;
        rainBounceVelocity[i] = 0;
        rainBounceHeight[i] = 0;
        rainBounceStartTime[i] = 0;
        rainBounceWaterLevel[i] = 0;
      }
    }
    // Reset palette state when switching TO palette mode (force re-initialization)
    if (displayMode == 2) {
      resetPaletteState = true; // Force palette to re-initialize
    }
    lastDisplayMode = displayMode;
  }
  
  // Display based on current mode
  if (displayMode == 0) {
    displayTemperature();
  } else if (displayMode == 1 && isRaining) {
    displayRain();
  } else if (displayMode == 2) {
    displayColorPalette();
  } else {
    displayColorPalette(); // Fallback to color palette
  }
  
  delay(50);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed! Starting config portal...");
    startConfigPortal();
  }
}

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping weather update");
    return;
  }
  
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?id=" + 
               OPEN_WEATHER_MAP_LOCATION_ID + 
               "&appid=" + OPEN_WEATHER_MAP_API_KEY + 
               "&units=" + OPEN_WEATHER_MAP_UNITS;
  
  if (OPEN_WEATHER_MAP_LOCATION_ID.length() == 0) {
    Serial.println("Location ID not configured!");
    return;
  }
  
  Serial.print("Fetching weather from: ");
  Serial.println(url);
  
  http.begin(url);
  http.setTimeout(10000); // 10 second timeout
  http.setConnectTimeout(5000); // 5 second connect timeout
  
  int httpCode = http.GET();
  Serial.print("HTTP response code: ");
  Serial.println(httpCode);
  
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Weather response:");
    Serial.println(payload);
    
    // Parse JSON
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      // Get temperature (already in Fahrenheit due to units=imperial)
      currentTemp = doc["main"]["temp"];
      Serial.print("Temperature: ");
      Serial.print(currentTemp);
      Serial.println("°F");
      
      // Check if it's raining
      isRaining = false;
      if (doc["weather"].is<JsonArray>()) {
        JsonArray weatherArray = doc["weather"];
        for (JsonObject weatherItem : weatherArray) {
          int weatherId = weatherItem["id"];
          String main = weatherItem["main"];
          
          // Check for rain conditions (weather ID 200-531)
          if ((weatherId >= 200 && weatherId < 600) || main == "Rain") {
            isRaining = true;
            Serial.println("It's raining!");
            break;
          }
        }
      }
      
      // Also check rain object if present
      if (doc["rain"].is<JsonObject>()) {
        isRaining = true;
        Serial.println("Rain detected in rain object");
      }
      
    } else {
      Serial.print("JSON parsing error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("HTTP error code: ");
    Serial.println(httpCode);
    Serial.println("Failed to get weather data");
    // Keep previous temperature if update fails
  }
  
  http.end();
  
  // If we still don't have a valid temperature, set a default for testing
  if (currentTemp == 0) {
    Serial.println("Warning: Temperature is 0, setting default for testing");
    currentTemp = 72.0; // Default temperature for testing
  }
  isRaining = true;
}

bool displayTemperature() {
  // Returns true when display is complete, false while still displaying
  int tempInt = (int)currentTemp;
  int tens = tempInt / 10;
  int ones = tempInt % 10;
  
  // Debug output
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) {
    Serial.print("Display temp - tempInt: ");
    Serial.print(tempInt);
    Serial.print(", tens: ");
    Serial.print(tens);
    Serial.print(", ones: ");
    Serial.print(ones);
    Serial.print(", state: ");
    Serial.print(tempDisplayState);
    Serial.print(", stack: ");
    Serial.println(tempCurrentStack);
    lastDebug = millis();
  }
  
  // If temperature is 0 or invalid, show error pattern
  if (tempInt <= 0 || tempInt > 150) {
    // Flash all LEDs red to indicate error
    static bool errorFlash = false;
    static unsigned long lastFlash = 0;
    if (millis() - lastFlash > 500) {
      errorFlash = !errorFlash;
      for(int j = 0; j < NUM_LEDS; j++) {
        leds[j] = errorFlash ? CRGB(255, 0, 0) : CRGB(0, 0, 0);
      }
      FastLED.show();
      lastFlash = millis();
    }
    return false; // Error state, still displaying
  }
  
  // Reset if temperature changed OR if lastDisplayedTemp is -1 (forced reset)
  if (tempInt != lastDisplayedTemp || lastDisplayedTemp == -1) {
    tempDisplayState = 0;
    tempCurrentStack = 0;
    tempPMax = 0;
    tempTLast = millis(); // FIX: Set to current time, not 0
    tempH = 0;
    tempPos = NUM_LEDS - 1;
    tempBouncing = false; // Reset bounce state
    tempBounceVelocity = 0;
    tempBounceHeight = 0;
    tempBounceCount = 0;
    tempInitialized = false; // Need to re-initialize
    lastDisplayedTemp = tempInt;
    FastLED.clear();
    FastLED.show();
    Serial.println("Temperature changed, resetting display");
  }
  
  // Initialize state if starting fresh
  if (!tempInitialized && tempDisplayState == 0 && tempCurrentStack == 0) {
    tempTensCount = tens;
    tempOnesCount = ones;
    tempCurrentStack = 0;
    tempTLast = millis(); // CRITICAL: Set to current time
    tempPMax = 0;
    tempH = 0; // Start at top (height = 0)
    tempPos = NUM_LEDS - 1; // CRITICAL: Start position at top
    tempBouncing = false;
    tempBounceVelocity = 0;
    tempBounceHeight = 0;
    tempBounceCount = 0;
    tempInitialized = true;
    FastLED.clear();
    FastLED.show();
    Serial.print("Starting temp display - tens: ");
    Serial.print(tens);
    Serial.print(", ones: ");
    Serial.print(ones);
    Serial.print(", tempInt: ");
    Serial.println(tempInt);
    
    // Make sure we have valid values
    if (tempTensCount == 0 && tempOnesCount == 0) {
      Serial.println("ERROR: Both tens and ones are 0!");
      // Flash error pattern
      for(int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(255, 0, 0);
      }
      FastLED.show();
      return false; // Error state, still displaying
    }
  }
  
  // Display tens place - falling stacks (EXACT COPY OF ONES PLACE LOGIC)
  if (tempDisplayState == 0) {
    if (tempCurrentStack < tempTensCount) {
      if (!tempBouncing) {
        // Stack is falling - EXACT COPY OF WORKING ONES CODE
        unsigned long now = millis();
        float tempTCycle = (now - tempTLast) / 1000.0; // Convert to seconds (renamed to avoid conflict with rain tCycle array)
        
        // Safety check: if tempTCycle is negative or very large, reset to start at top
        if (tempTCycle < 0.0 || tempTCycle > 10.0) {
          tempTLast = now;
          tempTCycle = 0.0;
          tempH = 0.0;
          tempPos = NUM_LEDS - 1;
        } else {
          tempH = 0.5 * TEMP_GRAVITY * pow(tempTCycle, 2.0);
          tempPos = (NUM_LEDS - 1) - (int)(tempH * (NUM_LEDS - 1));
        }
        
        // Ensure tempPos doesn't go below 0 or above top
        if (tempPos < 0) tempPos = 0;
        if (tempPos > NUM_LEDS - 1) tempPos = NUM_LEDS - 1;
        
        // Stack lands when it reaches the current top of the pile
        if (tempPos <= tempPMax) {
          // Calculate impact velocity for bounce
          // Velocity = gravity * time, scale for LED units (LEDs per second)
          float impactVelocity = TEMP_GRAVITY * tempTCycle * NUM_LEDS; // Velocity in LED units per second
          tempBounceVelocity = impactVelocity * BOUNCE_COEFFICIENT * BOUNCE_DAMPING; // Initial bounce velocity (in LED units per second)
          tempBounceHeight = 0;
          tempBounceStartTime = millis();
          tempBounceCount = 0;
          tempBouncing = true;
          tempPos = tempPMax; // Set to landing position
        }
      } else {
        // Stack is bouncing
        float bounceTime = (millis() - tempBounceStartTime) / 1000.0;
        
        // Physics: h = v0*t - 0.5*g*t^2 (upward motion, then gravity pulls down)
        // tempBounceVelocity is in LED units per second, scale gravity for LED units
        tempBounceHeight = tempBounceVelocity * bounceTime - 0.5 * TEMP_GRAVITY * NUM_LEDS * pow(bounceTime, 2.0);
        
        // Convert bounce height to LED position offset (tempBounceHeight is in LED units)
        int bounceOffset = (int)tempBounceHeight;
        tempPos = tempPMax + bounceOffset;
        
        // CRITICAL: Ensure tempPos doesn't go out of bounds - especially prevent going above top
        if (tempPos < 0) tempPos = 0;
        if (tempPos > NUM_LEDS - 1) {
          tempPos = NUM_LEDS - 1; // Cap at top of column
          // If we hit the top, reverse the bounce direction
          tempBounceHeight = 0;
          tempBounceVelocity = -tempBounceVelocity * 0.5; // Reverse and dampen
        }
        
        // If bounce height goes negative, we've hit the ground again
        if (tempBounceHeight <= 0 || tempPos <= tempPMax) {
          tempPos = tempPMax; // Snap to landing position
          tempBounceCount++;
          
          if (tempBounceCount >= MAX_BOUNCES || tempBounceVelocity < 2.0) {
            // Done bouncing - settle into position
            tempBouncing = false;
            tempBounceVelocity = 0;
            tempBounceHeight = 0;
            
            // Stack has settled, add it to the pile
            tempTLast = millis();
            tempCurrentStack++;
            tempPMax = tempCurrentStack * 5;
            
            // Draw all stacked LEDs
            for(int j = 0; j < tempPMax && j < NUM_LEDS; j++) {
              leds[j] = CRGB(250, 0, 0); // Red background
            }
            // Green markers every 5 LEDs
            for(int j = 0; j < tempPMax && j < NUM_LEDS; j += 5) {
              leds[j] = CRGB(0, 100, 0); // Green marker
            }
            // Clear above the stack
            for(int j = tempPMax; j < NUM_LEDS; j++) {
              leds[j] = CRGB(0, 0, 0);
            }
            FastLED.show();
            
            if (tempCurrentStack >= tempTensCount) {
              // All tens stacks done, wait then switch to ones
              delay(2000);
              
              // Clear display completely before switching
              FastLED.clear();
              FastLED.show();
              delay(100); // Ensure clear is visible
              
              tempDisplayState = 1;
              tempCurrentStack = 0;
              tempTLast = millis();
              tempPMax = 0;
              tempH = 0; // Reset height
              tempPos = NUM_LEDS - 1; // Reset position to top
              tempBouncing = false;
              tempBounceVelocity = 0;
              tempBounceHeight = 0;
              tempBounceCount = 0;
              
              // Clear again to be sure
              FastLED.clear();
              FastLED.show();
            } else {
              // Start next stack falling - EXACT COPY OF WORKING ONES CODE
              tempTLast = millis();
              tempBouncing = false;
              tempH = 0; // Reset height
              tempPos = NUM_LEDS - 1; // Reset position to top
            }
          } else {
            // Bounce again with reduced velocity
            tempBounceVelocity = tempBounceVelocity * BOUNCE_COEFFICIENT * BOUNCE_DAMPING;
            tempBounceStartTime = millis();
            tempBounceHeight = 0;
          }
        }
      }
      
      // Draw the current state
      FastLED.clear();
      
      // Draw already stacked LEDs (if any)
      for(int j = 0; j < tempPMax && j < NUM_LEDS; j++) {
        leds[j] = CRGB(250, 0, 0); // Red background
      }
      // Green markers every 5 LEDs
      for(int j = 0; j < tempPMax && j < NUM_LEDS; j += 5) {
        leds[j] = CRGB(0, 100, 0); // Green marker
      }
      
      // Draw falling/bouncing stack (5 LEDs)
      for(int j = 0; j < 5; j++) {
        int ledPos = tempPos - j; // Draw downward from tempPos
        if (ledPos >= 0 && ledPos < NUM_LEDS) {
          leds[ledPos] = CRGB(255, 0, 0); // Red stack
        }
      }
      
      FastLED.show();
      delay(10);
    }
  }
  // Display ones place - falling stacks
  else if (tempDisplayState == 1) {
    // If ones count is 0, immediately go to done state
    if (tempOnesCount == 0) {
      tempDisplayState = 2;
      tempStateTime = millis();
      FastLED.clear();
      FastLED.show();
      return false; // Still displaying (in done state)
    }
    
    if (tempCurrentStack < tempOnesCount) {
      if (!tempBouncing) {
        // Stack is falling
        unsigned long now = millis();
        float tempTCycle = (now - tempTLast) / 1000.0; // Convert to seconds (renamed to avoid conflict with rain tCycle array)
        
        // Safety check: if tempTCycle is negative or very large, reset to start at top
        if (tempTCycle < 0.0 || tempTCycle > 10.0) {
          tempTLast = now;
          tempTCycle = 0.0;
          tempH = 0.0;
          tempPos = NUM_LEDS - 1;
        } else {
          tempH = 0.5 * TEMP_GRAVITY * pow(tempTCycle, 2.0);
          tempPos = (NUM_LEDS - 1) - (int)(tempH * (NUM_LEDS - 1));
        }
        
        // Ensure tempPos doesn't go below 0 or above top
        if (tempPos < 0) tempPos = 0;
        if (tempPos > NUM_LEDS - 1) tempPos = NUM_LEDS - 1;
        
        // Stack lands when it reaches the current top of the pile
        if (tempPos <= tempPMax) {
          // Calculate impact velocity for bounce
          // Velocity = gravity * time, scale for LED units (LEDs per second)
          float impactVelocity = TEMP_GRAVITY * tempTCycle * NUM_LEDS; // Velocity in LED units per second
          tempBounceVelocity = impactVelocity * BOUNCE_COEFFICIENT * BOUNCE_DAMPING; // Initial bounce velocity (in LED units per second)
          tempBounceHeight = 0;
          tempBounceStartTime = millis();
          tempBounceCount = 0;
          tempBouncing = true;
          tempPos = tempPMax; // Set to landing position
        }
      } else {
        // Stack is bouncing
        float bounceTime = (millis() - tempBounceStartTime) / 1000.0;
        
        // Physics: h = v0*t - 0.5*g*t^2 (upward motion, then gravity pulls down)
        // tempBounceVelocity is in LED units per second, scale gravity for LED units
        tempBounceHeight = tempBounceVelocity * bounceTime - 0.5 * TEMP_GRAVITY * NUM_LEDS * pow(bounceTime, 2.0);
        
        // Convert bounce height to LED position offset (tempBounceHeight is in LED units)
        int bounceOffset = (int)tempBounceHeight;
        tempPos = tempPMax + bounceOffset;
        
        // CRITICAL: Ensure tempPos doesn't go out of bounds - especially prevent going above top
        if (tempPos < 0) tempPos = 0;
        if (tempPos > NUM_LEDS - 1) {
          tempPos = NUM_LEDS - 1; // Cap at top of column
          // If we hit the top, reverse the bounce direction
          tempBounceHeight = 0;
          tempBounceVelocity = -tempBounceVelocity * 0.5; // Reverse and dampen
        }
        
        // If bounce height goes negative, we've hit the ground again
        if (tempBounceHeight <= 0 || tempPos <= tempPMax) {
          tempPos = tempPMax; // Snap to landing position
          tempBounceCount++;
          
          if (tempBounceCount >= MAX_BOUNCES || tempBounceVelocity < 2.0) {
            // Done bouncing - settle into position
            tempBouncing = false;
            tempBounceVelocity = 0;
            tempBounceHeight = 0;
            
            // Stack has settled, add it to the pile
            tempTLast = millis();
            tempCurrentStack++;
            tempPMax = tempCurrentStack * 5;
            
            // Draw all stacked LEDs
            for(int j = 0; j < tempPMax && j < NUM_LEDS; j++) {
              leds[j] = CRGB(0, 0, 250); // Blue background
            }
            // Green markers every 5 LEDs
            for(int j = 0; j < tempPMax && j < NUM_LEDS; j += 5) {
              leds[j] = CRGB(0, 255, 0); // Green marker
            }
            // Clear above the stack
            for(int j = tempPMax; j < NUM_LEDS; j++) {
              leds[j] = CRGB(0, 0, 0);
            }
            FastLED.show();
            
            if (tempCurrentStack >= tempOnesCount) {
              // All ones stacks done, wait then reset
              delay(2000);
              tempDisplayState = 2;
              tempStateTime = millis();
              tempBouncing = false;
              tempBounceVelocity = 0;
              tempBounceHeight = 0;
              tempBounceCount = 0;
            } else {
              // Start next stack falling - EXACT COPY OF WORKING ONES CODE
              tempTLast = millis();
              tempBouncing = false;
              tempH = 0; // Reset height
              tempPos = NUM_LEDS - 1; // Reset position to top
            }
          } else {
            // Bounce again with reduced velocity
            tempBounceVelocity = tempBounceVelocity * BOUNCE_COEFFICIENT * BOUNCE_DAMPING;
            tempBounceStartTime = millis();
            tempBounceHeight = 0;
          }
        }
      }
      
      // Draw the current state
      FastLED.clear();
      
      // Draw already stacked LEDs (if any)
      for(int j = 0; j < tempPMax && j < NUM_LEDS; j++) {
        leds[j] = CRGB(0, 0, 250); // Blue background
      }
      // Green markers every 5 LEDs
      for(int j = 0; j < tempPMax && j < NUM_LEDS; j += 5) {
        leds[j] = CRGB(0, 255, 0); // Green marker
      }
      
      // Draw falling/bouncing stack (5 LEDs)
      for(int j = 0; j < 5; j++) {
        int ledPos = tempPos - j; // Draw downward from tempPos
        if (ledPos >= 0 && ledPos < NUM_LEDS) {
          leds[ledPos] = CRGB(0, 0, 255); // Blue stack
        }
      }
      
      FastLED.show();
      delay(10);
    } else {
      // tempCurrentStack >= tempOnesCount but we haven't transitioned to state 2 yet
      // This can happen if tempOnesCount is 0 or all stacks are done
      // Transition to done state
      tempDisplayState = 2;
      tempStateTime = millis();
      FastLED.clear();
      FastLED.show();
    }
  }
  // Done displaying, hold for a bit then clear and return
  else if (tempDisplayState == 2) {
    // Continue to display the final stacked state with all green markers
    FastLED.clear();
    
    // Draw all stacked LEDs (blue background)
    for(int j = 0; j < tempPMax && j < NUM_LEDS; j++) {
      leds[j] = CRGB(0, 0, 250); // Blue background
    }
    // Green markers every 5 LEDs (keep all visible)
    for(int j = 0; j < tempPMax && j < NUM_LEDS; j += 5) {
      leds[j] = CRGB(0, 255, 0); // Green marker
    }
    
    FastLED.show();
    
    if (millis() - tempStateTime > 3000) {
      // Clear output and return true to indicate done
      FastLED.clear();
      FastLED.show();
      // Reset state for next call
      tempDisplayState = 0;
      tempCurrentStack = 0;
      tempPMax = 0;
      tempInitialized = false;
      lastDisplayedTemp = -1; // Force re-initialization on next call
      return true; // Display complete
    }
    return false; // Still holding
  }
  
  return false; // Still displaying
}

void testRainDisplay() {
  // Initialize rain test variables
  for(int i = 0; i < NUM_BALLS; i++) {
    tLast[i] = millis();
    h[i] = 0;
    COR[i] = random(2, 12);
    // GRAVITY is already initialized with array values
  }
  pMax = 0;
  george = 0;
  
  // Run rain animation for 8 seconds (longer to see bounce effect)
  unsigned long testStart = millis();
  unsigned long testDuration = 8000; // 8 seconds
  
  while (millis() - testStart < testDuration) {
    // Rain animation matching original (no bounce)
    for(int i = 0; i < NUM_BALLS; i++) {
      tCycle[i] = millis() - tLast[i];
      h[i] = 0.5 * GRAVITY[i] * pow(tCycle[i] / RAIN_TIME_SCALE, 2.0);
      // Drops fall from top down to bottom (matching original calculation style)
      pos[i] = (NUM_LEDS - (round(h[i] * NUM_LEDS))) - COR[i];
      
      // Ensure position is within bounds
      if (pos[i] < 0) pos[i] = 0;
      if (pos[i] >= NUM_LEDS) pos[i] = NUM_LEDS - 1;
      
      // Check if drop hits water level - if so, reset and add to water
      if (pos[i] <= pMax && pos[i] >= 0) {
        tLast[i] = millis();
        george = george + 1;
        pMax = round(george / 10);
        
        // Reset drop to top for next fall
        h[i] = 0;
        COR[i] = random(2, 12);
        // Immediately set position to top for next frame
        pos[i] = NUM_LEDS - 1 - COR[i];
        if (pos[i] < 0) pos[i] = NUM_LEDS - 1;
      }
    }
    
    // Background color (dark blue-green) - fill all LEDs
    for(int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB(0, 25, 12);
    }
    
    // Water level at bottom (cyan/blue) - only in bottom area
    for(int i = 0; i < pMax && i < NUM_LEDS; i++) {
      leds[i] = CRGB(0, 255, i * 4);
    }
    
    // Falling raindrops (green and blue like original) - can appear anywhere
    for(int i = 0; i < NUM_BALLS; i++) {
      if (pos[i] >= 0 && pos[i] < NUM_LEDS) {
        leds[pos[i]] = CRGB(0, 255, 0);        // Green
        if (pos[i] + 1 < NUM_LEDS) {
          leds[pos[i] + 1] = CRGB(0, 25, 255);  // Blue (matching original)
        }
      }
    }
    
    FastLED.show();
    
    // Clear raindrops for next frame
    for(int i = 0; i < NUM_BALLS; i++) {
      if (pos[i] >= 0 && pos[i] < NUM_LEDS) {
        leds[pos[i]] = CRGB(0, 0, 0);
        if (pos[i] + 1 < NUM_LEDS) {
          leds[pos[i] + 1] = CRGB(0, 0, 0);
        }
      }
    }
    
    // Reset water level if it gets too high (increased from 40 to 80)
    if(pMax > 80) {
      george = 0;
      pMax = 0;
    }
    
    delay(50);
  }
  
  // Clear after test
  FastLED.clear();
  FastLED.show();
  Serial.println("Rain test complete");
}

void displayRain() {
  // Rain animation with single bounce when hitting water (using temperature bounce physics)
  for(int i = 0; i < NUM_BALLS; i++) {
    if (!rainBouncing[i]) {
      // Drop is falling
      tCycle[i] = millis() - tLast[i];
      h[i] = 0.5 * GRAVITY[i] * pow(tCycle[i] / RAIN_TIME_SCALE, 2.0);
      // Drops fall from top down to bottom (matching original: pos = (60 - round(h*60)) - COR)
      // Scaled for 150 LEDs: use NUM_LEDS instead of 60
      pos[i] = (NUM_LEDS - (round(h[i] * NUM_LEDS))) - COR[i];
      
      // Ensure position is within valid range
      if (pos[i] < 0) {
        pos[i] = 0;
      }
      if (pos[i] >= NUM_LEDS) {
        pos[i] = NUM_LEDS - 1;
      }
      
      // Check if drop hits water level - if so, start bounce
      // Only trigger if drop is at or below water level and wasn't already bouncing
      if (pos[i] <= pMax && pos[i] >= 0 && !rainBouncing[i]) {
        // Store the water level at impact (before incrementing)
        rainBounceWaterLevel[i] = pMax;
        
        // Calculate impact velocity for bounce (reduced energy for rain)
        // Velocity = gravity * time, scale for LED units (LEDs per second)
        float impactVelocity = GRAVITY[i] * (tCycle[i] / RAIN_TIME_SCALE) * NUM_LEDS; // Velocity in LED units per second
        rainBounceVelocity[i] = impactVelocity * BOUNCE_COEFFICIENT * BOUNCE_DAMPING * RAIN_BOUNCE_COEFFICIENT; // Reduced bounce energy for rain
        rainBounceHeight[i] = 0;
        rainBounceStartTime[i] = millis();
        rainBouncing[i] = true;
        pos[i] = rainBounceWaterLevel[i]; // Set to water level at impact
        
        // Add to water level (this happens after storing the impact level)
        george = george + 1;
        pMax = round(george / 10);
      }
    } else {
      // Drop is bouncing - use same physics as temperature bounce
      float bounceTime = (millis() - rainBounceStartTime[i]) / 1000.0;
      
      // Physics: h = v0*t - 0.5*g*t^2 (upward motion, then gravity pulls down)
      // Use average gravity scaled for LED units (same as temperature)
      float avgGravity = 1.0; // Average of GRAVITY array
      rainBounceHeight[i] = rainBounceVelocity[i] * bounceTime - 0.5 * avgGravity * NUM_LEDS * pow(bounceTime, 2.0);
      
      // Convert bounce height to LED position offset (rainBounceHeight is in LED units)
      int bounceOffset = (int)rainBounceHeight[i];
      pos[i] = rainBounceWaterLevel[i] + bounceOffset;
      
      // CRITICAL: Ensure pos doesn't go out of bounds - especially prevent going above top
      if (pos[i] < 0) pos[i] = 0;
      if (pos[i] > NUM_LEDS - 1) {
        pos[i] = NUM_LEDS - 1; // Cap at top of column
        // If we hit the top, reverse the bounce direction
        rainBounceHeight[i] = 0;
        rainBounceVelocity[i] = -rainBounceVelocity[i] * 0.5; // Reverse and dampen
      }
      
      // If bounce height goes negative, we've hit the water again - bounce is done
      if (rainBounceHeight[i] <= 0 || pos[i] <= rainBounceWaterLevel[i]) {
        pos[i] = rainBounceWaterLevel[i]; // Snap to water level
        
        // Single bounce complete - reset drop to top for next fall
        rainBouncing[i] = false;
        rainBounceVelocity[i] = 0;
        rainBounceHeight[i] = 0;
        tLast[i] = millis();
        h[i] = 0;
        COR[i] = random(2, 12);
        pos[i] = NUM_LEDS - 1 - COR[i];
        if (pos[i] < 0) pos[i] = NUM_LEDS - 1;
      }
    }
  }
  
  // Background color (dark blue-green) - fill all LEDs
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 25, 12);
  }
  
  // Water level at bottom (cyan/blue) - only in bottom area
  for(int i = 0; i < pMax && i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 255, i * 4);
  }
  
  // Falling raindrops (green and blue like original) - can appear anywhere
  for(int i = 0; i < NUM_BALLS; i++) {
    if (pos[i] >= 0 && pos[i] < NUM_LEDS) {
      leds[pos[i]] = CRGB(0, 255, 0);        // Green
      if (pos[i] + 1 < NUM_LEDS) {
        leds[pos[i] + 1] = CRGB(0, 25, 255);  // Blue (matching original)
      }
    }
  }
  
  FastLED.show();
  
  // Clear raindrops for next frame (before next calculation)
  for(int i = 0; i < NUM_BALLS; i++) {
    if (pos[i] >= 0 && pos[i] < NUM_LEDS) {
      leds[pos[i]] = CRGB(0, 0, 0);
      if (pos[i] + 1 < NUM_LEDS) {
        leds[pos[i] + 1] = CRGB(0, 0, 0);
      }
    }
  }
  
  // Reset water level if it gets too high (increased from 40 to 80)
  if(pMax > 80) {
    george = 0;
    pMax = 0;
  }
  
  delay(50);
}

// Color palette definitions (cool blues and blacks)
DEFINE_GRADIENT_PALETTE(blueBlack1_gp){
    0,   0,   0,   0,  // Black
   64,   0,  20,  40,  // Dark blue
  128,   0,  50, 100,  // Medium blue
  192,   0,  80, 150,  // Bright blue
  255,   0, 100, 200   // Light blue
};

DEFINE_GRADIENT_PALETTE(blueBlack2_gp){
    0,   0,   0,   0,  // Black
   64,   0,  10,  30,  // Very dark blue
  128,   0,  40,  80,  // Dark blue
  192,   0,  70, 130,  // Medium blue
  255,   0, 120, 180   // Bright blue
};

DEFINE_GRADIENT_PALETTE(blueBlack3_gp){
    0,   0,   0,   0,  // Black
   32,   0,   5,  15,  // Very dark
   96,   0,  30,  60,  // Dark blue
  160,   0,  60, 120,  // Medium blue
  224,   0,  90, 160,  // Bright blue
  255,   0, 110, 200   // Light blue
};

DEFINE_GRADIENT_PALETTE(blueBlack4_gp){
    0,   0,   0,   0,  // Black
   85,   0,  15,  35,  // Dark blue
  170,   0,  45,  90,  // Medium blue
  255,   0,  75, 140   // Bright blue
};

DEFINE_GRADIENT_PALETTE(blueBlack5_gp){
    0,   0,   0,   0,  // Black
   51,   0,  25,  50,  // Dark blue
  102,   0,  50, 100,  // Medium blue
  153,   0,  75, 150,  // Bright blue
  204,   0, 100, 180,  // Light blue
  255,   0, 120, 200   // Very light blue
};

DEFINE_GRADIENT_PALETTE(blueBlack6_gp){
    0,   0,   0,   0,  // Black
   42,   0,  10,  25,  // Very dark blue
   85,   0,  30,  60,  // Dark blue
  128,   0,  55, 110,  // Medium blue
  170,   0,  80, 150,  // Bright blue
  213,   0, 100, 180,  // Light blue
  255,   0, 115, 200   // Very light blue
};

DEFINE_GRADIENT_PALETTE(blueBlack7_gp){
    0,   0,   0,   0,  // Black
   36,   0,   8,  20,  // Very dark
   73,   0,  20,  45,  // Dark blue
  109,   0,  40,  85,  // Medium-dark blue
  146,   0,  65, 130,  // Medium blue
  182,   0,  85, 160,  // Bright blue
  219,   0, 105, 190,  // Light blue
  255,   0, 125, 210   // Very light blue
};

DEFINE_GRADIENT_PALETTE(blueBlack8_gp){
    0,   0,   0,   0,  // Black
   32,   0,  12,  30,  // Dark blue
   64,   0,  25,  55,  // Medium-dark blue
   96,   0,  40,  85,  // Medium blue
  128,   0,  60, 120,  // Bright blue
  160,   0,  80, 150,  // Light blue
  192,   0, 100, 180,  // Very light blue
  224,   0, 115, 200,  // Brightest blue
  255,   0, 130, 220   // Maximum blue
};

// Dark forest green palettes
DEFINE_GRADIENT_PALETTE(greenBlack1_gp){
    0,   0,   0,   0,  // Black
   64,   0,  20,   5,  // Dark forest green
  128,   0,  50,  15,  // Medium forest green
  192,   0,  80,  25,  // Bright forest green
  255,   0, 100,  35   // Light forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack2_gp){
    0,   0,   0,   0,  // Black
   64,   0,  15,   8,  // Very dark forest green
  128,   0,  40,  18,  // Dark forest green
  192,   0,  70,  28,  // Medium forest green
  255,   0, 100,  40   // Bright forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack3_gp){
    0,   0,   0,   0,  // Black
   32,   0,   8,   3,  // Very dark
   96,   0,  30,  12,  // Dark forest green
  160,   0,  60,  22,  // Medium forest green
  224,   0,  90,  32,  // Bright forest green
  255,   0, 110,  42   // Light forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack4_gp){
    0,   0,   0,   0,  // Black
   85,   0,  18,   8,  // Dark forest green
  170,   0,  45,  18,  // Medium forest green
  255,   0,  75,  30   // Bright forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack5_gp){
    0,   0,   0,   0,  // Black
   51,   0,  25,  10,  // Dark forest green
  102,   0,  50,  20,  // Medium forest green
  153,   0,  75,  30,  // Bright forest green
  204,   0, 100,  40,  // Light forest green
  255,   0, 120,  50   // Very light forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack6_gp){
    0,   0,   0,   0,  // Black
   42,   0,  12,   5,  // Very dark forest green
   85,   0,  30,  12,  // Dark forest green
  128,   0,  55,  22,  // Medium forest green
  170,   0,  80,  32,  // Bright forest green
  213,   0, 100,  42,  // Light forest green
  255,   0, 115,  52   // Very light forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack7_gp){
    0,   0,   0,   0,  // Black
   36,   0,  10,   4,  // Very dark
   73,   0,  22,  10,  // Dark forest green
  109,   0,  40,  18,  // Medium-dark forest green
  146,   0,  65,  28,  // Medium forest green
  182,   0,  85,  38,  // Bright forest green
  219,   0, 105,  48,  // Light forest green
  255,   0, 125,  58   // Very light forest green
};

DEFINE_GRADIENT_PALETTE(greenBlack8_gp){
    0,   0,   0,   0,  // Black
   32,   0,  14,   6,  // Dark forest green
   64,   0,  28,  12,  // Medium-dark forest green
   96,   0,  42,  18,  // Medium forest green
  128,   0,  60,  26,  // Bright forest green
  160,   0,  80,  36,  // Light forest green
  192,   0, 100,  46,  // Very light forest green
  224,   0, 115,  56,  // Brightest forest green
  255,   0, 130,  66   // Maximum forest green
};

// Array of palette pointers - blues first (0-7), then greens (8-15)
CRGBPalette16 palettes[] = {
  blueBlack1_gp,   // 0
  blueBlack2_gp,   // 1
  blueBlack3_gp,   // 2
  blueBlack4_gp,   // 3
  blueBlack5_gp,   // 4
  blueBlack6_gp,   // 5
  blueBlack7_gp,   // 6
  blueBlack8_gp,   // 7
  greenBlack1_gp,  // 8
  greenBlack2_gp,  // 9
  greenBlack3_gp,  // 10
  greenBlack4_gp,  // 11
  greenBlack5_gp,  // 12
  greenBlack6_gp,  // 13
  greenBlack7_gp,  // 14
  greenBlack8_gp   // 15
};
const int NUM_PALETTES = 16;
const int NUM_BLUE_PALETTES = 8;
const int NUM_GREEN_PALETTES = 8;

void displayColorPalette() {
  static unsigned long lastPaletteChange = 0;
  static int currentPaletteIndex = 0;
  static int targetPaletteIndex = 0;
  static float paletteBlendProgress = 0.0;
  static int paletteOffset = 0;
  static bool initialized = false;
  // Track palette type (blue or green) for complex alternation
  static bool isBlue = true; // Track current color family
  static int sequenceCount = 0; // For complex patterns
  
  unsigned long currentTime = millis();
  
  // Check if we need to reset (when switching to palette mode)
  if (resetPaletteState) {
    initialized = false;
    resetPaletteState = false;
  }
  
  // Initialize on first call
  if (!initialized) {
    lastPaletteChange = currentTime;
    paletteOffset = 0; // Initialize offset
    // Randomly start with blue or green
    isBlue = random(2) == 0;
    if (isBlue) {
      currentPaletteIndex = random(0, NUM_BLUE_PALETTES);
    } else {
      currentPaletteIndex = random(NUM_BLUE_PALETTES, NUM_PALETTES);
    }
    targetPaletteIndex = currentPaletteIndex;
    paletteBlendProgress = 0.0;
    initialized = true;
    Serial.println("Color palette initialized");
  }
  
  // Slowly change palette every 2 minutes with complex alternation
  if (currentTime - lastPaletteChange > PALETTE_CHANGE_INTERVAL) {
    sequenceCount++;
    
    // Complex alternation pattern:
    // - 40% chance: Stay in same color family, move to next palette
    // - 35% chance: Switch color family (blue<->green)
    // - 15% chance: Jump within same color family randomly
    // - 10% chance: Complete random jump (any palette)
    
    int randChoice = random(100);
    
    if (randChoice < 40) {
      // Stay in same color family, move to next palette
      if (isBlue) {
        targetPaletteIndex = (currentPaletteIndex + 1) % NUM_BLUE_PALETTES;
      } else {
        targetPaletteIndex = ((currentPaletteIndex + 1 - NUM_BLUE_PALETTES) % NUM_GREEN_PALETTES) + NUM_BLUE_PALETTES;
      }
    } else if (randChoice < 75) {
      // Switch color family
      isBlue = !isBlue;
      if (isBlue) {
        // Switch to blue - pick a random blue palette
        targetPaletteIndex = random(0, NUM_BLUE_PALETTES);
      } else {
        // Switch to green - pick a random green palette
        targetPaletteIndex = random(NUM_BLUE_PALETTES, NUM_PALETTES);
      }
    } else if (randChoice < 90) {
      // Jump randomly within same color family
      if (isBlue) {
        targetPaletteIndex = random(0, NUM_BLUE_PALETTES);
      } else {
        targetPaletteIndex = random(NUM_BLUE_PALETTES, NUM_PALETTES);
      }
      // Make sure it's different from current
      while (targetPaletteIndex == currentPaletteIndex) {
        if (isBlue) {
          targetPaletteIndex = random(0, NUM_BLUE_PALETTES);
        } else {
          targetPaletteIndex = random(NUM_BLUE_PALETTES, NUM_PALETTES);
        }
      }
    } else {
      // Complete random jump (any palette)
      targetPaletteIndex = random(0, NUM_PALETTES);
      // Update color family based on selected palette
      isBlue = (targetPaletteIndex < NUM_BLUE_PALETTES);
      // Make sure it's different from current
      while (targetPaletteIndex == currentPaletteIndex) {
        targetPaletteIndex = random(0, NUM_PALETTES);
        isBlue = (targetPaletteIndex < NUM_BLUE_PALETTES);
      }
    }
    
    paletteBlendProgress = 0.0;
    lastPaletteChange = currentTime;
    Serial.print("Transitioning to palette ");
    Serial.print(targetPaletteIndex);
    Serial.print(" (");
    Serial.print(isBlue ? "blue" : "green");
    Serial.println(")");
  }
  
  // Smoothly blend between palettes
  if (currentPaletteIndex != targetPaletteIndex) {
    paletteBlendProgress += PALETTE_TRANSITION_SPEED;
    if (paletteBlendProgress >= 1.0) {
      paletteBlendProgress = 1.0;
      currentPaletteIndex = targetPaletteIndex;
    }
  }
  
  // Animate palette offset for slow movement
  paletteOffset = (paletteOffset + 1) % 256;
  
  // Blend the two palettes for base background
  CRGBPalette16 currentPalette = palettes[currentPaletteIndex];
  CRGBPalette16 targetPalette = palettes[targetPaletteIndex];
  
  // Fill LEDs with base palette colors - CRITICAL: This must run every frame
  for(int i = 0; i < NUM_LEDS; i++) {
    int position = (i * 8 + paletteOffset) % 256;
    position = constrain(position, 0, 255);
    
    CRGB color1 = ColorFromPalette(currentPalette, position, 255, LINEARBLEND);
    CRGB color2 = ColorFromPalette(targetPalette, position, 255, LINEARBLEND);
    leds[i] = blend(color1, color2, (uint8_t)(paletteBlendProgress * 255));
  }
  
  // CRITICAL: Must call show() to display the LEDs
  FastLED.show();
  delay(30); // Smooth animation
}
