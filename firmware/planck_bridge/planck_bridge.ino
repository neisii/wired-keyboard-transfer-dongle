// Planck-Wireless: USB 키보드(QMK) -> BLE HID 키보드 브리지
// 대상 보드: LILYGO T-Dongle-S3 (ESP32-S3)
//
// 동작
//   1) ESP32-S3 USB Host로 키보드의 Boot Protocol(8바이트) 리포트를 수신
//   2) 리포트를 해석 없이 그대로 BLE HID 입력 리포트로 전달 (QMK 투명성)
//   3) APA102 LED로 상태 표시: 광고 중 = 파랑 0.5초 점멸, 연결됨 = 파랑 점등
//   4) BOOT 버튼(GPIO0)을 3초 이상 누르면 저장된 본딩 삭제 후 재광고
//
// Arduino IDE 설정 (양품확인 문서와 다른 부분)
//   - USB Mode: "USB-OTG (TinyUSB)"   (Hardware CDC and JTAG는 USB Host와 PHY 충돌)
//   - USB CDC On Boot: Disabled       (시리얼 로그 불가, 디버그는 LED로 확인)
//   - 나머지(Flash 16MB, Partition 16M 등)는 동일
//   - 필요 라이브러리: Adafruit DotStar (BLE는 ESP32 코어 내장 라이브러리 사용)

#include <Adafruit_DotStar.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <HIDTypes.h>
#include <esp_gap_ble_api.h>

extern "C" {
#include "usb/usb_host.h"
}

// ---------------------------------------------------------------- 설정
#define LED_DATA_PIN   40
#define LED_CLOCK_PIN  39
#define BOOT_BTN_PIN   0
#define BLE_NAME       "Planck-BLE"
#define REPORT_LEN     8
#define CLEAR_BOND_MS  3000

// ---------------------------------------------------------------- LED
static Adafruit_DotStar led(1, LED_DATA_PIN, LED_CLOCK_PIN, DOTSTAR_BGR);

static void setLed(bool on, uint8_t r = 0, uint8_t g = 0, uint8_t b = 255) {
  led.setPixelColor(0, on ? r : 0, on ? g : 0, on ? b : 0);
  led.show();
}

// ---------------------------------------------------------------- BLE
static const uint8_t kReportMap[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,       // Keyboard, Report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02,                   // modifiers
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                   // reserved
  0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02, // LEDs (out)
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,                   // LED padding
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
  0x95, 0x06, 0x75, 0x08, 0x81, 0x00,                   // 6 keys
  0xC0
};

static BLEServer*         g_server = nullptr;
static BLEHIDDevice*      g_hid    = nullptr;
static BLECharacteristic* g_input  = nullptr;
static volatile bool      g_bleConnected = false;
static QueueHandle_t      g_reportQ;   // USB task -> loop()

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override    { g_bleConnected = true; }
  void onDisconnect(BLEServer* s) override {
    g_bleConnected = false;
    s->getAdvertising()->start();        // 자동 재광고
  }
};

static void clearBonds() {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return;
  esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
  if (!list) return;
  if (esp_ble_get_bond_device_list(&n, list) == ESP_OK) {
    for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
  }
  free(list);
}

static void bleBegin() {
  BLEDevice::init(BLE_NAME);
  BLEDevice::setSecurityCallbacks(nullptr);

  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCb());

  g_hid = new BLEHIDDevice(g_server);
  g_input = g_hid->inputReport(1);
  g_hid->outputReport(1);                // 호스트 LED(Caps 등) 수신용, 현재는 무시
  g_hid->manufacturer()->setValue("DIY");
  g_hid->pnp(0x02, 0x05AC, 0x0220, 0x0100);
  g_hid->hidInfo(0x00, 0x01);
  g_hid->reportMap((uint8_t*)kReportMap, sizeof(kReportMap));
  g_hid->startServices();
  g_hid->setBatteryLevel(100);

  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);   // Just Works
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEAdvertising* adv = g_server->getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(g_hid->hidService()->getUUID());
  adv->start();
}

static void bleSend(const uint8_t* rpt) {
  if (!g_bleConnected) return;
  g_input->setValue((uint8_t*)rpt, REPORT_LEN);
  g_input->notify();
}

