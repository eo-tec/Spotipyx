#include "photos.h"
#include "display.h"
#include "clock.h"
#include "mqtt_handlers.h"
#include "net_task.h"
#include <Fonts/Picopixel.h>
#include <new>
#include <PhotoReveal.h>

// Mark a rectangle in the overlay bitmask
void markOverlayRegion(int x, int y, int w, int h) {
    for (int row = max(0, y); row < min(64, y + h); row++) {
        for (int col = max(0, x); col < min(64, x + w); col++) {
            overlayMaskSet(col, row);
        }
    }
}

// Build overlay bitmask from current title/author/clock positions
void buildOverlayMask() {
    overlayMaskClear();
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);

    // Title region
    if (currentTitle.length() > 0) {
        int16_t x1, y1;
        uint16_t w, h;
        dma_display->getTextBounds(currentTitle, 1, titleY, &x1, &y1, &w, &h);
        // Mark the full width for scrolling titles, or just the text width
        int rectW = titleNeedsScroll ? PANEL_RES_X : w + 3;
        markOverlayRegion(0, y1 - 1, rectW, h + 2);
    }

    // Author name region: usar la posicion real donde showPhotoInfo lo pinto
    // (derecha en layout de una linea, izquierda en el de dos lineas)
    if (currentName.length() > 0) {
        int16_t x1, y1;
        uint16_t w, h;
        dma_display->getTextBounds(currentName, nameX, nameY, &x1, &y1, &w, &h);
        markOverlayRegion(nameX - 1, y1 - 1, w + 2, h + 2);
    }

    // Clock region (top-right corner)
    if (clockEnabled) {
        // Clock uses ~30x8 pixels in top-right
        markOverlayRegion(PANEL_RES_X - 30, 0, 30, 8);
    }

    #ifdef DEV_MODE
    // Dev overlay (top-left)
    markOverlayRegion(0, 0, 18, 8);
    #endif
}

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

    armMqttResponseWait();
    if (!netPublish(topic.c_str(), payload.c_str())) {
        LOG("[Photo] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse(RESP_PHOTO, 15000)) {
        esp_task_wdt_reset();
        LOGF("[Photo] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        if (currentAnimationId > 0) {
            // Animacion: lo que haya en pantalla (foto o video en curso) sigue
            // hasta que la descarga termine (el swap lo hace startAnimationPlaybackIfReady)
            startAnimationDownloadIfNeeded();
        }
        if (currentAnimationId <= 0) { // foto normal, o sin memoria para animar
            if (animPlaying) photoPending = true; // prefetch: se pinta al acabar el video
            else displayPhotoWithFade();
        }
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
    // reqId: si la respuesta llega tarde (tras el timeout) se descarta en vez de
    // pisar la foto de la peticion siguiente
    mqttRequestId++;
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"id\":" + String(id) + ",\"reqId\":" + String(mqttRequestId) + "}";

    armMqttResponseWait();
    if (!netPublish(topic.c_str(), payload.c_str())) {
        LOG("[Photo] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse(RESP_PHOTO, 15000)) {
        esp_task_wdt_reset();
        LOGF("[Photo] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        if (currentAnimationId > 0) {
            startAnimationDownloadIfNeeded();
        }
        if (currentAnimationId <= 0) {
            if (animPlaying) photoPending = true;
            else displayPhotoWithFade();
        }
    } else {
        LOG("[Photo] Error recibiendo foto via MQTT");
    }

    isLoadingPhoto = false;
    processPendingPhoto();
}

// Animacion con la que entra una foto que te acaban de mandar. Los estilos estan en
// lib/photo_reveal y se pueden ver en el Mac con native-sim/ (make run). Cada frame
// elige el suyo en la app: llega como "reveal_style" en update_info y en la config.
static const reveal::Style OFFERED_STYLES[] = {
    reveal::Style::POLAROID, reveal::Style::LETTER, reveal::Style::POSTCARD,
    reveal::Style::FILM,     reveal::Style::PRINT,  reveal::Style::NEWPIC,
};
static const reveal::Style DEFAULT_NEW_PHOTO_STYLE = reveal::Style::POLAROID;
static volatile reveal::Style newPhotoStyle = DEFAULT_NEW_PHOTO_STYLE;

// Nombre del estilo (el de styleName) -> estilo. Uno desconocido o que no se ofrece
// en la app vuelve al de por defecto, para que un backend mas nuevo no deje al frame
// sin animacion.
static reveal::Style newPhotoStyleFromName(const char *name) {
    if (name) {
        for (reveal::Style style : OFFERED_STYLES)
            if (strcmp(name, reveal::styleName(style)) == 0) return style;
    }
    return DEFAULT_NEW_PHOTO_STYLE;
}

void loadNewPhotoStyle() {
    if (preferences.isKey("revealStyle"))
        newPhotoStyle = newPhotoStyleFromName(preferences.getString("revealStyle").c_str());
    LOGF("[PhotoCenter] Animacion de foto nueva: %s", reveal::styleName(newPhotoStyle));
}

// Llamable desde la tarea de red (core 0): solo cambia un byte que el core 1 lee al
// empezar cada animacion. Guarda en flash solo si cambia (la config llega en cada arranque).
void setNewPhotoStyle(const char *name) {
    reveal::Style style = newPhotoStyleFromName(name);
    if (name && style == DEFAULT_NEW_PHOTO_STYLE && strcmp(name, reveal::styleName(style)) != 0)
        LOGF("[PhotoCenter] Animacion desconocida '%s': se usa %s", name, reveal::styleName(style));
    if (style == newPhotoStyle) return;
    newPhotoStyle = style;
    preferences.putString("revealStyle", reveal::styleName(style));
    LOGF("[PhotoCenter] Animacion de foto nueva: %s", reveal::styleName(style));
}
static const uint32_t NEW_PHOTO_FRAME_MS = 33; // ~30 fps

// Un video que te acaban de mandar se esta descargando y se anunciara al acabar
static bool revealPending = false;

// Si llegan varias seguidas, cada una se queda al menos esto en pantalla antes de
// anunciar la siguiente de la cola (un video, ademas, una vuelta entera)
static const unsigned long NEW_PHOTO_MIN_SHOW_MS = 5000;
static unsigned long newPhotoShownAt = 0;
static unsigned long newPhotoMinShowMs = 0;

// El panel trae los canales rotados: lo que entra por el rojo se ve azul, el verde se
// ve rojo y el azul se ve verde (por eso el resto del firmware pinta con
// color565(b, r, g)). lib/photo_reveal trabaja con colores reales, asi que se
// deshace al leer de pantalla y se vuelve a aplicar al pintar.

// Lo que habia en pantalla (screenBuffer, RGB565 con los canales del panel)
class ScreenSource : public reveal::Source {
 public:
    void get(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) const override {
        rgb565ToRgb(screenBuffer[y][x], b, r, g);
    }
};

// El fotograma de video que se esta viendo (playBuffer, RGB565 con los canales del
// panel, como drawAnimationFrame). drawAnimationFrame no pasa por screenBuffer, asi
// que con un video en curso la pantalla anterior se lee de aqui.
class PlayFrameSource : public reveal::Source {
 public:
    const uint8_t *frame = nullptr;
    void get(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) const override {
        int idx = animFrameWidth == 64 ? (y * 64 + x) * 2 : ((y / 2) * 32 + x / 2) * 2;
        rgb565ToRgb((frame[idx] << 8) | frame[idx + 1], b, r, g);
    }
};

// La foto nueva tal como llega del backend (photoBuffer, RGB888 real)
class PhotoSource : public reveal::Source {
 public:
    void get(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) const override {
        int idx = (y * 64 + x) * 3;
        r = photoBuffer[idx];
        g = photoBuffer[idx + 1];
        b = photoBuffer[idx + 2];
    }
};

void displayPhotoFromCenter()
{
    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    ScreenSource screen;
    PlayFrameSource playing;
    const reveal::Source *from = &screen;
    if (animPlaying && playBuffer && playFrameCount > 0) {
        uint8_t shown = animCurrentFrame > 0 ? animCurrentFrame - 1 : playFrameCount - 1;
        playing.frame = playBuffer + shown * animFrameSize;
        from = &playing;
    }
    PhotoSource to;
    // 12 KB solo mientras dura la animacion. Si no hay memoria, la foto sale sin mas.
    reveal::Canvas *frame = new (std::nothrow) reveal::Canvas;
    if (frame) {
        reveal::Style style = newPhotoStyle;
        uint32_t duration = reveal::durationMs(style);
        uint32_t frames = 0, slowest = 0;
        unsigned long start = millis();
        for (;;) {
            uint32_t t = millis() - start;
            reveal::render(style, *from, to, showSender ? photoAuthor : "", t, *frame);
            for (int y = 0; y < PANEL_RES_Y; y++)
                for (int x = 0; x < PANEL_RES_X; x++)
                    dma_display->drawPixelRGB888(x, y, frame->px[y][x][2], frame->px[y][x][0], frame->px[y][x][1]);
            frames++;
            uint32_t spent = millis() - start - t;
            if (spent > slowest) slowest = spent;
            if (t >= duration) break;
            esp_task_wdt_reset();
            if (spent < NEW_PHOTO_FRAME_MS) delay(NEW_PHOTO_FRAME_MS - spent);
        }
        uint32_t elapsed = millis() - start;
        LOGF("[PhotoCenter] Animacion %s: %u ms, %u fotogramas (%.1f fps, el mas lento %u ms), heap libre %d",
             reveal::styleName(style), (unsigned)elapsed, (unsigned)frames,
             frames * 1000.0f / elapsed, (unsigned)slowest, ESP.getFreeHeap());
        delete frame;
    } else {
        LOG("[PhotoCenter] Sin memoria para la animacion");
    }
    // Lo que se veia (foto o video) ya no esta: la foto nueva lo sustituye
    stopPlayback();

    // Lo que queda en pantalla es la foto: dejarla en screenBuffer para los fundidos
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            uint8_t r, g, b;
            to.get(x, y, r, g, b);
            drawPixelWithBuffer(x, y, dma_display->color565(b, r, g));
        }
    }
    showPhotoInfo(String(photoTitle), String(photoAuthor));
    showClockOverlay();
    showDevOverlay();
    newPhotoShownAt = millis();
    newPhotoMinShowMs = NEW_PHOTO_MIN_SHOW_MS;
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
    // reqId: si la respuesta llega tarde (tras el timeout) se descarta en vez de
    // pisar la foto de la peticion siguiente
    mqttRequestId++;
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"id\":" + String(id) + ",\"reqId\":" + String(mqttRequestId) + "}";

    armMqttResponseWait();
    if (!netPublish(topic.c_str(), payload.c_str())) {
        LOG("[PhotoCenter] Error publicando request MQTT");
        isLoadingPhoto = false;
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse(RESP_PHOTO, 15000)) {
        esp_task_wdt_reset();
        LOGF("[PhotoCenter] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        // Un video se anuncia cuando ya esta entero: hasta entonces sigue lo que
        // hubiera (si es otro video, reproduciendose) y al acabar la descarga
        // startAnimationPlaybackIfReady() hace la animacion de foto nueva y lo
        // arranca sin pausa. Una foto ya esta descargada: se anuncia ahora.
        if (currentAnimationId > 0) {
            startAnimationDownloadIfNeeded();
        }
        if (currentAnimationId > 0) {
            revealPending = true;
        } else {
            displayPhotoFromCenter();
        }
    } else {
        LOG("[PhotoCenter] Error recibiendo foto via MQTT");
    }

    songShowing = "";
    isLoadingPhoto = false;
    // La siguiente foto de la cola la saca el loop (processPendingPhoto)
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

// Fotos que te han mandado y esperan turno. Las mete el callback MQTT (core 0) y las
// saca el loop (core 1) cuando no hay otra cargandose ni un video nuevo bajando, asi
// que si llegan varias seguidas salen todas, una detras de otra.
static const int NEW_PHOTO_QUEUE_SIZE = 6;
static int newPhotoQueue[NEW_PHOTO_QUEUE_SIZE];
static int newPhotoQueueCount = 0;
static portMUX_TYPE newPhotoQueueMux = portMUX_INITIALIZER_UNLOCKED;

void queueNewPhoto(int id) {
    if (id < 0) return;
    bool dropped = false;
    portENTER_CRITICAL(&newPhotoQueueMux);
    if (newPhotoQueueCount == NEW_PHOTO_QUEUE_SIZE) {
        // Llena: se pierde la mas antigua, que la ultima es la que se espera ver
        for (int i = 1; i < NEW_PHOTO_QUEUE_SIZE; i++) newPhotoQueue[i - 1] = newPhotoQueue[i];
        newPhotoQueueCount--;
        dropped = true;
    }
    newPhotoQueue[newPhotoQueueCount++] = id;
    portEXIT_CRITICAL(&newPhotoQueueMux);
    if (dropped) LOGF("[Photo] Cola de fotos nuevas llena: se descarta la mas antigua");
}

bool hasQueuedNewPhotos() {
    return newPhotoQueueCount > 0;
}

static bool takeNewPhoto(int &id) {
    bool found = false;
    portENTER_CRITICAL(&newPhotoQueueMux);
    if (newPhotoQueueCount > 0) {
        id = newPhotoQueue[0];
        for (int i = 1; i < newPhotoQueueCount; i++) newPhotoQueue[i - 1] = newPhotoQueue[i];
        newPhotoQueueCount--;
        found = true;
    }
    portEXIT_CRITICAL(&newPhotoQueueMux);
    return found;
}

void processPendingPhoto() {
    // Una a una: la siguiente espera a que la anterior se haya anunciado y visto
    if (isLoadingPhoto || revealPending) return;
    // Con el panel apagado por horario esperan y se anuncian al encenderse
    if (screenOff) return;
    if (newPhotoShownAt && millis() - newPhotoShownAt < newPhotoMinShowMs) return;
    int id;
    if (takeNewPhoto(id)) {
        LOGF("[Photo] Procesando foto nueva id=%d (%d en cola)", id, newPhotoQueueCount);
        onReceiveNewPic(id);
    }
}

void onReceiveNewPic(int id)
{
    photoIndex = 1;

    // Un push del usuario cancela la descarga en curso (prefetch del siguiente
    // video), pero lo que se esta viendo sigue hasta que la foto nueva este lista
    resetAnimationDownloadState(true);
    photoPending = false;

    // Mostrar la foto con animación desde el centro via MQTT
    showPhotoFromCenterById(id);
    lastPhotoChange = millis();
    photoIndex = 0;
    songShowing = "";
}

// Vuelve a pintar título y remitente de la foto en pantalla tras cambiar
// showTitle/showSender: devuelve la foto a la franja de abajo y los repinta.
void refreshPhotoInfo()
{
    if (songShowing != "") return; // Spotify en pantalla: no hay foto que tocar
    for (int y = PANEL_RES_Y - 18; y < PANEL_RES_Y; y++) { // cubre el layout de dos líneas
        for (int x = 0; x < PANEL_RES_X; x++) {
            dma_display->drawPixel(x, y, screenBuffer[y][x]);
        }
    }
    titleNeedsScroll = false;
    showPhotoInfo(String(photoTitle), String(photoAuthor));
    buildOverlayMask();
}

void showPhotoInfo(String title, String name)
{
    // El dueño del frame puede ocultar uno u otro desde la app
    if (!showTitle) title = "";
    if (!showSender) name = "";

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

    // Margen entre textos cuando están en la misma línea. Con 4, las cajas negras
    // de titulo y autor podian quedar a ras (0px de foto entre medias); con 5
    // siempre queda al menos 1px de hueco.
    const int horizontalMargin = 5;

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
            nameX = PANEL_RES_X - nameW;
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
            nameX = 1;
            dma_display->getTextBounds(name, 1, nameY, &nameX1, &nameY1, &nameW, &nameH);
            dma_display->fillRect(0, nameY1 - 1, nameW + 3, nameH + 2, myBLACK);
            dma_display->setCursor(1, nameY);
            dma_display->print(name);
        }
    }

}

