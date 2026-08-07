#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "globals.h"

void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttReconnect();
void applyFactoryReset();  // core 1: procesado del flag pendingFactoryReset
void applyEnvVarsReset();  // core 1: procesado del flag pendingEnvVarsReset

#endif
