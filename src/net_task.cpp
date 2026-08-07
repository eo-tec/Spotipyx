#include "globals.h"
#include "net_task.h"
#include "mqtt_client.h"

// Mensaje de la cola de publishes salientes. Tamaños: el payload más grande
// que pasa por aquí es un request JSON corto (photo/ota/config/anim frame);
// el boot report (más largo) se publica directo antes de arrancar la tarea.
struct MqttPubMsg {
    char topic[64];
    char payload[224];
};

static QueueHandle_t pubQueue = nullptr;
static SemaphoreHandle_t animBufMutex = nullptr;
volatile bool netTaskRunning = false;

void animBufLock() {
    if (animBufMutex) xSemaphoreTakeRecursive(animBufMutex, portMAX_DELAY);
}

void animBufUnlock() {
    if (animBufMutex) xSemaphoreGiveRecursive(animBufMutex);
}

bool netPublish(const char* topic, const char* payload) {
    if (!netTaskRunning) {
        return mqttClient.publish(topic, payload);
    }
    MqttPubMsg msg;
    strlcpy(msg.topic, topic, sizeof(msg.topic));
    strlcpy(msg.payload, payload, sizeof(msg.payload));
    // Timeout corto: si la cola está llena (tarea de red bloqueada drenando un
    // paquete gordo), devolvemos false en vez de congelar el core 1; el
    // llamante reintenta intercalando updateAnimationPlayback()
    return xQueueSend(pubQueue, &msg, pdMS_TO_TICKS(50)) == pdTRUE;
}

static void netTaskLoop(void*) {
    MqttPubMsg msg;
    for (;;) {
        if (!mqttClient.connected()) {
            mqttReconnect();
        }

        // Drenar publishes encolados por el core 1
        while (xQueueReceive(pubQueue, &msg, 0) == pdTRUE) {
            if (!mqttClient.publish(msg.topic, msg.payload)) {
                LOGF("[Net] Publish fallido en %s", msg.topic);
            }
        }

        // Bombear MQTT: aquí es donde el socket puede bloquear hasta 2s con
        // paquetes fragmentados; en core 0 ya no congela la reproducción
        mqttClient.loop();

        // Refrescar NTP aquí: NTPClient::update() bloquea 1s por intento cuando
        // el servidor no responde y encadenaba iteraciones de ~1s en el core 1
        // (video a 1fps con schedule/reloj activos). El core 1 solo usa los
        // getters, que operan sobre el epoch cacheado sin tocar red.
        timeClient.update();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void startNetTask() {
    if (netTaskRunning) return;
    pubQueue = xQueueCreate(16, sizeof(MqttPubMsg));
    animBufMutex = xSemaphoreCreateRecursiveMutex();
    if (!pubQueue || !animBufMutex) {
        LOG("[Net] ERROR creando cola/mutex - seguimos en modo single-core");
        return;
    }
    // Prio 1 (la misma que loop()); stack holgado porque el callback MQTT
    // (fotos, config, dibujo) corre en su contexto
    BaseType_t ok = xTaskCreatePinnedToCore(netTaskLoop, "netTask", 10240, nullptr, 1, nullptr, 0);
    if (ok != pdPASS) {
        LOG("[Net] ERROR creando la tarea - seguimos en modo single-core");
        return;
    }
    netTaskRunning = true;
    LOG("[Net] Tarea de red arrancada en core 0");
}
