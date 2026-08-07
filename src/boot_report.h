#ifndef BOOT_REPORT_H
#define BOOT_REPORT_H

// Telemetría de arranque: reset_reason + contador de boots + resumen del core
// dump (si el arranque anterior acabó en panic). Se publica en
// frame/{id}/request/boot una vez por arranque, fire-and-forget (sin response).

// Llamar temprano en setup(), justo después de preferences.begin():
// captura esp_reset_reason() e incrementa el contador de boots en NVS.
void bootReportInit();

// Llamar tras conectar MQTT: publica el boot report y, si había core dump
// y el publish tuvo éxito, lo borra de flash para no reenviarlo.
void sendBootReport();

#endif