// Encola el request de un frame reintentando si la cola de red esta llena,
// pero acotado: resetea el WDT (un while sin reset causo el TASK_WDT del
// soak 2026-08-07) y desiste tras capMs — el timeout de descarga re-pedira
// los frames que falten cuando la red se recupere.
static bool requestFrameBounded(int animId, int frameIdx, unsigned long capMs) {
    unsigned long t0 = millis();
    while (!requestAnimationFrame(animId, frameIdx)) {
        esp_task_wdt_reset();
        updateAnimationPlayback();
        if (millis() - t0 > capMs) return false;
    }
    return true;
}

// Fotogramas pedidos y aun sin llegar, como mucho. Pedirlos todos de golpe dejaba ~1 MB
// esperando en el broker y el frame lo traga a ~60 KB/s: un aviso de foto nueva o la
// respuesta a una peticion de foto llegaban detras de todo eso (hasta 20 s, y la
// peticion caducaba), tambien tras cancelar la descarga. Con 8 en camino la descarga
// va igual de rapido y lo que llegue despues espera como mucho ~1,5 s.
static const uint8_t ANIM_REQUEST_WINDOW = 8;
static uint8_t animNextSlot = 0; // los slots se piden en orden: [0, animNextSlot) ya pedidos

// Pide los siguientes fotogramas hasta tener ANIM_REQUEST_WINDOW en camino. Llamada
// desde el loop principal, barata si no hay descarga.
void pumpAnimationRequests() {
    if (currentAnimationId <= 0 || !animBuffer || animReady) return;
    while (animNextSlot < animFrameCount &&
           animNextSlot - animFramesReceived < ANIM_REQUEST_WINDOW) {
        if (!requestAnimationFrame(currentAnimationId, animNextSlot * animFrameStep)) return;
        animNextSlot++;
    }
}