// ---------------------------------------------------------------- USB Host
static usb_host_client_handle_t g_client;
static usb_device_handle_t      g_dev = nullptr;
static usb_transfer_t*          g_in  = nullptr;
static uint8_t  g_iface = 0, g_ep = 0;
static uint16_t g_mps = 8;
static bool     g_claimed = false;
static volatile bool g_inDead = false;     // IN 전송 종료(분리/오류)
static volatile bool g_newDev = false, g_devGone = false;
static volatile uint8_t g_newAddr = 0;

static void clientEventCb(const usb_host_client_event_msg_t* m, void*) {
  if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    g_newAddr = m->new_dev.address;
    g_newDev = true;
  } else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    g_devGone = true;
  }
}

static void hostLibTask(void*) {
  for (;;) {
    uint32_t flags;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

static void inCb(usb_transfer_t* t) {
  if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes >= REPORT_LEN) {
    uint8_t rpt[REPORT_LEN];
    memcpy(rpt, t->data_buffer, REPORT_LEN);
    xQueueSend(g_reportQ, rpt, 0);
  }
  if (t->status == USB_TRANSFER_STATUS_COMPLETED ||
      t->status == USB_TRANSFER_STATUS_TIMED_OUT) {
    if (usb_host_transfer_submit(t) == ESP_OK) return;
  }
  g_inDead = true;
}

// 컨트롤 전송 (no-data). client 이벤트를 돌려가며 완료까지 대기
static volatile bool g_ctrlDone;
static void ctrlCb(usb_transfer_t*) { g_ctrlDone = true; }

static bool ctrlNoData(uint8_t bRequest, uint16_t wValue, uint16_t wIndex) {
  usb_transfer_t* t;
  if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &t) != ESP_OK) return false;
  usb_setup_packet_t* s = (usb_setup_packet_t*)t->data_buffer;
  s->bmRequestType = 0x21;               // Host->Device, Class, Interface
  s->bRequest = bRequest;
  s->wValue = wValue;
  s->wIndex = wIndex;
  s->wLength = 0;
  t->num_bytes = sizeof(usb_setup_packet_t);
  t->device_handle = g_dev;
  t->bEndpointAddress = 0;
  t->callback = ctrlCb;
  t->context = nullptr;
  t->timeout_ms = 500;
  g_ctrlDone = false;
  bool ok = false;
  if (usb_host_transfer_submit_control(g_client, t) == ESP_OK) {
    uint32_t start = millis();
    while (!g_ctrlDone && millis() - start < 1000) usb_host_client_handle_events(g_client, 20);
    ok = g_ctrlDone && t->status == USB_TRANSFER_STATUS_COMPLETED;
  }
  usb_host_transfer_free(t);
  return ok;
}

// 설정 디스크립터에서 Boot Keyboard 인터페이스와 인터럽트 IN 엔드포인트 탐색
static bool findBootKeyboard(const usb_config_desc_t* cfg) {
  const uint8_t* p = (const uint8_t*)cfg;
  uint16_t total = cfg->wTotalLength, off = 0;
  bool inKbd = false;
  while (off + 2 <= total && p[off] > 0) {
    uint8_t len = p[off], type = p[off + 1];
    if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && len >= 9) {
      // [5]=class [6]=subclass [7]=protocol
      inKbd = (p[off + 5] == 0x03 && p[off + 6] == 0x01 && p[off + 7] == 0x01);
      if (inKbd) g_iface = p[off + 2];
    } else if (inKbd && type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && len >= 7) {
      uint8_t addr = p[off + 2], attr = p[off + 3];
      if ((addr & 0x80) && (attr & 0x03) == 0x03) {
        g_ep = addr;
        g_mps = p[off + 4] | (p[off + 5] << 8);
        return true;
      }
    }
    off += len;
  }
  return false;
}

static void sendReleaseAll() {
  uint8_t zero[REPORT_LEN] = {0};
  xQueueSend(g_reportQ, zero, 0);
}

