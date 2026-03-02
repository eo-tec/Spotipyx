#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "globals.h"

void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttReconnect();

#endif