void startAnimationDownloadIfNeeded() {
    if (currentAnimationId > 0 && animFrameCount > 0 && !animReady) {
        // Bajo lock: la tarea de red puede estar escribiendo un frame rezagado
        // de la animacion anterior en el buffer que vamos a liberar
        animBufLock();
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
            // En size_t: con frames de 2 KB un bloque grande desbordaria un u8
            size_t maxFrames = safeBlock / animFrameSize;
            if (maxFrames > MAX_ANIM_FRAMES) maxFrames = MAX_ANIM_FRAMES;

            if (maxFrames < 2) {
                LOGF("[Anim] Not enough RAM for animation (largest block: %d)", maxBlock);
                currentAnimationId = -1;
                animFrameCount = 0;
                animBufUnlock();
                return;
            }

            if (maxFrames < totalBackendFrames) {
                // Sample evenly to cover full duration
                animFrameStep = (totalBackendFrames + maxFrames - 1) / maxFrames; // ceil division
                framesToUse = totalBackendFrames / animFrameStep;
                if (framesToUse < 2) framesToUse = 2;
            }
        }

        // Red de seguridad comun a ambas ramas: los slots indexan animBuffer y el
        // bitmap, asi que un backend que anuncie mas frames de los que soportamos
        // (u8 admite hasta 255) tiene que acabar submuestreado, no desbordando.
        if (framesToUse > MAX_ANIM_FRAMES) {
            animFrameStep = (totalBackendFrames + MAX_ANIM_FRAMES - 1) / MAX_ANIM_FRAMES;
            framesToUse = totalBackendFrames / animFrameStep;
            if (framesToUse > MAX_ANIM_FRAMES) framesToUse = MAX_ANIM_FRAMES;
            LOGF("[Anim] %d frames anunciados > MAX_ANIM_FRAMES: submuestreo a %d (step=%d)",
                 totalBackendFrames, framesToUse, animFrameStep);
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
            animBufUnlock();
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
        animSlotsClear(animFramesBitmap);
        animRetryCount = 0;
        animBufUnlock(); // los frames ya pueden empezar a llegar mientras encolamos

        // Descarga en ventana: la tarea de red (core 0) publica y drena las
        // respuestas; el loop va pidiendo mas segun llegan (pumpAnimationRequests).
        animNextSlot = 0;
        animDownloadStartTime = millis();
        pumpAnimationRequests();
    }
}

