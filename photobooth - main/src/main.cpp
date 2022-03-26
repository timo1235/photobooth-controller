/**
 * Photobooth controller sketch v1.0
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
// Every number can be changed to a value between 0-9. For example {0, 5, 0, 9, 0, 1}. That should be enough possible
// combinations to find a unique one
// Default: { 0x5, 0x0, 0x0, 0x0, 0x0, 0x1 }
#define BOOTH_MAC_ADDRESS { 0x5E, 0x0, 0x0, 0x0, 0x0, 0x1 }

// Should it be possible to abort the sequence with another button press
#define ABORT_ACTIVE true

// --------------------------------------------------------------------------------------------------------------------
// END Configuration - No need to change anything below - unless you know what you are doing!!
// --------------------------------------------------------------------------------------------------------------------

// Includes
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <EEPROM.h>
// WS2812 library
#include <Adafruit_NeoPixel.h>
// Display library
#include <U8x8lib.h>
// Debouncing library
#include <Bounce2.h>

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

uint8_t newMACAddress[] = BOOTH_MAC_ADDRESS;

// Struct for incoming data
DATA_TO_BOOTH dataToBooth;

const byte colorSwitchPin = 26;
const byte triggerPin = 27;
const byte triggerLedPin = 14;
const byte shutterPin = 12;
const byte ws2813Pin = 13;
const byte ws2813LedCount = 10; 
byte ledClockBrightness = 50;
Adafruit_NeoPixel ledClock = Adafruit_NeoPixel(ws2813LedCount, ws2813Pin, NEO_GRB + NEO_KHZ800);
TaskHandle_t SecondCore;
U8X8_SSD1306_128X64_NONAME_4W_HW_SPI u8x8(/* cs=*/5, /* dc=*/ 17, /* reset=*/ 16);
Bounce buttonTrigger = Bounce();
Bounce buttonColor = Bounce();

struct RGB {
  char color[15];
  uint32_t colorCode;
};

RGB colors[] = {
  {"Aus", ledClock.Color(1,1,1)},
  {"Weiss", ledClock.Color(255,255,255)},
  {"Rot", ledClock.Color(255,0,0)},
  {"Gruen", ledClock.Color(0,255,0)},
  {"Blau", ledClock.Color(0,0,255)},
  {"Pink", ledClock.Color(253,35,253)},
  {"Gelb", ledClock.Color(248,255,27)}
};

uint8_t sequenceTimes[] = { 0, 2, 4, 6, 8, 10, 12 };

uint8_t currentClockColor;
bool shouldStartSequence = false;
uint32_t sequenceStartTime_MS = 0;
bool shouldAbortSequence = false;
uint32_t colorSwitchButtonLastPressed_MS = millis();
uint8_t currentSequenceTime;

#ifdef DEBUG    //Macros are usually in all capital letters.
#define DPRINT(...)    Serial.print(__VA_ARGS__)     //DPRINT is a macro, debug print
#define DPRINTLN(...)  Serial.println(__VA_ARGS__)   //DPRINTLN is a macro, debug print with new line
#else
#define DPRINT(...)     //now defines a blank line
#define DPRINTLN(...)   //now defines a blank line
#endif

void SecondCoreCode(void *pvParameters);

bool shouldAbort() {
  if(shouldAbortSequence) {
    DPRINTLN("shouldAbort: Indicate abort sequence with red blinks");
    for (uint8_t i = 0; i < 2; i++) {
      ledClock.fill(colors[2].colorCode,0,ws2813LedCount);
      ledClock.show();
      digitalWrite(triggerLedPin, LOW);
      delay(200);
      ledClock.fill(colors[0].colorCode,0,ws2813LedCount);
      ledClock.show();
      digitalWrite(triggerLedPin, HIGH);
      delay(200);
    }
    shouldStartSequence = false;
    shouldAbortSequence = false;
    sequenceStartTime_MS = 0;
    return true;
  }

  return false;
}

void takePicture() {
  DPRINTLN("Taking a picture!");
  digitalWrite(shutterPin, HIGH);
  delay(100);
  digitalWrite(shutterPin, LOW);
}

void ledSequence(int waitTime)
{
  DPRINT("ledSequence: Start - waitTime: ");
  DPRINTLN(waitTime);
  // blink leds if there is enough time
  if (waitTime > 1000) {
    DPRINTLN("ledSequence: Blink Leds before the sequence");
    for (uint8_t i = 0; i < 3; i++) {
      ledClock.fill(colors[currentClockColor].colorCode,0,ws2813LedCount);
      ledClock.show();
      digitalWrite(triggerLedPin, LOW);
      delay(100);
      ledClock.fill(colors[0].colorCode,0,ws2813LedCount);
      ledClock.show();
      digitalWrite(triggerLedPin, HIGH);
      delay(100);
      waitTime -= 100 * 2;
    }
  }
  // start the actual led sequence
  DPRINTLN("ledSequence: Led ring sequence start");
  uint16_t waitTimeStep = waitTime / ws2813LedCount;
  for (int i = ws2813LedCount-1; i >= 0; i--) {
    // Check if the sequence should be aborted
    if(shouldAbort() && ABORT_ACTIVE) return;
    ledClock.setPixelColor(i, colors[currentClockColor].colorCode);
    ledClock.show();
    // dont wait when the last led turns on - so the picture is taken faster
    if (i != 0) {
      delay(waitTimeStep);
    }
  }
  ledClock.fill(colors[1].colorCode,0,ws2813LedCount);
  ledClock.show();
  takePicture();
  delay(1000);
  ledClock.fill(colors[0].colorCode,0,ws2813LedCount);
  ledClock.show();
}

