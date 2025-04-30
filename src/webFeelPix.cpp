#include "webFeelPix.h"
#include "Fichier.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>


/**
 *  prototype des fonctions 
 */
void initWebServer();
void handleList(AsyncWebServerRequest* req);
void handleSelect(AsyncWebServerRequest* req);
void handleUpload(AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final);

/**
 * @brief Objet serveur HTTP partagé.
 */
extern AsyncWebServer server;

/**
 * @brief Initialise et configure le serveur web (Wi-Fi, SPIFFS, routes).
 */
void initWebServer() {
  // Monte SPIFFS si besoin
  SPIFFS.begin(false);

  // Mode Wi-Fi AP
  WiFi.softAP(espName,MDP_WIFI);

  // 1) Routes dynamiques
  server.on("/list", HTTP_GET,  handleList);
  server.on("/select", HTTP_GET, [](AsyncWebServerRequest *req){
    Serial.println(">>> /select reçu");    // TRACE N°1
    if (!req->hasParam("file")) {
      Serial.println("!!! /select missing file"); // TRACE N°2
      req->send(400, "application/json", "{\"error\":\"file manquant\"}");
      return;
    }
    String name = req->getParam("file")->value();
    Serial.printf(">>> fichier demandé : %s\n", name.c_str()); // TRACE N°3
    // … reste du code …
  });
  server.on("/upload", HTTP_POST,
    // 1er callback: onRequest (vide, on n’envoie rien ici)
    [](AsyncWebServerRequest *req){},
    // 2e callback: onUpload
    handleUpload
  );

  // 2) Handler 404 pour tracer ce qui passe
  server.onNotFound([](AsyncWebServerRequest *req){
    Serial.printf("404 sur %s\n", req->url().c_str());
    req->send(404, "text/plain", "Not found");
  });

  // 3) Enfin, la route statique
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.begin();
}

/**
 * @brief Handler de la route GET /list.
 * 
 * Envoie au client un JSON listant les noms des fichiers
 * de smileys (ceux dont le nom commence par "lire_").
 * 
 * @param req Objet représentant la requête HTTP entrante.
 */
void handleList(AsyncWebServerRequest *req) {
  DynamicJsonDocument doc(256);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < fichiers.nbFichiers; i++) {
    String nom = fichiers.noms[i];
    if (nom.startsWith("/lire_")) arr.add(nom);
  }
  String payload;
  serializeJson(doc, payload);
  req->send(200, "application/json", payload);
}

/**
 * @brief Handler de la route POST /select.
 * 
 * Lit le corps JSON { file: "<nom>" }, met à jour
 * la variable globale `fichierCourant` et renvoie un statut.
 * 
 * @param req Objet représentant la requête HTTP entrante.
 */
