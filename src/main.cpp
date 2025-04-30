#include "Fichier.h"
#include <SPI.h>
#include "SPIFFS.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "webFeelPix.h"

/**
 * @file main.cpp
 * @project FeelPix – Afficheur d'humeur
 * @author Denizon
 * @date 22 avril 2025
 *
 * @brief
 * Ce programme contrôle un écran TFT (ST7735) connecté à un ESP32-WROOM afin d'afficher
 * des visuels représentant des "humeurs" sous forme pixelisée. L'utilisateur peut
 * parcourir les différentes humeurs à l'aide de trois boutons : suivant, précédent, aléatoire.
 *
 * Les images sont stockées sous forme de fichiers dans la mémoire flash interne (SPIFFS)
 * et décrivent une matrice de pixels ainsi que leur couleur.
 *
 * Objectifs :
 * - Afficher dynamiquement des visuels émotionnels ou symboliques
 * - Naviguer facilement via des boutons physiques
 * - Préparer une future extension avec lecteur de carte SD et éditeur graphique externe
 *
 * Matériel :
 * - ESP32-WROOM
 * - Écran TFT ST7735 (3.3 V, interface SPI)
 * - 3 boutons-poussoirs pour l'interaction utilisateur
 * - Alimentation autonome à terme (batterie)
 */

// --- Déclaration des PIN ---
#define PIN_SCK      18  // clock
#define PIN_SDA      23  // data
#define PIN_RES      4   // reset
#define PIN_RS       2   // command
#define PIN_CS       5   // chip select

#define PIN_BOUTON_BLEU  12
#define PIN_BOUTON_VERT  14
#define PIN_BOUTON_ROUGE 27

// Codes pour switch/case
#define BTN_NONE   0
#define BTN_BLEU   1
#define BTN_VERT   2
#define BTN_ROUGE  3

#define SMILEY_WIDTH   16
#define SMILEY_HEIGHT  16
#define WIDTH_SCREEN   128
#define HEIGHT_SCREEN  160

#define TIME_INIT_DISPLAY 5000//duree d'affichage du message à l'initialisation 


// --- Déclaration des variables et constantes ---
Adafruit_ST7735 tft(PIN_CS, PIN_RS, PIN_RES);

struct PixelMapping {
  char symbole;
  uint16_t couleur;
};

PixelMapping correspondance[] = {
  {'.', ST77XX_BLACK},
  {'Y', ST77XX_YELLOW},
  {'R', ST77XX_RED},
  {'B', ST77XX_BLUE},
  {'W', ST77XX_WHITE},
  {'G', ST77XX_GREEN},
};

volatile uint8_t dernierAppui = BTN_NONE;
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastDebounce = 0;

FichiersLisibles fichiers;  // défini extern

// ** Nom unique de l'ESP **
String espName;                   // ex "ESP789B"

bool wifiEnabled = true;

// --- Prototypes ---
uint16_t getCouleur(char c);
int calculerEchellePixel(const String& ligne, int largeur);
void afficherLigneSmiley(const String& ligne, int idx, int taillePixel, int offsetY);
void recenserFichiersLisibles();
void lireFichierEtAfficher(const char* nomFichier);  // <— ajouté !
void IRAM_ATTR boutonBleuAppuye();
void IRAM_ATTR boutonVertAppuye();
void IRAM_ATTR boutonRougeAppuye();

int fichierCourant = 0;
AsyncWebServer server(80);
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t mainTaskHandle = NULL;