static void usbDetach() {
  if (g_dev && g_ep) {
    usb_host_endpoint_halt(g_dev, g_ep);
    usb_host_endpoint_flush(g_dev, g_ep);
    uint32_t start = millis();
    while (!g_inDead && millis() - start < 500) usb_host_client_handle_events(g_client, 20);
  }
  if (g_in) { usb_host_transfer_free(g_in); g_in = nullptr; }
  if (g_dev) {
    if (g_claimed) usb_host_interface_release(g_client, g_dev, g_iface);
    usb_host_device_close(g_client, g_dev);
  }
  g_dev = nullptr; g_claimed = false; g_ep = 0;
  sendReleaseAll();
}

static void usbAttach(uint8_t addr) {
  if (usb_host_device_open(g_client, addr, &g_dev) != ESP_OK) { g_dev = nullptr; return; }
  const usb_config_desc_t* cfg;
  if (usb_host_get_active_config_descriptor(g_dev, &cfg) != ESP_OK || !findBootKeyboard(cfg)) {
    usb_host_device_close(g_client, g_dev); g_dev = nullptr; g_ep = 0; return;   // 키보드 아님
  }
  if (usb_host_interface_claim(g_client, g_dev, g_iface, 0) != ESP_OK) {
    usb_host_device_close(g_client, g_dev); g_dev = nullptr; g_ep = 0; return;
  }
  g_claimed = true;
  ctrlNoData(0x0B, 0, g_iface);          // SET_PROTOCOL(Boot)
  ctrlNoData(0x0A, 0, g_iface);          // SET_IDLE(0)

  uint16_t sz = ((REPORT_LEN + g_mps - 1) / g_mps) * g_mps;   // MPS의 배수
  if (usb_host_transfer_alloc(sz, 0, &g_in) != ESP_OK) { usbDetach(); return; }
  g_in->num_bytes = sz;
  g_in->device_handle = g_dev;
  g_in->bEndpointAddress = g_ep;
  g_in->callback = inCb;
  g_in->context = nullptr;
  g_in->timeout_ms = 0;
  g_inDead = false;
  if (usb_host_transfer_submit(g_in) != ESP_OK) usbDetach();
}

static void clientTask(void*) {
  for (;;) {
    usb_host_client_handle_events(g_client, 50);
    if (g_newDev)  { g_newDev = false;  if (!g_dev) usbAttach(g_newAddr); }
    if (g_devGone || (g_dev && g_inDead)) { g_devGone = false; if (g_dev) usbDetach(); }
  }
}

static void usbBegin() {
  usb_host_config_t cfg = {};
  cfg.skip_phy_setup = false;
  cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  usb_host_install(&cfg);

  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 5;
  cc.async.client_event_callback = clientEventCb;
  cc.async.callback_arg = nullptr;
  usb_host_client_register(&cc, &g_client);

  xTaskCreate(hostLibTask, "usb_lib", 4096, nullptr, 5, nullptr);
  xTaskCreate(clientTask,  "usb_cli", 6144, nullptr, 5, nullptr);
}

// ---------------------------------------------------------------- main
void setup() {
  led.begin();
  led.setBrightness(40);
  setLed(true);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  g_reportQ = xQueueCreate(32, REPORT_LEN);
  bleBegin();
  usbBegin();
}

void loop() {
  // USB -> BLE 포워딩
  uint8_t rpt[REPORT_LEN];
  while (xQueueReceive(g_reportQ, rpt, 0) == pdTRUE) bleSend(rpt);

  // BOOT 길게 누르면 본딩 삭제
  static uint32_t pressedAt = 0;
  if (digitalRead(BOOT_BTN_PIN) == LOW) {
    if (!pressedAt) pressedAt = millis();
    if (millis() - pressedAt > CLEAR_BOND_MS) {
      clearBonds();
      if (g_bleConnected) g_server->disconnect(g_server->getConnId());
      for (int i = 0; i < 6; i++) { setLed(i & 1, 255, 0, 0); delay(100); }
      pressedAt = 0;
      while (digitalRead(BOOT_BTN_PIN) == LOW) delay(10);
    }
  } else {
    pressedAt = 0;
  }

  // LED 상태 표시
  static uint32_t lastBlink = 0; static bool blinkOn = false;
  if (g_bleConnected) {
    setLed(true);
  } else if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    setLed(blinkOn);
  }

  delay(1);
}
