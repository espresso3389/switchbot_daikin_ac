// https://github.com/OpenWonderLabs/SwitchBotAPI-BLE/blob/latest/devicetypes/meter.md
#include <M5Atom.h>
#include "NimBLEDevice.h"

#define _IR_ENABLE_DEFAULT_ false
#define SEND_DAIKIN2 true

#include "IRremoteESP8266.h"
#include "IRsend.h"
#include "ir_Daikin.h"

const uint16_t ATOM_MATRIX_IR_PIN = 12;
IRDaikin2 daikin2(ATOM_MATRIX_IR_PIN);

static NimBLEUUID dataUUID((uint16_t)0xfd3d);
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

void setup()
{
  Serial.begin(115200);
  BLEDevice::init("");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
  pBLEScan->setActiveScan(true);

  daikin2.begin();
  daikin2.calibrate();
}

void loop()
{
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
}
