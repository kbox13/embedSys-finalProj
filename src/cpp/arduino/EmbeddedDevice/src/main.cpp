/*
 * Embedded Device: Event Scheduler System
 * 
 * Receives beat prediction events via MQTT and executes precise LED lighting
 * events using hardware timer interrupts on Arduino Nano ESP32.
 * 
 * Architecture:
 * - Core 0: Communication (WiFi, MQTT, SNTP)
 * - Core 1: Execution (Event scheduler, timer, ISR)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "mqtt_client.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "driver/gpio.h"
#include "wifi_config.h"  // WiFi credentials and MQTT config

// ============================================================================
// Configuration
// ============================================================================

// WiFi credentials (from wifi_config.h - macros are used directly)
// MQTT configuration (from wifi_config.h - macros are used directly)

// MQTT Topics
const char* TOPIC_EVENTS_SCHEDULE = "beat/events/schedule";
const char* TOPIC_TIME_SYNC = "beat/time/sync";
const char* TOPIC_COMMANDS = "beat/commands/all";

// SNTP configuration
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.nist.gov";
const char* TIMEZONE = "UTC";  // Use UTC for consistency

// Event queue configuration
#define MAX_EVENT_QUEUE_SIZE 50
#define EVENT_QUEUE_TIMEOUT_MS 1000

// LED flash duration (milliseconds)
#define LED_FLASH_DURATION_MS 150

// Hardware timer configuration
// Using ESP32 Arduino timer library (hw_timer)

// ============================================================================
// LED Pin Definitions
// ============================================================================

const int LED_BUILTIN_PIN = LED_BUILTIN;

const int LED_RED_PIN = LED_RED;      // RGB Red (GPIO 14)
const int LED_GREEN_PIN = LED_GREEN;  // RGB Green (GPIO 15)
const int LED_BLUE_PIN = LED_BLUE;    // RGB Blue (GPIO 16)


// ============================================================================
// Data Structures
// ============================================================================

struct ScheduledEvent {
    unsigned long execute_time_us;  // Microsecond precision
    bool red;
    bool green;
    bool blue;
    uint8_t event_id;
};

struct TimeSyncState {
    bool synced;
    time_t sync_epoch;
    unsigned long sync_micros;
    unsigned long time_offset_us;
};

// ============================================================================
// Global State
// ============================================================================

// Time synchronization
TimeSyncState timeSync = {false, 0, 0, 0};
SemaphoreHandle_t timeSyncMutex = NULL;

// Event queue (thread-safe)
QueueHandle_t eventQueue = NULL;
SemaphoreHandle_t eventQueueMutex = NULL;
ScheduledEvent eventList[MAX_EVENT_QUEUE_SIZE];
size_t eventCount = 0;

// Hardware timer
// Note: ESP32 Arduino uses hw_timer_t from ESP32 timer library
// We'll use the timer interrupt functionality
volatile ScheduledEvent* nextEvent = NULL;
volatile bool eventExecuted = false;
volatile unsigned long timerAlarmTime = 0;

// MQTT client
esp_mqtt_client_handle_t mqttClient = NULL;
bool mqttConnected = false;

// WiFi status
bool wifiConnected = false;

// ============================================================================
// LED Control Functions
// ============================================================================

void setLED(int pin, bool state) {
    // Regular LEDs: HIGH = ON, LOW = OFF
    digitalWrite(pin, state ? HIGH : LOW);
}

void setRGBLED(int pin, bool state) {
    // RGB LED is active-low: LOW = ON, HIGH = OFF
    digitalWrite(pin, state ? LOW : HIGH);
}

void initLEDs() {
    pinMode(LED_BUILTIN_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN, OUTPUT);
    
    // Turn off all LEDs initially
    setLED(LED_BUILTIN_PIN, false);
    setRGBLED(LED_RED_PIN, false);
    setRGBLED(LED_GREEN_PIN, false);
    setRGBLED(LED_BLUE_PIN, false);
}

// ============================================================================
// Time Synchronization
// ============================================================================

void timeSyncNotificationCallback(struct timeval *tv) {
    // Called when SNTP syncs time
    if (xSemaphoreTake(timeSyncMutex, portMAX_DELAY)) {
        timeSync.sync_epoch = tv->tv_sec;
        timeSync.sync_micros = micros();
        timeSync.synced = true;
        timeSync.time_offset_us = 0;  // Will be calculated on first event
        xSemaphoreGive(timeSyncMutex);
        Serial.println("Time synchronized via SNTP");
    }
}

void initSNTP() {
    Serial.println("Initializing SNTP...");
    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, NTP_SERVER1);
    sntp_setservername(1, NTP_SERVER2);
    sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);
    sntp_init();
    
    // Set timezone (UTC for consistency)
    setenv("TZ", TIMEZONE, 1);
    tzset();
    
    // Wait for initial sync
    // Note: SNTP sync is asynchronous - the callback will fire when complete
    Serial.println("Waiting for SNTP time sync...");
    int retries = 0;
    
    // Wait for either status to complete OR callback to set synced flag
    while (retries < 60) {  // Wait up to 30 seconds (60 * 500ms)
        // Check if callback has already set the synced flag
        if (xSemaphoreTake(timeSyncMutex, 0) == pdTRUE) {
            bool synced = timeSync.synced;
            xSemaphoreGive(timeSyncMutex);
            if (synced) {
                Serial.println("Time sync successful! (via callback)");
                return;  // Callback already handled everything
            }
        }
        
        // Also check SNTP status
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            
            if (xSemaphoreTake(timeSyncMutex, portMAX_DELAY)) {
                timeSync.sync_epoch = tv.tv_sec;
                timeSync.sync_micros = micros();
                timeSync.synced = true;
                timeSync.time_offset_us = 0;
                xSemaphoreGive(timeSyncMutex);
            }
            
            Serial.println("Time sync successful! (via status check)");
            return;
        }
        
        delay(500);
        retries++;
    }
    
    // Final check - callback might have fired while we were waiting
    if (xSemaphoreTake(timeSyncMutex, 0) == pdTRUE) {
        bool synced = timeSync.synced;
        xSemaphoreGive(timeSyncMutex);
        if (synced) {
            Serial.println("Time sync successful! (callback fired during wait)");
            return;
        }
    }
    
    Serial.println("Time sync timeout - will continue and retry in background");
    Serial.println("SNTP will automatically retry periodically");
}

unsigned long unixTimeToMicros(time_t unixTime, long microseconds) {
    if (!timeSync.synced) {
        Serial.println("WARNING: Time not synced, using relative timing");
        return micros() + (microseconds / 1000);  // Fallback to relative
    }
    
    if (xSemaphoreTake(timeSyncMutex, portMAX_DELAY)) {
        // Get current Unix time
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        // Calculate difference in seconds and microseconds
        long diffSeconds = unixTime - tv.tv_sec;
        long diffMicros = microseconds - tv.tv_usec;
        
        // Convert to micros() equivalent
        unsigned long currentMicros = micros();
        unsigned long executeTime = currentMicros + (diffSeconds * 1000000L) + diffMicros;
        
        xSemaphoreGive(timeSyncMutex);
        return executeTime;
    }
    
    return micros();  // Fallback
}

// ============================================================================
// Event Queue Management
// ============================================================================

bool insertEventSorted(ScheduledEvent event) {
    if (xSemaphoreTake(eventQueueMutex, EVENT_QUEUE_TIMEOUT_MS / portTICK_PERIOD_MS) != pdTRUE) {
        Serial.println("ERROR: Failed to acquire event queue mutex");
        return false;
    }
    
    if (eventCount >= MAX_EVENT_QUEUE_SIZE) {
        Serial.println("WARNING: Event queue full, rejecting event");
        xSemaphoreGive(eventQueueMutex);
        return false;
    }
    
    // Insert event in sorted order (by execute_time_us)
    size_t insertIndex = eventCount;
    for (size_t i = 0; i < eventCount; i++) {
        if (event.execute_time_us < eventList[i].execute_time_us) {
            insertIndex = i;
            break;
        }
    }
    
    // Shift events to make room
    for (size_t i = eventCount; i > insertIndex; i--) {
        eventList[i] = eventList[i - 1];
    }
    
    // Insert new event
    eventList[insertIndex] = event;
    eventCount++;
    
    xSemaphoreGive(eventQueueMutex);
    return true;
}

bool removeEvent(size_t index) {
    if (xSemaphoreTake(eventQueueMutex, EVENT_QUEUE_TIMEOUT_MS / portTICK_PERIOD_MS) != pdTRUE) {
        return false;
    }
    
    if (index >= eventCount) {
        xSemaphoreGive(eventQueueMutex);
        return false;
    }
    
    // Shift events to fill gap
    for (size_t i = index; i < eventCount - 1; i++) {
        eventList[i] = eventList[i + 1];
    }
    
    eventCount--;
    xSemaphoreGive(eventQueueMutex);
    return true;
}

ScheduledEvent* peekNextEvent() {
    if (eventCount == 0) {
        return NULL;
    }
    return &eventList[0];
}

// ============================================================================
// Hardware Timer and ISR
// ============================================================================

// For Arduino ESP32, we'll use a simpler approach: check timer in scheduler task
// For more precise timing, we could use hardware timer interrupts, but Arduino
// ESP32 timer API is different. For now, we'll use micros() polling with
// hardware timer checking in the scheduler task.

void scheduleLEDTurnOff(unsigned long currentMicros) {
    // Schedule a turn-off event after flash duration
    ScheduledEvent turnOffEvent;
    turnOffEvent.execute_time_us = currentMicros + (LED_FLASH_DURATION_MS * 1000UL);
    turnOffEvent.red = false;
    turnOffEvent.green = false;
    turnOffEvent.blue = false;
    turnOffEvent.event_id = 0;  // Special ID for turn-off events
    
    if (insertEventSorted(turnOffEvent)) {
        // Turn-off event scheduled successfully
    }
}

void executeEvent(ScheduledEvent* event) {
    if (event == NULL) {
        return;
    }
    
    unsigned long currentMicros = micros();
    
    // Check if this is a turn-off event (all LEDs off)
    // if (!event->red && !event->green && !event->blue) {
    //     // Turn off all LEDs using digitalWrite
    //     Serial.printf("Turning off all LEDs (pins: R=%d, G=%d, B=%d)\n", 
    //                  LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN);
    //     setRGBLED(LED_RED_PIN, false);
    //     setRGBLED(LED_GREEN_PIN, false);
    //     setRGBLED(LED_BLUE_PIN, false);
    //     return;  // Don't schedule another turn-off for turn-off events
    // }
    
    // // Turn off all LEDs first
    // setRGBLED(LED_RED_PIN, false);
    // setRGBLED(LED_GREEN_PIN, false);
    // setRGBLED(LED_BLUE_PIN, false);
    
    // Then turn on only the specified LEDs
    Serial.printf("Setting LEDs: R=%d (pin %d), G=%d (pin %d), B=%d (pin %d)\n",
                 event->red, LED_RED_PIN, event->green, LED_GREEN_PIN, 
                 event->blue, LED_BLUE_PIN);
    setRGBLED(LED_RED_PIN, event->red);
    setRGBLED(LED_GREEN_PIN, event->green);
    setRGBLED(LED_BLUE_PIN, event->blue);
    
    // Schedule automatic turn-off after flash duration if led turned on
    if (event->red || event->green || event->blue) {
        scheduleLEDTurnOff(currentMicros);
    }
    
    Serial.printf("Event executed: ID=%d, RGB=(%d,%d,%d)\n",
                 event->event_id, event->red, event->green, event->blue);
}

void initHardwareTimer() {
    // Initialize GPIO pins for fast access
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << LED_RED_PIN) | (1ULL << LED_GREEN_PIN) | (1ULL << LED_BLUE_PIN));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    
    Serial.println("GPIO configured for fast LED control");
}

void configureTimerForEvent(ScheduledEvent* event) {
    if (event == NULL) {
        return;
    }
    
    // Simply set the next event - scheduler will check timing
    nextEvent = event;
    timerAlarmTime = event->execute_time_us;
}

// ============================================================================
// MQTT Message Handling
// ============================================================================

void scheduleSingleEvent(JsonObject& event) {
    ScheduledEvent scheduledEvent;
    
    time_t unix_time = event["unix_time"] | 0;
    long microseconds = event["microseconds"] | 0;
    scheduledEvent.red = (event["r"].as<int>() != 0);
    scheduledEvent.green = (event["g"].as<int>() != 0);
    scheduledEvent.blue = (event["b"].as<int>() != 0);
    scheduledEvent.event_id = event["event_id"] | 0;
    
    // Convert Unix timestamp to micros() equivalent
    scheduledEvent.execute_time_us = unixTimeToMicros(unix_time, microseconds);
    
    // Enqueue event
    if (insertEventSorted(scheduledEvent)) {
        Serial.printf("Event scheduled: ID=%d, time=%lu, RGB=(%d,%d,%d)\n",
                     scheduledEvent.event_id,
                     scheduledEvent.execute_time_us,
                     scheduledEvent.red, scheduledEvent.green, scheduledEvent.blue);
    } else {
        Serial.println("Failed to schedule event");
    }
}

void handleScheduleEvent(JsonDocument& doc) {
    // Check if batch or single event (using new API)
    if (doc["events"].is<JsonArray>()) {
        // Batch events
        JsonArray events = doc["events"];
        for (JsonObject event : events) {
            scheduleSingleEvent(event);
        }
    } else {
        // Single event - get JsonObject reference
        JsonObject eventObj = doc.as<JsonObject>();
        scheduleSingleEvent(eventObj);
    }
}

void handleTimeSync(JsonDocument& doc) {
    time_t unix_time = doc["unix_time"] | 0;
    long microseconds = doc["microseconds"] | 0;
    
    // Update time sync offset
    if (xSemaphoreTake(timeSyncMutex, portMAX_DELAY)) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        timeSync.sync_epoch = tv.tv_sec;
        timeSync.sync_micros = micros();
        timeSync.synced = true;
        
        xSemaphoreGive(timeSyncMutex);
    }
    
    Serial.println("Time sync updated via MQTT");
}

void handleMQTTMessage(const char* topic, int topic_len, const char* data, int data_len) {
    // Convert to null-terminated strings
    char topic_str[topic_len + 1];
    char data_str[data_len + 1];
    memcpy(topic_str, topic, topic_len);
    topic_str[topic_len] = '\0';
    memcpy(data_str, data, data_len);
    data_str[data_len] = '\0';
    
    Serial.printf("MQTT message received: topic=%s, len=%d\n", topic_str, data_len);
    
    // Parse JSON (using JsonDocument for v7 - replaces deprecated StaticJsonDocument)
    // ArduinoJson v7: JsonDocument uses dynamic allocation, which is fine for our small messages
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data_str);
    
    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return;
    }
    
    // Route to appropriate handler
    if (strcmp(topic_str, TOPIC_EVENTS_SCHEDULE) == 0) {
        handleScheduleEvent(doc);
    } else if (strcmp(topic_str, TOPIC_TIME_SYNC) == 0) {
        handleTimeSync(doc);
    } else if (strcmp(topic_str, TOPIC_COMMANDS) == 0) {
        Serial.println("Command received (not implemented)");
        // Future: handle commands
    }
}

// ============================================================================
// MQTT Event Handler
// ============================================================================

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, 
                               int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = mqttClient;
    
    if (event == NULL && event_id != MQTT_EVENT_CONNECTED && event_id != MQTT_EVENT_DISCONNECTED) {
        return;  // Some events don't have data
    }
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            Serial.println("MQTT Connected");
            mqttConnected = true;
            
            // Subscribe to topics with QoS 1
            if (client != NULL) {
                esp_mqtt_client_subscribe(client, TOPIC_EVENTS_SCHEDULE, 1);
                esp_mqtt_client_subscribe(client, TOPIC_TIME_SYNC, 1);
                esp_mqtt_client_subscribe(client, TOPIC_COMMANDS, 1);
            }
            
            Serial.println("Subscribed to MQTT topics");
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("MQTT Disconnected");
            mqttConnected = false;
            break;
            
        case MQTT_EVENT_DATA:
            if (event != NULL) {
                handleMQTTMessage(
                    event->topic,
                    event->topic_len,
                    event->data,
                    event->data_len
                );
            }
            break;
            
        case MQTT_EVENT_ERROR:
            Serial.println("MQTT Error");
            break;
            
        default:
            break;
    }
}

// ============================================================================
// FreeRTOS Tasks
// ============================================================================

// Core 0: MQTT Client Task
void mqttClientTask(void *parameter) {
    Serial.println("MQTT Client Task started on Core 0");
    
    while (true) {
        if (wifiConnected && !mqttConnected) {
            // Initialize MQTT client (ESP32 Arduino uses URI-based config)
            char mqtt_uri[128];
            snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtt://%s:%d", MQTT_BROKER, MQTT_PORT);
            
            esp_mqtt_client_config_t mqtt_cfg = {};
            mqtt_cfg.uri = mqtt_uri;
            mqtt_cfg.client_id = MQTT_CLIENT_ID;
            
            mqttClient = esp_mqtt_client_init(&mqtt_cfg);
            
            // Register event handler (correct signature for ESP32 Arduino)
            esp_mqtt_client_register_event(
                mqttClient,
                MQTT_EVENT_ANY,
                mqtt_event_handler,
                NULL
            );
            
            esp_mqtt_client_start(mqttClient);
            
            Serial.println("MQTT client started");
        }
        
        // Small delay
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Core 1: Event Scheduler Task
void eventSchedulerTask(void *parameter) {
    Serial.println("Event Scheduler Task started on Core 1");
    
    while (true) {
        unsigned long now = micros();
        bool eventExecuted = false;
        
        // 1. Check if current event should be executed
        if (nextEvent != NULL) {
            if (now >= timerAlarmTime) {
                // Execute event (cast away volatile for function call)
                ScheduledEvent* event = const_cast<ScheduledEvent*>(nextEvent);
                executeEvent(event);
                eventExecuted = true;
                
                // Remove executed event
                if (eventCount > 0) {
                    removeEvent(0);
                }
                nextEvent = NULL;
                timerAlarmTime = 0;
            }
        }
        
        // 2. Check for past-due events (execute immediately if missed)
        if (eventCount > 0) {
            ScheduledEvent* next = peekNextEvent();
            if (next != NULL && next->execute_time_us <= now) {
                // Event is past due, execute immediately
                executeEvent(next);
                removeEvent(0);
                eventExecuted = true;
                nextEvent = NULL;
                timerAlarmTime = 0;
            }
        }
        
        // 3. Check for new events and configure timer (if no event was just executed)
        if (!eventExecuted && eventCount > 0) {
            if (nextEvent == NULL) {
                // No event scheduled, get the next one
                ScheduledEvent* next = peekNextEvent();
                if (next != NULL) {
                    configureTimerForEvent(next);
                }
            } else {
                // Check if there's an earlier event
                ScheduledEvent* next = peekNextEvent();
                if (next != NULL && next->execute_time_us < timerAlarmTime) {
                    configureTimerForEvent(next);
                }
            }
        }
        
        // Small delay - reduced for better responsiveness
        // Use yield to allow other tasks to run, but check more frequently
        vTaskDelay(1 / portTICK_PERIOD_MS);  // 1ms delay (minimum for FreeRTOS)
    }
}

// ============================================================================
// WiFi Setup
// ============================================================================

void setupWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("");
        Serial.println("WiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("");
        Serial.println("WiFi connection failed");
    }
}

// ============================================================================
// Setup and Main Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== Embedded Device: Event Scheduler System ===");
    
    // Initialize LEDs
    initLEDs();
    Serial.println("LEDs initialized");
    
    // Create synchronization primitives
    timeSyncMutex = xSemaphoreCreateMutex();
    eventQueueMutex = xSemaphoreCreateMutex();
    
    if (timeSyncMutex == NULL || eventQueueMutex == NULL) {
        Serial.println("ERROR: Failed to create mutexes");
        return;
    }
    
    // Initialize hardware timer
    initHardwareTimer();
    
    // Test LEDs to verify functionality
    Serial.println("Testing LEDs...");
    delay(1000);
    setRGBLED(LED_RED_PIN, true);
    delay(1000);
    setRGBLED(LED_RED_PIN, false);
    setRGBLED(LED_GREEN_PIN, true);
    delay(1000);
    setRGBLED(LED_GREEN_PIN, false);
    setRGBLED(LED_BLUE_PIN, true);
    delay(1000);
    setRGBLED(LED_BLUE_PIN, false);
    Serial.println("LED test complete");
    
    // Setup WiFi
    setupWiFi();
    
    // Initialize SNTP
    if (wifiConnected) {
        initSNTP();
    }
    
    // Create FreeRTOS tasks
    // Core 0: MQTT Client Task
    xTaskCreatePinnedToCore(
        mqttClientTask,
        "MQTTClient",
        8192,  // Stack size
        NULL,
        1,     // Priority
        NULL,
        0      // Core 0
    );
    
    // Core 1: Event Scheduler Task
    xTaskCreatePinnedToCore(
        eventSchedulerTask,
        "EventScheduler",
        4096,  // Stack size
        NULL,
        2,     // Higher priority for timing-critical
        NULL,
        1      // Core 1
    );
    
    Serial.println("System initialized - tasks created");
    Serial.println("Waiting for MQTT connection and events...");
    
    // Delete setup task (tasks will handle everything)
    vTaskDelete(NULL);
}

void loop() {
    // This should never execute since setup() deletes itself
    // But keep it as a safety net
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