// La descarga termino: sustituye lo que haya en pantalla (foto anterior o video
// que acaba sus vueltas) por el primer frame + titulo de la animacion y arranca
// el playback. El buffer de descarga pasa a ser el de reproduccion, dejando la
// maquinaria de descarga libre para el prefetch del siguiente. Llamada desde el
// loop principal.
bool startAnimationPlaybackIfReady() {
    if (!animReady || currentAnimationId <= 0 || !animBuffer) return false;
    // Si hay un video reproduciendose, dejarle terminar sus vueltas antes del swap,
    // salvo que el nuevo sea uno que te acaban de mandar: ese entra ya
    if (animPlaying && animLoopCount < playMaxLoops && !revealPending) return false;

    int animId = currentAnimationId; // para el log (el reset lo pone a -1)
    unsigned long sinceReady = animReadyTime ? millis() - animReadyTime : 0;
    unsigned long tSwap = millis();

    // Video nuevo: la animacion de foto nueva sale de lo que se este viendo y acaba
    // en su primer frame (photoBuffer), con el titulo puesto. Libera el video anterior.
    bool reveal = revealPending;
    if (reveal) {
        revealPending = false;
        displayPhotoFromCenter();
    }

    animBufLock(); // transferencia del buffer: que la tarea de red no escriba a mitad
    if (playBuffer) free(playBuffer);
    playBuffer = animBuffer;
    animBuffer = nullptr;
    playFrameCount = animFrameCount;
    playFrameInterval = animFrameInterval;
    unsigned long loopMs = (unsigned long)playFrameCount * playFrameInterval;
    playMaxLoops = loopMs > 0 ? max(3UL, (unsigned long)secsPhotos / loopMs) : 3UL;

    animPlaying = false;
    resetAnimationDownloadState(false);
    animBufUnlock();

    if (!reveal) displayPhotoWithFade();
    buildOverlayMask(); // tras pintar el titulo, para que el layout sea el definitivo
    animCurrentFrame = 0;
    animLoopCount = 0;
    animLastFrameTime = millis();
    animPlaying = true;
    lastPhotoChange = millis(); // el intervalo de foto empieza cuando la animacion se ve
    if (reveal) newPhotoMinShowMs = max(NEW_PHOTO_MIN_SHOW_MS, loopMs);
    animReadyTime = 0;
    LOGF("[Anim] Starting playback id=%d (%lums desde ready, %s %lums)",
         animId, sinceReady, reveal ? "anuncio" : "swap+fade", millis() - tSwap);
    return true;
}

