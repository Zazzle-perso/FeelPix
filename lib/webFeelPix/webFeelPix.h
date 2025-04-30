#ifndef WEB_FEELPIX_H
#define WEB_FEELPIX_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Objet serveur partagé
extern AsyncWebServer server;

// Initialisation à appeler dans setup()
void initWebServer();

// Prototypes des handlers
void handleList(AsyncWebServerRequest *req);
void handleSelect(AsyncWebServerRequest *req);
void handleUpload(AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final);
// Supprime un fichier /lire_smileyX.txt et met à jour la liste
void handleDelete(AsyncWebServerRequest *req);

#endif // WEB_FEELPIX_H
