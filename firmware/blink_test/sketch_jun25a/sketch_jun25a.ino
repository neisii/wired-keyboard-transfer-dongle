#include <Adafruit_DotStar.h>

#define DATA_PIN  40
#define CLOCK_PIN 39
#define NUM_LEDS  1

Adafruit_DotStar led(NUM_LEDS, DATA_PIN, CLOCK_PIN, DOTSTAR_BGR);

void setup() {
  led.begin();
  led.setBrightness(50);
}

void loop() {
  led.setPixelColor(0, 0xFF00FF); // 마젠타
  led.show();
  delay(500);
  led.setPixelColor(0, 0x000000); // 끄기
  led.show();
  delay(500);
}
