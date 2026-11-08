# LILYGO T-Dongle-S3 양품 확인 가이드

## 환경

- Arduino IDE 2.x
- ESP32 core by Espressif Systems 3.3.0 이상
- Adafruit DotStar 라이브러리

---

## 1. Arduino IDE 환경 세팅

### ESP32 보드 패키지 설치
`파일 > 환경설정 > 추가 보드 관리자 URL`에 추가:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
`툴 > 보드 > 보드 매니저`에서 **esp32 by Espressif Systems** 설치

### Adafruit DotStar 라이브러리 설치
좌측 사이드바 책 모양 아이콘 (단축키 `⌘ + Shift + I`) > `DotStar` 검색 > **Adafruit DotStar** 설치

---

## 2. 보드 설정 (Tools 메뉴)

| 항목 | 값 |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enable |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80MHz |
| Flash Size | **16MB(128Mb)** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| PSRAM | Disabled |
| USB Mode | **Hardware CDC and JTAG** |
| Upload Speed | 921600 |
| Programmer | Esptool |

---

## 3. 보드 연결

USB 연결 후 `툴 > 포트`에서 `/dev/cu.usbmodem****` 선택

> 포트가 안 잡히면 **BOOT 버튼 누른 채로** USB 연결 후 포트 메뉴 재확인

---

## 4. 테스트 스케치

T-Dongle-S3의 내장 LED는 **APA102** 칩 (SPI 2핀 방식, NeoPixel/WS2812과 다름)

- RGB DIN: GPIO 40
- RGB CLK: GPIO 39
- 색상 순서: BGR

```cpp
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
```

---

## 5. 양품 판정 기준

| 확인 항목 | 정상 |
|---|---|
| 포트 인식 | `/dev/cu.usbmodem****` 포트 노출 |
| 업로드 | `Hash of data verified.` 메시지 출력 |
| LED 동작 | 마젠타 0.5초 간격 점멸 |

3개 모두 통과 시 **양품**.

---

## 참고

- [공식 문서](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/docs/en/t-dongle-s3/REAMDE.MD)
- [APA102 데이터시트](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/datasheet/APA102%202020%20256%206A.pdf)
