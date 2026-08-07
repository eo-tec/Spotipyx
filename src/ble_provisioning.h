#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include "globals.h"

void sendBLEResponse(bool success, String frameToken = "", String error = "");
void setupBLE();
void processBLENetworkScan();
bool processBLECredentials();
bool processBLECredentialsNonBlocking();
void enterWaitingForOwnerMode();  // solo estado; los efectos van al core 1
void exitWaitingForOwnerMode();   // idem
void processPendingOwnerUI();     // llamar desde el loop (core 1)
bool processBLECredentialsAlreadyConnected();

#endif
