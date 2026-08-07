#include "globals.h"
#include "config.h"
#include "boot_report.h"
#include <esp_system.h>

// El componente espcoredump viene activado en el sdkconfig del core Arduino
// (COREDUMP_ENABLE_TO_FLASH + formato ELF) y min_spiffs.csv ya reserva la
// partición "coredump" de 64 KB, así que todos los frames en campo lo tienen.
#if __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#define BOOT_REPORT_HAS_COREDUMP 1
#endif

// Capturados en bootReportInit() para enviarlos cuando haya MQTT
static esp_reset_reason_t s_resetReason = ESP_RST_UNKNOWN;
static uint32_t s_totalBoots = 0;

void bootReportInit()
{
    s_resetReason = esp_reset_reason();
    // Persistir YA el contador, antes de nada que pueda crashear: así un
    // boot-loop queda registrado aunque nunca llegue a conectar
    s_totalBoots = preferences.getUInt("totalBoots", 0) + 1;
    preferences.putUInt("totalBoots", s_totalBoots);
}

void sendBootReport()
{
    if (frameId == 0 || !mqttClient.connected()) {
        LOG("[BootReport] Sin frameId o MQTT desconectado - no se envía");
        return;
    }

    JsonDocument doc;
    doc["reset_reason"] = (int)s_resetReason;
    doc["boot_count"] = s_totalBoots;
    doc["fw_version"] = currentVersion;
    doc["hw_version"] = HW_VERSION;
    doc["free_heap"] = ESP.getFreeHeap();

#ifdef BOOT_REPORT_HAS_COREDUMP
    // Si el arranque anterior murió en panic, el core dump sigue en flash:
    // adjuntar el resumen (tarea, PC y backtrace en direcciones crudas; se
    // resuelven a funciones con el .elf de la versión correspondiente)
    bool hadCoreDump = false;
    size_t cdAddr = 0, cdSize = 0;
    if (esp_core_dump_image_get(&cdAddr, &cdSize) == ESP_OK) {
        hadCoreDump = true;
        esp_core_dump_summary_t summary;
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            JsonObject crash = doc["crash"].to<JsonObject>();
            crash["task"] = summary.exc_task;
            char addrBuf[12];
            snprintf(addrBuf, sizeof(addrBuf), "0x%08x", (unsigned)summary.exc_pc);
            crash["pc"] = addrBuf;
            crash["exc_cause"] = summary.ex_info.exc_cause;
            String bt;
            uint32_t depth = summary.exc_bt_info.depth;
            if (depth > 16) depth = 16;
            for (uint32_t i = 0; i < depth; i++) {
                snprintf(addrBuf, sizeof(addrBuf), "0x%08x", (unsigned)summary.exc_bt_info.bt[i]);
                if (i) bt += ' ';
                bt += addrBuf;
            }
            crash["backtrace"] = bt;
            crash["corrupted"] = summary.exc_bt_info.corrupted;
        }
    }
#endif

    String payload;
    serializeJson(doc, payload);
    String topic = String("frame/") + String(frameId) + "/request/boot";
    bool ok = mqttClient.publish(topic.c_str(), payload.c_str());
    LOGF("[BootReport] reset_reason=%d boot=%u → %s", (int)s_resetReason,
         (unsigned)s_totalBoots, ok ? "enviado" : "FALLO al publicar");

#ifdef BOOT_REPORT_HAS_COREDUMP
    // Borrar solo si se envió: si falló el publish, se reintenta al siguiente boot
    if (ok && hadCoreDump) {
        esp_err_t err = esp_core_dump_image_erase();
        LOGF("[BootReport] Core dump borrado: %s", err == ESP_OK ? "OK" : "ERROR");
    }
#endif
}