void webTask(void* pv) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void mainTask(void* pv) {
  Serial.println(">> mainTask START");
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(200);
  while (true) {
    if (dernierAppui != BTN_NONE && millis() - lastDebounce > DEBOUNCE_MS) {
      lastDebounce = millis();
      switch (dernierAppui) {
        case BTN_BLEU:
          fichierCourant = (fichierCourant + 1) % fichiers.nbFichiers;
          break;
        case BTN_VERT:
          fichierCourant = (fichierCourant + fichiers.nbFichiers - 1) % fichiers.nbFichiers;
          break;
        case BTN_ROUGE:
        wifiEnabled = !wifiEnabled;
        if (wifiEnabled) {
          WiFi.mode(WIFI_AP);
          WiFi.softAP(espName, MDP_WIFI);
          delay(200);
          tft.fillScreen(ST77XX_BLACK);
          tft.setTextSize(2);
          tft.setTextColor(ST77XX_WHITE);
          tft.setCursor(0, HEIGHT_SCREEN/2 - 8);
          tft.print("WiFi ON");
          tft.setTextSize(1);
          tft.setCursor(0, HEIGHT_SCREEN/2 + 8);
          tft.print(WiFi.softAPIP().toString());
          tft.setTextSize(2);
        } else {
          WiFi.softAPdisconnect(true);
          tft.fillScreen(ST77XX_BLACK);
          tft.setTextSize(2);
          tft.setTextColor(ST77XX_WHITE);
          tft.setCursor(0, HEIGHT_SCREEN/2);
          tft.print("WiFi OFF");
        }
        delay(TIME_INIT_DISPLAY);
      }
      lireFichierEtAfficher(fichiers.noms[fichierCourant].c_str());
      dernierAppui = BTN_NONE;
    }
    vTaskDelayUntil(&lastWake, period);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  esp_log_level_set("*", ESP_LOG_VERBOSE);

  // Initialisation écran
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

    // 1) Démarre un AP temporaire (avec un SSID par défaut)
    WiFi.mode(WIFI_AP);
    WiFi.softAP("tmpSSID", MDP_WIFI);
    delay(100);
  
    // 2) Lis la MAC AP
    String apMac = WiFi.softAPmacAddress();  // maintenant valide
    apMac.replace(":", "");
    String tail = apMac.substring(apMac.length() - 6);
    espName = "ESP_" + tail;
  
    // 3) Redémarre l’AP avec le SSID définitif
    WiFi.softAPdisconnect(true);
    delay(50);
    WiFi.softAP(espName.c_str(), MDP_WIFI);
  
    // 4) Affiche espName
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    int16_t x = (WIDTH_SCREEN - espName.length()*12)/2;
    if (x < 0) x = 0;
    tft.setCursor(x, (HEIGHT_SCREEN/2)-12);
    tft.print(espName);
    delay(TIME_INIT_DISPLAY);

  // Boutons
  pinMode(PIN_BOUTON_BLEU, INPUT_PULLUP);
  pinMode(PIN_BOUTON_VERT, INPUT_PULLUP);
  pinMode(PIN_BOUTON_ROUGE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOUTON_BLEU), boutonBleuAppuye, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOUTON_VERT), boutonVertAppuye, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOUTON_ROUGE), boutonRougeAppuye, FALLING);

  // SPIFFS (format if failed)
  if (!SPIFFS.begin(true)) {
    Serial.println("Échec du montage SPIFFS (formatage auto)");
    while (1);
  }

  Serial.println(">> VERSION DEBUG V1.1 <<");
  recenserFichiersLisibles();
  lireFichierEtAfficher(fichiers.noms[fichierCourant].c_str());

  // Routes
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.on("/list", HTTP_GET, handleList);
  server.on("/select", HTTP_GET, handleSelect);
  server.on("/select", HTTP_POST, handleSelect);

server.on("/upload", HTTP_POST,
  // onRequest – on ne renvoie rien ici
  [](AsyncWebServerRequest *req) {},
  // onUpload – appelé pour chaque chunk multipart/form-data
  handleUpload
);

// Route de suppression
server.on("/delete", HTTP_GET, handleDelete);

  server.onNotFound([](AsyncWebServerRequest* req){
    Serial.printf("404 sur %s\n", req->url().c_str());
    req->send(404, "text/plain", "Not found");
  });
  // Dans setup(), juste avant server.begin():