void drawAnimationFrame(uint8_t frameIndex) {
    uint8_t* frame = playBuffer + frameIndex * animFrameSize;

    if (animFrameWidth == 64) {
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                if (overlayMaskGet(x, y)) continue; // skip overlay pixels
                int idx = (y * 64 + x) * 2;
                uint16_t color = (frame[idx] << 8) | frame[idx + 1];
                dma_display->drawPixel(x, y, color);
            }
        }
    } else {
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                int px = x * 2, py = y * 2;
                int idx = (y * 32 + x) * 2;
                uint16_t color = (frame[idx] << 8) | frame[idx + 1];
                // Draw 2x2 block, skipping overlay pixels
                if (!overlayMaskGet(px, py))     dma_display->drawPixel(px, py, color);
                if (!overlayMaskGet(px+1, py))   dma_display->drawPixel(px+1, py, color);
                if (!overlayMaskGet(px, py+1))   dma_display->drawPixel(px, py+1, color);
                if (!overlayMaskGet(px+1, py+1)) dma_display->drawPixel(px+1, py+1, color);
            }
        }
    }
}

void updateAnimationPlayback() {
    if (!animPlaying || playFrameCount == 0 || !playBuffer) return;

    unsigned long now = millis();
    if (now - animLastFrameTime < playFrameInterval) return;

    // Catch-up: si el loop va lento (p.ej. recibiendo frames de una descarga en
    // paralelo, ~130ms por mensaje), saltamos los fotogramas atrasados para
    // mantener la velocidad real del video en vez de reproducir a camara lenta.
    uint16_t steps = (now - animLastFrameTime) / playFrameInterval;

    // [Diag] acumular los saltos de catch-up y volcarlos como mucho 1 vez/seg:
    // el peor retraso individual delata el bloqueo que congela el video
    {
        static uint16_t skippedAccum = 0;
        static unsigned long worstGapMs = 0;
        static unsigned long lastSkipReport = 0;
        if (steps > 1) {
            skippedAccum += steps - 1;
            unsigned long gap = now - animLastFrameTime;
            if (gap > worstGapMs) worstGapMs = gap;
        }
        if (skippedAccum > 0 && now - lastSkipReport >= 1000) {
            LOGF("[Diag] Video atrasado: %u frames saltados (peor hueco %lums, interval=%lums, loop=%lu/%lu, dl id=%d %d/%d)",
                 skippedAccum, worstGapMs, playFrameInterval, animLoopCount, playMaxLoops,
                 currentAnimationId, animFramesReceived, animFrameCount);
            skippedAccum = 0;
            worstGapMs = 0;
            lastSkipReport = now;
        }
    }
    for (uint16_t i = 1; i < steps; i++) {
        animCurrentFrame++;
        if (animCurrentFrame >= playFrameCount) {
            animCurrentFrame = 0;
            animLoopCount++;
        }
    }

    drawAnimationFrame(animCurrentFrame);
    animCurrentFrame++;
    if (animCurrentFrame >= playFrameCount) {
        animCurrentFrame = 0;
        animLoopCount++;
    }
    animLastFrameTime += (unsigned long)steps * playFrameInterval; // conserva la fase

    if (animLoopCount >= playMaxLoops) {
        // Si el siguiente video se esta descargando (o ya esta listo), seguimos en
        // bucle hasta que startAnimationPlaybackIfReady haga el swap.
        if (currentAnimationId > 0) {
            // [Diag] estas vueltas extra son exactamente "la ultima iteracion se
            // cuelga antes del siguiente video": registrar cuanto duran y por qué
            static unsigned long lastExtraLoopLog = 0;
            if (millis() - lastExtraLoopLog >= 1000) {
                LOGF("[Diag] Vueltas extra (%lu/%lu): esperando descarga id=%d (%d/%d frames, ready=%d, retry=%d, %lums sin progreso)",
                     animLoopCount, playMaxLoops, currentAnimationId,
                     animFramesReceived, animFrameCount, (int)animReady, animRetryCount,
                     animDownloadStartTime ? millis() - animDownloadStartTime : 0);
                lastExtraLoopLog = millis();
            }
            return;
        }

        if (photoPending) {
            // Prefetch devolvio una foto estatica: ahora es su turno
            stopPlayback();
            photoPending = false;
            displayPhotoWithFade();
            lastPhotoChange = millis();
        } else {
            LOG("[Anim] Playback finished (loop limit reached)");
            stopPlayback();
            // No reseteamos lastPhotoChange: la reproduccion ya consumio el intervalo
            // (playMaxLoops esta calculado para durar ~secsPhotos), asi que la
            // siguiente foto sale enseguida en vez de dejar el ultimo frame congelado.
        }
    }
}

