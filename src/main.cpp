#include <M5Atom.h>
#include "NimBLEDevice.h"

// #define _IR_ENABLE_DEFAULT_ false
// #define SEND_DAIKIN2 true

#include "IRremoteESP8266.h"
#include "IRsend.h"
#include "ir_Daikin.h"

#include "IRac.h"
#include "IRutils.h"

const uint16_t IR_TX_PIN = 26;
const uint16_t IR_RX_PIN = 32;
IRDaikin2 daikin2(IR_TX_PIN);

// SwitchBot cooperation codes are based on the following articles but certain values
// are changed based on my own observation:
// https://qiita.com/warpzone/items/11ec9bef21f5b965bce3
// https://github.com/OpenWonderLabs/SwitchBotAPI-BLE/blob/latest/devicetypes/meter.md
static const NimBLEUUID dataUUID(static_cast<uint16_t>(0xfd3d));
const uint8_t DEVICE_TYPE = 0x69; // ???

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice *advertisedDevice)
  {
    auto count = advertisedDevice->getServiceDataCount();
    for (int i = 0; i < count; i++)
    {
      auto uuid = advertisedDevice->getServiceDataUUID(i);
      auto data = advertisedDevice->getServiceData(i);
      if (uuid.equals(dataUUID) && data.length() == 6 && (data[0] & 0b01111111) == DEVICE_TYPE)
      {
        auto battery = data[2] & 0b01111111;
        auto temp10x = (data[3] & 0b00001111) + (data[4] & 0b01111111) * 10;
        auto tempPositive = (data[4] & 0b10000000) != 0;
        auto humidity = data[5] & 0b01111111;

        // auto isEncrypted = (data[0] & 0b10000000) >> 7;
        // auto isDualStateMode = (data[1] & 0b10000000) >> 7;
        // auto isStatusOff = (data[1] & 0b01000000) >> 6;
        // auto isTemperatureHighAlert = (data[3] & 0b10000000) >> 7;
        // auto isTemperatureLowAlert = (data[3] & 0b01000000) >> 6;
        // auto isHumidityHighAlert = (data[3] & 0b00100000) >> 5;
        // auto isHumidityLowAlert = (data[3] & 0b00010000) >> 4;
        // auto isTemperatureUnitF = (data[5] & 0b10000000) >> 7;

        Serial.printf("B=%d%% T=%s%d.%d H=%d%%\n",
                      battery,
                      tempPositive ? "" : "-", temp10x / 10, temp10x % 10,
                      humidity);
        advertisedDevice->getScan()->stop();

        processTemperature(tempPositive, temp10x);
      }
    }
  }

  void processTemperature(bool tempPositive, int temp10x)
  {
    auto temp = temp10x / (tempPositive ? 10.0f : -10.0f);
  }
};

BLEScan *pBLEScan;

IRrecv irrecv(IR_RX_PIN, 1024, 50, true);
decode_results results;
uint8_t prev[kDaikin312StateLength] = {};
uint8_t *pprev = nullptr;

void setup()
{
  Serial.begin(115200);
  BLEDevice::init("");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
  pBLEScan->setActiveScan(true);

  daikin2.begin();
  daikin2.calibrate();

  irrecv.enableIRIn();
}

