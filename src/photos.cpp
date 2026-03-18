#include "photos.h"
#include "display.h"
#include "clock.h"
#include "mqtt_handlers.h"
#include <Fonts/Picopixel.h>

void displayPhotoWithFade()
{
    // Solo hacer fadeOut DESPUÉS de confirmar que la imagen está completa
    fadeOut();
    dma_display->clearScreen();

    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    // Copiar al screenBuffer desde el buffer estático
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int idx = (y * 64 + x) * 3;
            uint8_t g = photoBuffer[idx];
            uint8_t b = photoBuffer[idx + 1];
            uint8_t r = photoBuffer[idx + 2];
            screenBuffer[y][x] = dma_display->color565(r, g, b);
        }
    }

    fadeIn();
    showPhotoInfo(String(photoTitle), String(photoAuthor));
    showClockOverlay();
    showDevOverlay();
}

void showPhoto(int index)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[Photo] Heap libre: %d bytes", ESP.getFreeHeap());
    LOGF("[Photo] Solicitando foto index=%d via MQTT", index);

    // Generar request ID único para filtrar respuestas stale
    mqttRequestId++;

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"index\":" + String(index) + ",\"reqId\":" + String(mqttRequestId) + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[Photo] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("photo", 15000)) {
        esp_task_wdt_reset();
        LOGF("[Photo] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        displayPhotoWithFade();
        startAnimationDownloadIfNeeded();
    } else {
        LOG("[Photo] Error recibiendo foto via MQTT");
    }

    isLoadingPhoto = false;
    processPendingPhoto();
}

void showPhotoById(int id)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[Photo] Heap libre: %d bytes", ESP.getFreeHeap());
    LOGF("[Photo] Solicitando foto id=%d via MQTT", id);

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"id\":" + String(id) + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[Photo] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("photo", 15000)) {
        esp_task_wdt_reset();
        LOGF("[Photo] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        displayPhotoWithFade();
        startAnimationDownloadIfNeeded();
    } else {
        LOG("[Photo] Error recibiendo foto via MQTT");
    }

    isLoadingPhoto = false;
    processPendingPhoto();
}

void displayPhotoFromCenter()
{
    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    const int width = 64;
    const int height = 64;
    int centerX = 32;
    int centerY = 32;

    // Animación de cuadrados de colores
    uint16_t colors[] = {color1, color2, color3, color4, color5};
    int numColors = 5;

    for (int colorIndex = 0; colorIndex < numColors; colorIndex++)
    {
        for (int size = 2; size <= max(PANEL_RES_X, PANEL_RES_Y); size += 2)
        {
            for (int y = centerY - size / 2; y <= centerY + size / 2; y++)
            {
                for (int x = centerX - size / 2; x <= centerX + size / 2; x++)
                {
                    if (x >= 0 && x < PANEL_RES_X && y >= 0 && y < PANEL_RES_Y)
                    {
                        drawPixelWithBuffer(x, y, colors[colorIndex]);
                    }
                }
            }
            delay(5);
        }
    }

    // Mostrar imagen desde el centro
    for (int radius = 0; radius <= max(width, height); radius++)
    {
        for (int y = centerY - radius; y <= centerY + radius; y++)
        {
            for (int x = centerX - radius; x <= centerX + radius; x++)
            {
                if (x >= 0 && x < width && y >= 0 && y < height)
                {
                    if (abs(x - centerX) == radius || abs(y - centerY) == radius)
                    {
                        int index = (y * width + x) * 3;
                        uint8_t r = photoBuffer[index + 2];
                        uint8_t g = photoBuffer[index + 0];
                        uint8_t b = photoBuffer[index + 1];
                        uint16_t color = dma_display->color565(r, g, b);
                        drawPixelWithBuffer(x, y, color);
                    }
                }
            }
        }
        delay(5);
    }
    showPhotoInfo(String(photoTitle), String(photoAuthor));
}

void showPhotoFromCenterById(int id)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[PhotoCenter] Heap libre: %d bytes", ESP.getFreeHeap());
    LOGF("[PhotoCenter] Solicitando foto id=%d via MQTT", id);

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"id\":" + String(id) + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[PhotoCenter] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("photo", 15000)) {
        esp_task_wdt_reset();
        LOGF("[PhotoCenter] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        displayPhotoFromCenter();
        startAnimationDownloadIfNeeded();
    } else {
        LOG("[PhotoCenter] Error recibiendo foto via MQTT");
    }

    songShowing = "";
    isLoadingPhoto = false;
    processPendingPhoto();
}