// El prefetch se dispara cuando al video actual le quedan <= ANIM_PREFETCH_MS de
// reproduccion. Solo en v2 (PSRAM): sin PSRAM no hay RAM para dos buffers.
bool animPrefetchDue() {
    if (!animPlaying || !hasPsram || photoPending || currentAnimationId > 0) return false;
    const unsigned long ANIM_PREFETCH_MS = 15000;
    unsigned long loopMs = (unsigned long)playFrameCount * playFrameInterval;
    if (loopMs == 0) return true;
    unsigned long total = playMaxLoops * loopMs;
    unsigned long played = animLoopCount * loopMs; // aprox: ignora el frame actual
    unsigned long remaining = total > played ? total - played : 0;
    return remaining <= ANIM_PREFETCH_MS;
}

// Para la reproduccion y libera su buffer. No toca el estado de descarga.
void stopPlayback() {
    animPlaying = false;
    animCurrentFrame = 0;
    animLoopCount = 0;
    playFrameCount = 0;
    overlayMaskClear();
    if (playBuffer) {
        free(playBuffer);
        playBuffer = nullptr;
        LOGF("[Anim] Play buffer freed (free heap: %d)", ESP.getFreeHeap());
    }
}

// Resetea el estado de descarga (y opcionalmente libera su buffer). No toca la
// reproduccion en curso.
void resetAnimationDownloadState(bool freeBuffer) {
    animBufLock(); // llamable desde ambos cores (handlePhotoResponse corre en la tarea de red)
    currentAnimationId = -1;
    animReady = false;
    animFrameCount = 0;
    animFramesReceived = 0;
    animSlotsClear(animFramesBitmap);
    animFrameStep = 1;
    animFrameInterval = 200;
    animRetryCount = 0;
    animDownloadStartTime = 0;
    animNextSlot = 0;
    if (freeBuffer && animBuffer) {
        free(animBuffer);
        animBuffer = nullptr;
    }
    animBufUnlock();
}