char *getMacStrFromAddress(uint8_t *address)
{
  static char macStr[18];
  // Copies the sender mac address to a string
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", address[0], address[1], address[2], address[3], address[4], address[5]);
  return macStr;
}

void processButtonTriggered() {
  // Abort when sequence has already startet. But only if some time went by to prevent fast double clicks
  if(!shouldAbortSequence && sequenceStartTime_MS != 0 && (millis() - sequenceStartTime_MS) > 1000) {
    DPRINTLN("OnDataRecv: Should abort sequence");
    shouldAbortSequence = true;
  } else if (sequenceStartTime_MS == 0 && !shouldStartSequence && !shouldAbortSequence) {
    DPRINTLN("OnDataRecv: Should start the sequence");
    shouldStartSequence = true;
  }
}

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t *address, const uint8_t *incomingData, int len)
{
  DPRINTLN("OnDataRecv: Got Message");
  memcpy(&dataToBooth, incomingData, sizeof(dataToBooth));
  if(dataToBooth.command == COMMANDS::BUTTON_TRIGGERED) {
    processButtonTriggered();
  }
}

void setUpWifi()
{
  WiFi.enableLongRange(true);
  WiFi.mode(WIFI_STA);

  DPRINT("My old Mac Address: ");
  DPRINTLN(WiFi.macAddress());

  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_mac(WIFI_IF_STA, &newMACAddress[0]);

  DPRINT("My new Mac Address: ");
  DPRINTLN(WiFi.macAddress());

    if (esp_now_init() != ESP_OK)
    {
      DPRINTLN("Error initializing ESP-NOW. Things wont work");
      return;
    }

    esp_now_register_recv_cb(OnDataRecv);
}

void updateDisplay() {
  u8x8.setFont(u8x8_font_profont29_2x3_f);
  u8x8.clearDisplay();
  u8x8.drawString(0,0,colors[currentClockColor].color);
  u8x8.setCursor(0,5);
  u8x8.print(sequenceTimes[currentSequenceTime]);
  u8x8.print(" Sec.");

}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  EEPROM.begin(2);

  currentClockColor = EEPROM.read(0);
  // Fallback
  if(currentClockColor < 0 || currentClockColor > sizeof(colors) / sizeof(RGB)) currentClockColor = 4;
  currentSequenceTime = EEPROM.read(1);
  // Fallback
  if(currentSequenceTime < 0 || currentSequenceTime > sizeof(sequenceTimes) / sizeof(uint8_t)) currentSequenceTime = 3;

  buttonTrigger.attach(triggerPin, INPUT_PULLUP);
  buttonTrigger.interval(5); // interval in ms
  buttonColor.attach(colorSwitchPin, INPUT_PULLUP);
  buttonColor.interval(5); // interval in ms

  pinMode(shutterPin, OUTPUT);
  digitalWrite(shutterPin, LOW);
  pinMode(triggerLedPin, OUTPUT);
  digitalWrite(triggerLedPin, HIGH);

  ledClock.begin();
  ledClock.setBrightness(ledClockBrightness);
  ledClock.fill(colors[0].colorCode,0,ws2813LedCount);
  ledClock.show();

  setUpWifi();

  u8x8.begin();
  u8x8.setFlipMode(1);
  updateDisplay();

  // Setup dual core
  xTaskCreatePinnedToCore(
    SecondCoreCode, /* Task function. */
    "SecondCore",   /* name of task. */
    10000,           /* Stack size of task */
    NULL,            /* parameter of the task */
    1,               /* priority of the task */
    &SecondCore,    /* Task handle to keep track of created task */
    0);

  DPRINTLN("setup: done!");
}

void loop() {
  buttonTrigger.update();
  buttonColor.update();

  // ********************
  // buttonTrigger
  // ********************
  // Short press handling
  if (buttonTrigger.rose())
  {
    processButtonTriggered();
  }

  // ********************
  // buttonColor
  // ********************
  // Short press handling
  if (buttonColor.rose())
  {
      if (buttonColor.previousDuration() < 1000)
      {
        currentClockColor++;
        if(currentClockColor == sizeof(colors) / sizeof(RGB)) currentClockColor = 1;
        updateDisplay();
        EEPROM.write(0, currentClockColor);
        EEPROM.commit();
      } 
  }
  // long press handling - reset postion
  if (buttonColor.read() == LOW)
  {
    if (buttonColor.currentDuration() > 1000)
    {
      while(buttonColor.read() == LOW) {
        currentSequenceTime++;
        if(currentSequenceTime == sizeof(sequenceTimes) / sizeof(uint8_t)) currentSequenceTime = 0;
        updateDisplay();
        for (int i = 0; i < 100; i++)
        {
          buttonColor.update();
          delay(10);
        }
      }
      EEPROM.write(1, currentSequenceTime);
      EEPROM.commit();
    }
  }

  delay(1);
}

void SecondCoreCode(void *pvParameters)
{
  // displayInit();

  for (;;)
  {
    // updateDisplay();
    if(shouldStartSequence) {
      shouldStartSequence = false;
      sequenceStartTime_MS = millis();
      ledSequence(sequenceTimes[currentSequenceTime] * 1000);
      shouldStartSequence = false;
      sequenceStartTime_MS = 0;
      shouldAbortSequence = false;
    }
    vTaskDelay(1);
  }
}