server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req){
  // Si vous avez un /favicon.ico dans SPIFFS, envoyez-le :
  if (SPIFFS.exists("/favicon.ico")) {
    req->send(SPIFFS, "/favicon.ico", "image/x-icon");
  } else {
    // Sinon on répond “Pas de contenu” pour couper court au 404
    req->send(204);
  }
});

  server.begin();

  // Tâches RTOS
  xTaskCreatePinnedToCore(webTask, "WebServer",   8192, NULL, 2, &webTaskHandle, 0);
  xTaskCreatePinnedToCore(mainTask, "MainLogic", 8192, NULL, 1, &mainTaskHandle, 1);
}

void IRAM_ATTR boutonBleuAppuye()  { dernierAppui = BTN_BLEU; }
void IRAM_ATTR boutonVertAppuye()  { dernierAppui = BTN_VERT; }
void IRAM_ATTR boutonRougeAppuye() { dernierAppui = BTN_ROUGE; }

void recenserFichiersLisibles() {
  Serial.println("recenserFichiersLisibles");
  fichiers.nbFichiers = 0;

  File root = SPIFFS.open("/");
  File f = root.openNextFile();

  while (f && fichiers.nbFichiers < MAX_FICHIERS) {
    String nom = f.name();  // peut rendre "/lire_smileyX.txt" ou "lire_smileyX.txt"
    Serial.println(nom);

    // Normalise : supprime un slash initial éventuel
    if (nom.startsWith("/")) {
      nom = nom.substring(1);
    }

    // On ne retient que ceux qui commencent par "lire_"
    if (nom.startsWith("lire_")) {
      // On stocke toujours avec un slash initial pour SPIFFS.open()
      fichiers.noms[fichiers.nbFichiers++] = "/" + nom;
    }

    f = root.openNextFile();
  }

  Serial.print("Nombre de fichiers lisibles : ");
  Serial.println(fichiers.nbFichiers);
}


uint16_t getCouleur(char c) {
  for (auto &m : correspondance) {
    if (m.symbole == c) return m.couleur;
  }
  return ST77XX_BLACK;
}

int calculerEchellePixel(const String& ligne, int largeur) {
  int cols = ligne.length();
  return cols ? (largeur / cols) : 1;
}

void afficherLigneSmiley(const String& ligne, int idx,
                         int taillePixel, int offsetY) {
  for (int x = 0; x < ligne.length(); x++) {
    uint16_t col = getCouleur(ligne[x]);
    for (int dx = 0; dx < taillePixel; dx++) {
      for (int dy = 0; dy < taillePixel; dy++) {
        tft.drawPixel(x*taillePixel + dx,
                      idx*taillePixel + dy + offsetY,
                      col);
      }
    }
  }
}

// --- Implémentation manquante jusqu'ici ---
void lireFichierEtAfficher(const char* nomFichier) {
  File fichier = SPIFFS.open(nomFichier);
  if (!fichier || fichier.isDirectory()) {
    Serial.printf("Erreur d'ouverture SPIFFS : %s\n", nomFichier);
    return;
  }

  // Première passe : lecture de la 1ère ligne + comptage total
  String ligneTemp = fichier.readStringUntil('\n');
  ligneTemp.trim();
  int pixelSize = calculerEchellePixel(ligneTemp, tft.width());

  int totalLines = 1;
  while (fichier.available()) {
    fichier.readStringUntil('\n');
    totalLines++;
  }

  // Calcul offset vertical
  int totalHeight = totalLines * pixelSize;
  int offsetY = (tft.height() - totalHeight) / 2;

  // Deuxième passe : réinitialiser et afficher
  fichier.seek(0);
  tft.fillScreen(ST77XX_BLACK);
  int ligneIndex = 0;
  while (fichier.available()) {
    String ligne = fichier.readStringUntil('\n');
    ligne.trim();
    afficherLigneSmiley(ligne, ligneIndex, pixelSize, offsetY);
    ligneIndex++;
  }
  fichier.close();
}

void loop() {}