void stopAnimation() {
    stopPlayback();
    resetAnimationDownloadState(true);
    photoPending = false;
    revealPending = false;
}

// Re-requests any frames still missing after a stall window (no frame received
// for ANIM_DOWNLOAD_TIMEOUT_MS — el cronometro se resetea con cada frame que llega).
// Called from the main loop; cheap when there's no animation downloading.
void checkAnimationDownloadTimeout() {
    if (animReady || currentAnimationId <= 0 || animBuffer == nullptr) return;
    if (animDownloadStartTime == 0) return;

    const unsigned long ANIM_DOWNLOAD_TIMEOUT_MS = 5000;
    const uint8_t ANIM_MAX_RETRIES = 3;

    if (millis() - animDownloadStartTime < ANIM_DOWNLOAD_TIMEOUT_MS) return;

    if (animRetryCount >= ANIM_MAX_RETRIES) {
        LOGF("[Anim] Giving up after %d retries (%d/%d frames)",
             animRetryCount, animFramesReceived, animFrameCount);
        if (revealPending) {
            // El video que te han mandado no ha bajado entero: se anuncia su primer
            // frame como foto, que algo hay que enseñar
            revealPending = false;
            resetAnimationDownloadState(true);
            displayPhotoFromCenter();
            lastPhotoChange = millis();
        } else if (animPlaying) {
            // Prefetch fallido: el video actual sigue; al terminar sus vueltas el
            // loop principal pedira otra foto por el camino normal
            resetAnimationDownloadState(true);
        } else {
            stopAnimation();
            // La foto nueva nunca llego a pintarse (seguia la anterior en pantalla):
            // mostramos su primer frame como foto estatica
            displayPhotoWithFade();
        }
        return;
    }

    // Copiar el estado bajo lock (el bitmap ya no cabe en una palabra: lectura
    // no atomica) y publicar fuera, para no retener el mutex mientras la cola de
    // red drena
    animBufLock();
    uint8_t bitmap[ANIM_BITMAP_BYTES];
    for (uint8_t i = 0; i < ANIM_BITMAP_BYTES; i++) bitmap[i] = animFramesBitmap[i];
    uint8_t frameCount = animFrameCount;
    int animId = currentAnimationId;
    animBufUnlock();

    uint8_t missing = 0;
    for (uint8_t slot = 0; slot < frameCount && slot < animNextSlot; slot++) {
        if (!animSlotIsSet(bitmap, slot)) {
            if (!requestFrameBounded(animId, slot * animFrameStep, 3000)) {
                LOGF("[Anim] Cola de red saturada durante retry (slot %d): se reintentara en la proxima ronda", slot);
                break;
            }
            missing++;
        }
    }
    animRetryCount++;
    animDownloadStartTime = millis();
    LOGF("[Anim] Retry %d: re-requested %d missing frames", animRetryCount, missing);
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
