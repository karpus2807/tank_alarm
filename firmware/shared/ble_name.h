#pragma once
#include <BLEDevice.h>
#include <BLEAdvertising.h>

// Phone Bluetooth scan shows the IP the web page is on. Nothing else uses Bluetooth.
inline void blePublishIp(const char* ip) {
  static char shown[20] = "";
  static bool on = false;
  if (!ip || !ip[0] || ip[0] == '0') return;
  if (!strcmp(shown, ip)) return;
  strncpy(shown, ip, sizeof(shown) - 1);
  shown[sizeof(shown) - 1] = 0;
  if (on) {
    BLEDevice::deinit(false);
    delay(40);
    on = false;
  }
  BLEDevice::init(shown);
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setName(shown);
  adv->setAdvertisementData(advData);
  BLEAdvertisementData scan;
  scan.setName(shown);
  adv->setScanResponseData(scan);
  adv->setScanResponse(true);
  adv->start();
  on = true;
}
