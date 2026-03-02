#include "schedule.h"

bool isWithinSchedule() {
    if (!scheduleEnabled) {
        return true;  // Si el horario no está habilitado, siempre encendido
    }

    // Obtener hora UTC del NTPClient
    timeClient.update();
    int utcHours = timeClient.getHours();
    int utcMinutes = timeClient.getMinutes();
    int utcTotalMinutes = utcHours * 60 + utcMinutes;

    // Aplicar timezone offset para obtener hora local
    int localTotalMinutes = (utcTotalMinutes + timezoneOffset + 24 * 60) % (24 * 60);
    int localHour = localTotalMinutes / 60;
    int localMinute = localTotalMinutes % 60;

    // Convertir horarios de encendido y apagado a minutos totales
    int onTimeMinutes = scheduleOnHour * 60 + scheduleOnMinute;
    int offTimeMinutes = scheduleOffHour * 60 + scheduleOffMinute;
    int currentTimeMinutes = localHour * 60 + localMinute;

    // Caso normal: encendido antes que apagado (ej: 8:00 a 22:00)
    if (onTimeMinutes < offTimeMinutes) {
        return currentTimeMinutes >= onTimeMinutes && currentTimeMinutes < offTimeMinutes;
    }
    // Caso nocturno: encendido después que apagado (ej: 22:00 a 8:00)
    else if (onTimeMinutes > offTimeMinutes) {
        return currentTimeMinutes >= onTimeMinutes || currentTimeMinutes < offTimeMinutes;
    }
    // Caso igual: siempre encendido
    return true;
}

void updateScreenPower() {
    bool shouldBeOn = isWithinSchedule();

    if (shouldBeOn && screenOff) {
        // Encender pantalla
        LOG("[Schedule] Encendiendo pantalla");
        dma_display->setBrightness(max(brightness, 10));
        screenOff = false;
        // Forzar mostrar foto inmediatamente
        lastPhotoChange = 0;
    } else if (!shouldBeOn && !screenOff) {
        // Apagar pantalla
        LOG("[Schedule] Apagando pantalla");
        dma_display->clearScreen();
        dma_display->setBrightness(0);
        screenOff = true;
    }
}
