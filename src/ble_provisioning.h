#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include "globals.h"

void sendBLEResponse(bool success, String frameToken = "", String error = "");
void setupBLE();
bool processBLECredentials();
bool processBLECredentialsNonBlocking();
void enterWaitingForOwnerMode();
void exitWaitingForOwnerMode();
bool processBLECredentialsAlreadyConnected();

#endif
