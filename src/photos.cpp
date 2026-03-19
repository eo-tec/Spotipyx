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
        if (animBuffer) {
            free(animBuffer);
            animBuffer = nullptr;
        }

        uint8_t totalBackendFrames = animFrameCount;
        uint8_t framesToUse = totalBackendFrames;
        animFrameStep = 1;

        if (!hasPsram) {
            // Limit frames to fit in largest contiguous free block (with 8KB safety margin)
            size_t maxBlock = ESP.getMaxAllocHeap();
            size_t safeBlock = maxBlock > 8192 ? maxBlock - 8192 : 0;
            uint8_t maxFrames = safeBlock / animFrameSize;

            if (maxFrames < 2) {
                LOGF("[Anim] Not enough RAM for animation (largest block: %d)", maxBlock);
                currentAnimationId = -1;
                animFrameCount = 0;
                return;
            }

            if (maxFrames < totalBackendFrames) {
                // Sample evenly to cover full duration
                animFrameStep = (totalBackendFrames + maxFrames - 1) / maxFrames; // ceil division
                framesToUse = totalBackendFrames / animFrameStep;
                if (framesToUse < 2) framesToUse = 2;
            }
        }

        size_t needed = framesToUse * animFrameSize;
        if (hasPsram) {
            animBuffer = (uint8_t*)ps_malloc(needed);
        } else {
            animBuffer = (uint8_t*)malloc(needed);
        }
        if (!animBuffer) {
            LOGF("[Anim] Failed to allocate %d bytes (free heap: %d, largest block: %d)", needed, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            currentAnimationId = -1;
            animFrameCount = 0;
            return;
        }

        // Adjust playback interval to maintain original duration
        // Original duration = totalBackendFrames / animFps seconds
        // We have framesToUse frames → interval = duration / framesToUse
        animFrameInterval = (unsigned long)totalBackendFrames * 1000 / animFps / framesToUse;
        animFrameCount = framesToUse;

        LOGF("[Anim] %d/%d frames (step=%d, %dx%d, %s), interval=%dms",
             framesToUse, totalBackendFrames, animFrameStep,
             animFrameWidth, animFrameWidth, hasPsram ? "PSRAM" : "RAM",
             animFrameInterval);
        animFramesReceived = 0;
        requestAnimationFrame(currentAnimationId, 0);
    }
}

void drawAnimationFrame(uint8_t frameIndex) {
    uint8_t* frame = animBuffer + frameIndex * animFrameSize;
    if (animFrameWidth == 64) {
        // Full resolution: 1:1 pixel mapping
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int idx = (y * 64 + x) * 2;
                uint16_t color = (frame[idx] << 8) | frame[idx + 1];
                dma_display->drawPixel(x, y, color);
            }
        }
    } else {
        // 32x32: scale up 2x2 per pixel
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                int idx = (y * 32 + x) * 2;
                uint16_t color = (frame[idx] << 8) | frame[idx + 1];
                dma_display->fillRect(x * 2, y * 2, 2, 2, color);
            }
        }
    }
}

void updateAnimationPlayback() {
    if (!animPlaying || !animReady || animFrameCount == 0 || !animBuffer) return;

    unsigned long now = millis();

    if (now - animLastFrameTime >= animFrameInterval) {
        drawAnimationFrame(animCurrentFrame);
        animCurrentFrame++;
        if (animCurrentFrame >= animFrameCount) {
            animCurrentFrame = 0;
            animLoopCount++;
        }
        animLastFrameTime = now;

        // Stop after N loops — show animation for ~secsPhotos duration
        unsigned long animDuration = (unsigned long)animFrameCount * 1000 / animFps; // one loop ms
        unsigned long maxLoops = max(3UL, secsPhotos / animDuration);
        if (animLoopCount >= maxLoops) {
            LOG("[Anim] Playback finished (loop limit reached)");
            stopAnimation();
            lastPhotoChange = millis(); // reset timer so next photo shows after interval
        }
    }
}

void stopAnimation() {
    animPlaying = false;
    animReady = false;
    currentAnimationId = -1;
    animFrameCount = 0;
    animFramesReceived = 0;
    animLoopCount = 0;
    animFrameStep = 1;
    animFrameInterval = 200;
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