void handleSelect(AsyncWebServerRequest *req) {
  String name;

  // 1) Récupérer le paramètre "file"
  if (req->method() == HTTP_GET) {
    // GET /select?file=...
    if (!req->hasParam("file")) {
      req->send(400, "application/json", "{\"error\":\"paramètre 'file' manquant\"}");
      return;
    }
    name = req->getParam("file")->value();
  }
  else if (req->method() == HTTP_POST) {
    // POST JSON dans le corps
    if (!req->hasParam("body", true)) {
      req->send(400, "application/json", "{\"error\":\"no body\"}");
      return;
    }
    String b = req->getParam("body", true)->value();
    DynamicJsonDocument doc(128);
    auto err = deserializeJson(doc, b);
    if (err) {
      req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
      return;
    }
    name = doc["file"].as<String>();
  }
  else {
    req->send(405, "application/json", "{\"error\":\"méthode non supportée\"}");
    return;
  }

  // 2) Chercher l’index du fichier
  int idx = -1;
  for (int i = 0; i < fichiers.nbFichiers; i++) {
    // on stocke toujours dossiers.noms[i] avec un slash initial
    if (fichiers.noms[i].endsWith(name)) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    req->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  // 3) Mettre à jour et afficher
  fichierCourant = idx;
  lireFichierEtAfficher(fichiers.noms[idx].c_str());

  // 4) Répondre OK
  req->send(200, "application/json", "{\"ok\":true}");
}


/**
 * @brief Handler de l’upload de fichier via /upload.
 *
 * Lire le body POST complet (text/plain) contenant la carte pixel,
 * puis créer un nouveau fichier nommé automatiquement :
 *   lire_smiley0.txt, lire_smiley1.txt, … jusqu’à MAX_FICHIERS,
 * en réutilisant les indices cycliquement (modulo).
 *
 * @param req   Requête HTTP AsyncWebServerRequest*
 * @param filename   Ignoré par AsyncTCP pour POST bruts
 * @param index      Offset dans le stream (toujours 0 ici)
 * @param data       Données reçues
 * @param len        Longueur des données
 * @param final      true si c’est la fin du body
 */
void handleUpload(AsyncWebServerRequest* req,
  String /*filename*/,
  size_t index,
  uint8_t* data,
  size_t len,
  bool final) {
static String buffer;

// 1) Reset au début
if (index == 0) {
buffer = "";
Serial.println("[UPLOAD] Début upload");
}

// 2) Accumuler les données
buffer += String((char*)data, len);
Serial.printf("[UPLOAD] chunk index=%u, len=%u, total=%u\n",
index, len, buffer.length());

// 3) À la fin, écrire en SPIFFS
if (final) {
Serial.println("[UPLOAD] Body complet reçu");

// --- Choix d’un index libre ---
bool used[MAX_FICHIERS] = { false };
for (int i = 0; i < fichiers.nbFichiers; i++) {
String n = fichiers.noms[i];               // "/lire_smiley7.txt"
int start = n.indexOf("lire_smiley") + 12;
int num = n.substring(start, n.lastIndexOf(".txt")).toInt();
if (num >= 0 && num < MAX_FICHIERS) used[num] = true;
}
int idx = 0;
while (idx < MAX_FICHIERS && used[idx]) idx++;
if (idx >= MAX_FICHIERS) idx = 0;

// --- Construire chemin SPIFFS ---
String name = String("lire_smiley") + idx + ".txt";
String path = "/" + name;
Serial.printf("[UPLOAD] chemin = %s\n", path.c_str());

// Supprimer l’ancien si besoin
if (SPIFFS.exists(path)) {
SPIFFS.remove(path);
Serial.println("[UPLOAD] ancien fichier supprimé");
}

// Écriture
File f = SPIFFS.open(path, FILE_WRITE);
if (!f) {
Serial.println("[UPLOAD] SPIFFS.open a échoué !");
req->send(500, "application/json", "{\"error\":\"écriture impossible\"}");
return;
}
f.print(buffer);
f.close();
Serial.println("[UPLOAD] écriture terminée");

// Mise à jour et réponse
recenserFichiersLisibles();
req->send(200, "application/json",
String("{\"ok\":true,\"file\":\"") + name + "\"}");
}
}

// Supprime un fichier /lire_smileyX.txt et met à jour la liste
void handleDelete(AsyncWebServerRequest *req) {
  // Récupère le paramètre file (sans slash initial)
  if (!req->hasParam("file")) {
    req->send(400, "application/json", "{\"error\":\"paramètre 'file' manquant\"}");
    return;
  }
  String name = req->getParam("file")->value();    // ex: "lire_smiley3.txt"
  String path = "/" + name;                        // "/lire_smiley3.txt"

  // Vérifier l’existence
  if (!SPIFFS.exists(path)) {
    req->send(404, "application/json", "{\"error\":\"fichier introuvable\"}");
    return;
  }

  // Supprimer
  if (!SPIFFS.remove(path)) {
    req->send(500, "application/json", "{\"error\":\"échec suppression\"}");
    return;
  }

  // Mettre à jour la liste et répondre
  recenserFichiersLisibles();
  req->send(200, "application/json", "{\"ok\":true}");
}



