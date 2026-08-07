#ifndef NET_TASK_H
#define NET_TASK_H

#include <Arduino.h>

// Tarea de red en core 0 (donde ya vive el stack WiFi): es la ÚNICA dueña de
// mqttClient una vez arranca (PubSubClient no es thread-safe). El core 1 pinta
// y encola publishes; los bloqueos de socket (hasta 2s con paquetes
// fragmentados) le pasan a esta tarea sin congelar el video.
//
// Ciclo de vida: se arranca al final de la conexión inicial (setup); antes de
// eso todo el flujo de arranque usa mqttClient directo como siempre.

extern volatile bool netTaskRunning;

void startNetTask();

// Publica via la cola de la tarea de red (thread-safe). Antes de startNetTask()
// publica directo (flujo de setup). Devuelve false si la cola sigue llena tras
// un timeout corto: el llamante decide si reintenta.
bool netPublish(const char* topic, const char* payload);

// Mutex recursivo que protege animBuffer y su estado de descarga: lo escriben
// handleAnimationFrameResponse/handlePhotoResponse (tarea de red) y lo
// libera/transfiere el core 1 (swap, stop, timeout).
void animBufLock();
void animBufUnlock();

#endif
