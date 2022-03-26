/**
 * Photobooth Buzzer Sketch v1.0
 * find out more: blog.altholtmann.com
 * by Timo Altholtmann
 * 
 * ***********************    General Notes    *****************************
 * - Uncomment means removing the 2 // in front of #define.
 * 
 **/
#include <Arduino.h>
// Debugging. If enabled, the sketch will print DEBUGGING states to the serial monitor
// with 115200 Baudrate
// Default: commented
#define DEBUG // Uncomment to output DEBUGGING messages

// Mac-Adress of the photobooth controller. This is the most important part and has to match the defined Mac-Adress in the 
// photobooth buzzer sketch otherwise a communication is not possible. Only needs to be changed, if there are other 
// photobooth controller in range.
// Default: { 0x5E, 0x0, 0x0, 0x0, 0x0, 0x1 }
#define BOOTH_MAC_ADDRESS { 0x5E, 0x0, 0x0, 0x0, 0x0, 0x1 }

// --------------------------------------------------------------------------------------------------------------------
// END Configuration - No need to change anything below - unless you know what you are doing!!
// --------------------------------------------------------------------------------------------------------------------
#ifdef ESP32
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
#else
  #include <ESP8266WiFi.h>
  #include <espnow.h>
#endif

#ifdef DEBUG    //Macros are usually in all capital letters.
#define DPRINT(...)    Serial.print(__VA_ARGS__)     //DPRINT is a macro, debug print
#define DPRINTLN(...)  Serial.println(__VA_ARGS__)   //DPRINTLN is a macro, debug print with new line
#else
#define DPRINT(...)     //now defines a blank line
#define DPRINTLN(...)   //now defines a blank line
#endif

enum COMMANDS 
{
    BUTTON_TRIGGERED,
    RESET_TRIGGERED,
};

typedef struct DATA_TO_BOOTH
{
    COMMANDS command;
    uint16_t payload;
} DATA_TO_BOOTH;

// Struct for outgoing data
DATA_TO_BOOTH dataToBooth = {COMMANDS::BUTTON_TRIGGERED,0};

// Programm vars
uint8_t boothMacAddress[] = BOOTH_MAC_ADDRESS;

#ifdef ESP32
    esp_now_peer_info_t peerInfo;
    RTC_DATA_ATTR uint16_t bootCount = 0;
#endif
// Interrupt pin
const gpio_num_t interruptPin = GPIO_NUM_25;

char *getMacStrFromAddress(uint8_t *address)
{
    static char macStr[18];
    // Copies the sender mac address to a string
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", address[0], address[1], address[2], address[3], address[4], address[5]);
    return macStr;
}

void goToSleep()
{
    DPRINTLN("Going to sleep now");
    #ifdef ESP32
        // Use external pin for wakeup
        esp_sleep_enable_ext0_wakeup(interruptPin, 0);
        esp_wifi_stop();
        // esp_sleep_enable_timer_wakeup(intveral_s * 1000000);
        esp_deep_sleep_start();
    #else
        ESP.deepSleep(0);
    #endif
}

void sendMessageToBooth()
{
    #ifdef ESP32
    // Send message via ESP-NOW
    esp_err_t result = esp_now_send(boothMacAddress, (uint8_t *)&dataToBooth, sizeof(dataToBooth));
    if (result != ESP_OK)
    {
        DPRINTLN("Error sending the data - Things wont work");
    }
    #else
        esp_now_send(boothMacAddress, (uint8_t *) &dataToBooth, sizeof(dataToBooth));
    #endif
}

void setupWifi()
{
  #ifdef ESP32
    WiFi.enableLongRange(true);
  #else 
    WiFi.disconnect();
    ESP.eraseConfig();
  #endif
    WiFi.mode(WIFI_STA);

  DPRINT("My Mac Address: ");
  DPRINTLN(WiFi.macAddress());

    //Init ESP-NOW
    if (esp_now_init() != 0)
    {
        DPRINTLN("Error initializing ESP-NOW. Things wont work");
        goToSleep();
        return;
    }
  
  #ifdef ESP32
    // Register mower esp32 as peer
    memcpy(peerInfo.peer_addr, boothMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    // Add peer
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        DPRINTLN("Failed to add peer. Things wont work");
        goToSleep();
    }
  #else 
    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_add_peer(boothMacAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  #endif
}

#ifdef ESP32
  void checkWakeUpReason()
  {
      esp_sleep_wakeup_cause_t wakeup_reason;
      wakeup_reason = esp_sleep_get_wakeup_cause();
      if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
      {
          DPRINTLN("Wake up by interrupt");
          dataToBooth.command = COMMANDS::BUTTON_TRIGGERED;
          sendMessageToBooth();
      }
  }
#endif

void setup()
{
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  setupWifi();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  #ifdef ESP32
    esp_deep_sleep_disable_rom_logging();
    bootCount++;
    DPRINT("BootCount: ");
    DPRINTLN(bootCount);

    pinMode(interruptPin, INPUT_PULLUP);
    checkWakeUpReason();
  #endif
}

void loop() {
    #ifndef ESP32
      sendMessageToBooth();
    #endif

    delay(10);

    goToSleep();
}