void showPhotoIndex(int index)
{
    LOGF("[Photo] Solicitando foto index=%d", index);
    showPhoto(index);
}

void showPhotoId(int id)
{
    LOGF("[Photo] Solicitando foto id=%d", id);
    showPhotoById(id);
}

void processPendingPhoto() {
    if (pendingNewPhotoId >= 0) {
        int id = pendingNewPhotoId;
        pendingNewPhotoId = -1;
        LOGF("[Photo] Procesando foto pendiente id=%d", id);
        onReceiveNewPic(id);
    }
}

void onReceiveNewPic(int id)
{
    // Si ya hay una foto cargándose, guardar como pendiente
    if (isLoadingPhoto) {
        LOGF("[Photo] Foto %d guardada como pendiente (hay otra cargando)", id);
        pendingNewPhotoId = id;
        return;
    }

    photoIndex = 1;

    // Mostrar la foto con animación desde el centro via MQTT
    showPhotoFromCenterById(id);
    lastPhotoChange = millis();
    photoIndex = 0;
    songShowing = "";
}

void showPhotoInfo(String title, String name)
{
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);

    // Calcular las dimensiones reales de ambos textos primero
    int16_t titleX1, titleY1, nameX1, nameY1;
    uint16_t titleW = 0, titleH = 0, nameW = 0, nameH = 0;

    dma_display->setTextWrap(false);

    if (title.length() > 0) {
        dma_display->getTextBounds(title, 0, 0, &titleX1, &titleY1, &titleW, &titleH);
    }

    if (name.length() > 0) {
        dma_display->getTextBounds(name, 0, 0, &nameX1, &nameY1, &nameW, &nameH);
    }
    LOGF("titleW: %d title: %s", titleW, title.c_str());

    // Guardar el título y nombre actuales
    currentTitle = title;
    currentName = name;

    // Margen entre textos cuando están en la misma línea
    const int horizontalMargin = 4;

    // Decidir el layout basado en los anchos reales
    bool sameLine = false;
    if (title.length() > 0 && name.length() > 0) {
        sameLine = (titleW + nameW + horizontalMargin) <= PANEL_RES_X;
    } else {
        sameLine = true;
    }

    if (sameLine) {
        titleY = PANEL_RES_Y - 2;
        nameY = PANEL_RES_Y - 2;

        if (title.length() > 0) {
            if (titleW > PANEL_RES_X - 2) {
                titleNeedsScroll = true;
                titleScrollOffset = 0;
                titleScrollState = SCROLL_PAUSED_START;
                titleScrollPauseStart = millis();

                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, PANEL_RES_X, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            } else {
                titleNeedsScroll = false;
                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, titleW + 3, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            }
        }

        if (name.length() > 0) {
            int nameX = PANEL_RES_X - nameW;
            dma_display->getTextBounds(name, nameX, nameY, &nameX1, &nameY1, &nameW, &nameH);
            dma_display->fillRect(nameX - 1, nameY1 - 1, nameW + 2, nameH + 2, myBLACK);
            dma_display->setCursor(nameX, nameY);
            dma_display->print(name);
        }
    } else {
        int bottomY = PANEL_RES_Y - 2;
        int topY = bottomY - titleH - 3;

        if (topY < PANEL_RES_Y - 12) {
            topY = PANEL_RES_Y - 12;
        }

        titleY = bottomY;
        nameY = topY;

        if (title.length() > 0) {
            if (titleW > PANEL_RES_X - 2) {
                titleNeedsScroll = true;
                titleScrollOffset = 0;
                titleScrollState = SCROLL_PAUSED_START;
                titleScrollPauseStart = millis();

                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, PANEL_RES_X, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            } else {
                titleNeedsScroll = false;
                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, titleW + 3, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            }
        }

        if (name.length() > 0) {
            dma_display->getTextBounds(name, 1, nameY, &nameX1, &nameY1, &nameW, &nameH);
            dma_display->fillRect(0, nameY1 - 1, nameW + 3, nameH + 2, myBLACK);
            dma_display->setCursor(1, nameY);
            dma_display->print(name);
        }
    }
}

