// Mensajes de pantalla para el usuario final, centralizados para poder
// cambiar el idioma en un solo sitio. Solo ASCII: las fuentes GFX
// (Picopixel) no tienen glifos acentuados.
#pragma once

// Estados que piden accion del usuario
#define MSG_SETUP        "Open the app"
#define MSG_LINK_APP     "Link in the app"

// Progreso de arranque
#define MSG_CONNECTING   "Connecting..."
#define MSG_CONNECTED    "Connected!"
#define MSG_ALMOST_READY "Almost ready..."
#define MSG_ONE_MOMENT   "One moment..."
#define MSG_READY        "Ready!"
#define MSG_DONE         "Done!"
#define MSG_RESTARTING   "Restarting..."

// Errores
#define MSG_WIFI_ERROR   "WiFi error"
#define MSG_NET_ERROR    "Network error"

// Pantalla de actualizacion OTA
#define MSG_UPDATING     "Updating"
