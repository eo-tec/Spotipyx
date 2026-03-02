#ifndef MQTT_HANDLERS_H
#define MQTT_HANDLERS_H

#include "globals.h"

bool waitForMqttResponse(const char* expectedType, unsigned long timeout = 10000);
void handleSongResponse(byte* payload, unsigned int length);
void handleCoverResponse(byte* payload, unsigned int length);
void handlePhotoResponse(byte* payload, unsigned int length);
void handleOtaResponse(byte* payload, unsigned int length);
void handleConfigResponse(byte* payload, unsigned int length);
void handleRegisterResponse(byte* payload, unsigned int length);
void requestConfig();
bool registerFrameViaMQTT();
void testInit();

#endif