void startAnimationDownloadIfNeeded() {
    if (currentAnimationId > 0 && animFrameCount > 0 && !animReady) {
        // Allocate buffer if needed
        size_t needed = animFrameCount * ANIM_FRAME_SIZE;
        if (animBuffer) {
            free(animBuffer);
            animBuffer = nullptr;
        }
        animBuffer = (uint8_t*)malloc(needed);
        if (!animBuffer) {
            LOGF("[Anim] Failed to allocate %d bytes for animation buffer (free heap: %d)", needed, ESP.getFreeHeap());
            currentAnimationId = -1;
            animFrameCount = 0;
            return;
        }
        LOGF("[Anim] Allocated %d bytes, starting download of %d frames (free heap: %d)", needed, animFrameCount, ESP.getFreeHeap());
        animFramesReceived = 0;
        requestAnimationFrame(currentAnimationId, 0);
    }
}

void drawAnimationFrame(uint8_t frameIndex) {
    uint8_t* frame = animBuffer + frameIndex * ANIM_FRAME_SIZE;
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            int idx = (y * PANEL_RES_X + x) * 2;
            uint16_t color = (frame[idx] << 8) | frame[idx + 1];
            dma_display->drawPixel(x, y, color);
        }
    }
}

void updateAnimationPlayback() {
    if (!animPlaying || !animReady || animFrameCount == 0 || !animBuffer) return;

    unsigned long now = millis();
    unsigned long interval = 1000 / animFps;

    if (now - animLastFrameTime >= interval) {
        drawAnimationFrame(animCurrentFrame);
        animCurrentFrame = (animCurrentFrame + 1) % animFrameCount;
        animLastFrameTime = now;
    }
}

void stopAnimation() {
    animPlaying = false;
    animReady = false;
    currentAnimationId = -1;
    animFrameCount = 0;
    animFramesReceived = 0;
    if (animBuffer) {
        free(animBuffer);
        animBuffer = nullptr;
        LOGF("[Anim] Buffer freed (free heap: %d)", ESP.getFreeHeap());
    }
}

void updatePhotoInfo() {
    if (!titleNeedsScroll || currentTitle.length() == 0) {
        return;
    }

    unsigned long currentTime = millis();

    switch (titleScrollState) {
        case SCROLL_PAUSED_START:
            if (currentTime - titleScrollPauseStart >= titleScrollPauseTime) {
                titleScrollState = SCROLL_SCROLLING;
                lastTitleScrollTime = currentTime;
            }
            break;

        case SCROLL_SCROLLING:
            if (currentTime - lastTitleScrollTime >= titleScrollSpeed) {
                titleScrollOffset++;

                String scrolledTitle = currentTitle.substring(titleScrollOffset);

                dma_display->setFont(&Picopixel);
                dma_display->setTextSize(1);
                int16_t x1, y1;
                uint16_t w, h;
                dma_display->getTextBounds(scrolledTitle, 1, titleY, &x1, &y1, &w, &h);

                dma_display->fillRect(0, y1 - 1, PANEL_RES_X, h + 2, myBLACK);

                dma_display->setTextColor(myWHITE);
                dma_display->setCursor(1, titleY);
                dma_display->print(scrolledTitle);

                if (w <= PANEL_RES_X - 2) {
                    titleScrollState = SCROLL_PAUSED_END;
                    titleScrollPauseStart = currentTime;
                }

                lastTitleScrollTime = currentTime;
            }
            break;

        case SCROLL_PAUSED_END:
            if (currentTime - titleScrollPauseStart >= titleScrollPauseTime) {
                titleScrollState = SCROLL_RETURNING;
                lastTitleScrollTime = currentTime;
            }
            break;

        case SCROLL_RETURNING:
            if (currentTime - lastTitleScrollTime >= titleScrollSpeed) {
                if (titleScrollOffset > 0) {
                    titleScrollOffset--;

                    String scrolledTitle = currentTitle.substring(titleScrollOffset);

                    dma_display->setFont(&Picopixel);
                    dma_display->setTextSize(1);
                    int16_t x1, y1;
                    uint16_t w, h;
                    dma_display->getTextBounds(scrolledTitle, 1, titleY, &x1, &y1, &w, &h);

                    dma_display->fillRect(0, y1 - 1, PANEL_RES_X, h + 2, myBLACK);

                    dma_display->setTextColor(myWHITE);
                    dma_display->setCursor(1, titleY);
                    dma_display->print(scrolledTitle);

                    lastTitleScrollTime = currentTime;
                } else {
                    titleScrollState = SCROLL_PAUSED_START;
                    titleScrollPauseStart = currentTime;
                }
            }
            break;
    }
}