void loop()
{
  if (irrecv.decode(&results) && results.bits == kDaikin312Bits)
  {
    auto &buf = results.state;
    if (pprev)
    {
      for (int i = 0; i < kDaikin312StateLength; i++)
      {
        if (prev[i] != buf[i])
          Serial.printf("%02d", i);
        else
          Serial.print(" .");
      }
      Serial.print("\n");
    }
    pprev = prev;
    memcpy(prev, buf, kDaikin312StateLength);
    for (int i = 0; i < kDaikin312StateLength; i++)
      Serial.printf("%02X%s", buf[i], i + 1 < kDaikin312StateLength ? "" : "\n");

    static const uint8_t header[] = {0x11, 0xDA, 0x27, 0x00, 0x02};
    if (memcmp(buf, header, sizeof(header)) != 0)
    {
      Serial.print("Invalid header (1).\n");
      return;
    }

    static const uint8_t header2[] = {0x11, 0xDA, 0x27, 0x00, 0x00};
    if (memcmp(buf + 20, header2, sizeof(header2)) != 0)
    {
      Serial.print("Invalid header (2).\n");
      return;
    }

    uint8_t sum = 0;
    for (int i = 0; i < 19; i++)
      sum += buf[i];
    if (sum != buf[19])
    {
      Serial.printf("Checksum failure: B19=%02X, SUM=%02X\n", buf[19], sum & 0xff);
      return;
    }
    sum = 0;
    for (int i = 20; i < 38; i++)
      sum += buf[i];
    if (sum != buf[38])
    {
      Serial.printf("Checksum failure: B38=%02X, SUM=%02X\n", buf[38], sum & 0xff);
      return;
    }

    auto verticalFinAngle = buf[12] >> 4;
    if (verticalFinAngle != 0)
      Serial.printf("  上下風向: %d (1:上 5:下)\n", verticalFinAngle);

    auto airCleanMode = (buf[14] & 0x10) != 0;      // ストリーマ ON=0x10
    auto internalCleanMode = (buf[14] & 0x40) != 0; // 水・内部クリーン ON=0x40

    auto b25 = buf[25];
    auto mode = (b25 & 0x70) >> 4;
    static const char *modes[] = {"自動冷房", "自動", "ドライ", "冷房", "暖房", "0x5 (?)", "送風", "自動暖房"};
    Serial.printf("  %s", modes[mode]);
    if (b25 & 0x8)
      Serial.print(",TBC");
    if (b25 & 0x4)
      Serial.print(",オフタイマー");
    if (b25 & 0x2)
      Serial.print(",オンタイマー");
    Serial.print(b25 & 0x1 ? ",ON\n" : ",OFF\n");

    auto temp2 = static_cast<int>(buf[26]);
    // 0x80 for 自動 or ドライ. 0x00 for others; possibly indicates automatic temperature.
    auto autoTemp = (buf[27] & 0x80) != 0;
    if (autoTemp)
    {
      // 0xCA: +5.0 ℃
      // 0xC1: +0.5 ℃
      // 0xC0:  0.0 ℃
      // 0xDF: -0.5 ℃
      // 0xD6: -5.0 ℃
      auto t = temp2 >= 0xD6 ? (temp2 - 0xE0) / 2.0f : (temp2 - 0xC0) / 2.0f;
      Serial.printf("  自動 標準%+f ℃\n", t);
    }
    else
    {
      auto t = temp2 / 2.0f;
      Serial.printf("  %+f ℃\n", t);
    }

    auto fanSpeed = buf[28] >> 4;
    auto swingVertical = (buf[28] & 0xf) == 0xf;   // 0xf or 0x0
    auto swingHorizontal = (buf[29] & 0xf) == 0xf; // 0xf or 0x0
    if (fanSpeed >= 3 && fanSpeed <= 7)
      Serial.printf("  風量: FS%d\n", fanSpeed - 1);
    else if (fanSpeed == 0xA)
      Serial.print("  風量: 自動\n");
    else if (fanSpeed == 0xB)
      Serial.print("  風量: しずか\n");
    else
      Serial.printf("  風量: 0x%02X (不明)\n", fanSpeed);

    auto sleepMode = (buf[33] & 0x4) != 0;    // おやすみ
    auto odekakeMode = (buf[33] & 0x80) != 0; // おでかけ

    Serial.printf("  風向: %s%s\n", swingVertical ? "上下" : "", swingHorizontal ? "左右" : "");
    Serial.printf("  ストリーマ: %s\n", airCleanMode ? "ON" : "OFF");
    Serial.printf("  水・内部クリーン: %s\n", internalCleanMode ? "ON" : "OFF");
    Serial.printf("  おやすみ: %s\n", sleepMode ? "ON" : "OFF");
    Serial.printf("  おでかけ: %s\n", odekakeMode ? "ON" : "OFF");
  }
  /*
  Serial.println("Scanning...");
  pBLEScan->start(10, false);
  delay(10000);
  */
  M5.update();
  if (M5.Btn.wasReleased())
  {
    Serial.printf("off!\n");
    daikin2.setBeep(1);
    daikin2.off();
    daikin2.send();
  }
  yield();
}
