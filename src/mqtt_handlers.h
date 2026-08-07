#ifndef MQTT_HANDLERS_H
#define MQTT_HANDLERS_H

#include "globals.h"

void armMqttResponseWait(); // llamar SIEMPRE antes del netPublish cuya respuesta se va a esperar
bool waitForMqttResponse(uint8_t expectedType, unsigned long timeout = 10000); // MqttRespType
void handleSongResponse(byte* payload, unsigned int length);
void handleCoverResponse(byte* payload, unsigned int length);
void handlePhotoResponse(byte* payload, unsigned int length);
void handleOtaResponse(byte* payload, unsigned int length);
void handleConfigResponse(byte* payload, unsigned int length);
void handleAnimationFrameResponse(byte* payload, unsigned int length);
bool requestAnimationFrame(int animationId, int frameIndex); // false = cola de red llena, reintentar
void handleRegisterResponse(byte* payload, unsigned int length);
void requestConfig();
bool registerFrameViaMQTT();
void testInit();

#endif
