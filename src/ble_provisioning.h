#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include "globals.h"

void sendBLEResponse(bool success, String frameToken = "", String error = "");
void setupBLE();
bool processBLECredentials();
bool processBLECredentialsNonBlocking();

#endif
