# firmware

Planck-Wireless 펌웨어 (LILYGO T-Dongle-S3, ESP32-S3).

| 폴더 | 설명 |
|---|---|
| `blink_test/sketch_jun25a` | 보드 양품 확인용 LED 점멸 스케치 (절차: `../T-Dongle-S3_양품확인.md`) |
| `planck_bridge` | USB 키보드 → BLE HID 키보드 브리지 |

## planck_bridge 기능

유선 USB 키보드(QMK)의 입력을 받아 BLE HID 키보드로 그대로 전달한다.

**USB Host (입력 수신)**
- 연결된 장치의 설정 디스크립터에서 Boot Keyboard 인터페이스(class 3 / subclass 1 / protocol 1)와 인터럽트 IN 엔드포인트를 찾는다. 키보드가 아니면 무시한다.
- 인터페이스 점유 후 `SET_PROTOCOL(Boot)`, `SET_IDLE(0)`를 보내고 8바이트 리포트를 계속 수신한다.
- 키보드를 뽑으면 정리하고 "모든 키 해제" 리포트를 보낸다. 다시 꽂으면 자동으로 재연결한다.

**BLE HID (전송)**
- BLE HID 키보드(이름 `Planck-BLE`)로 동작한다. Just Works 페어링, 본딩 저장.
- 받은 8바이트 리포트를 해석 없이 그대로 전달한다 (QMK 레이어/매크로 투명).
- 연결이 끊기면 자동으로 재광고한다.
- USB 태스크와 BLE 전송은 큐(32개)로 분리되어 있다.

**상태 LED (APA102, DIN GPIO 40 / CLK GPIO 39)**
- 광고 중: 파란색 0.5초 점멸
- 연결됨: 파란색 점등

**BOOT 버튼 (GPIO0)**
- 3초 이상 누르면 저장된 본딩을 모두 삭제하고 연결을 끊는다. 빨간색 점멸 후 재광고. 새 기기와 페어링할 때 사용한다.

## 빌드 설정 (Arduino IDE 2.x)

- ESP32 core by Espressif 3.3.0 이상
- 라이브러리: Adafruit DotStar (BLE는 코어 내장 라이브러리 사용)
- 보드 옵션은 `../T-Dongle-S3_양품확인.md`와 같되 아래 두 항목이 다르다.

| 항목 | 값 | 이유 |
|---|---|---|
| USB Mode | USB-OTG (TinyUSB) | Hardware CDC and JTAG는 USB Host와 PHY 충돌 |
| USB CDC On Boot | Disabled | 시리얼 로그 사용 불가, 디버그는 LED로 확인 |

## 제약

- Boot Protocol 사용: 6키 동시 입력(6KRO)까지만 지원. 미디어키/마우스키 등 Boot Protocol 밖의 리포트는 전달되지 않는다.
- 호스트(맥북)의 CapsLock 등 LED 상태를 키보드로 돌려보내지 않는다.
- 보드가 키보드에 5V(VBUS)를 공급하지 못한다. 외부 승압 모듈이 필요하다 (`../기획.md` 참고).

## 상태

- 컴파일과 실기기 테스트를 하지 않았다. 빌드 에러나 동작 문제가 있을 수 있다.
