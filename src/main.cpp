#include <Arduino.h>
#include <SPI.h>
#include "SPIFFS.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

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

 /**
 * @brief Déclaration des PIN
 */
#define PIN_SCK 18 // clock
#define PIN_SDA 23 // data
#define PIN_RES 4 // reset
#define PIN_RS 2 // command
#define PIN_CS 5// chip select

#define PIN_BOUTON_BLEU 12
#define PIN_BOUTON_VERT 14
#define PIN_BOUTON_ROUGE 27

// Codes pour switch/case
#define BTN_NONE 0
#define BTN_BLEU 1
#define BTN_VERT 2
#define BTN_ROUGE 3

// Exemple : smiley 16x16 avec jaune pour les pixels utiles
#define SMILEY_WIDTH 16
#define SMILEY_HEIGHT 16

#define WIDTH_SCREEN 128
#define HEIGHT_SCREEN 160

#define MAX_FICHIERS 10 // nomnre de fichier limite lisibles

/**
 * @brief Déclaration des variables et constantes
 */
Adafruit_ST7735 tft(PIN_CS, PIN_RS, PIN_RES);
/**
 * @brief On créé une structure qui va associer le code du fichier smiley lu à une couleur interprétable par l'écran
 */

 struct PixelMapping {
  char symbole;
  uint16_t couleur;
};

/**
 * @brief Structure qui va s'initialiser avec le nombre de fichiers qui contiennent des informations a afficher sur l'ecran et un tableau avec ces noms, le nombre est limite a 10 fichiers
 * les fichiers lisibles commencent par lire_
 */
struct FichiersLisibles {
  int nbFichiers = 0;
  String noms[MAX_FICHIERS];
};

FichiersLisibles fichiers;

PixelMapping correspondance[] = {
  {'.', ST77XX_BLACK},
  {'Y', ST77XX_YELLOW},
  {'R', ST77XX_RED},
  {'B', ST77XX_BLUE},
  {'W', ST77XX_WHITE},
  {'G', ST77XX_GREEN},
};

int fichierCourant=0; // numéro du fichier courant affiche

volatile uint8_t dernierAppui = BTN_NONE;  // indique la dernière touche appuyée

const int nbSymboles = sizeof(correspondance) / sizeof(PixelMapping);

unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

/**
 *  prototype des fonctions 
 */
uint16_t getCouleur(char c);
int calculerEchellePixel(const String& ligneTexte, int largeurEcran);
void lireFichierEtAfficher(const char* nomFichier);
void afficherLigneSmiley(const String& ligneTexte, int ligneIndex, int pixelSize, int offsetY);
void recenserFichiersLisibles();
void IRAM_ATTR boutonBleuAppuye();
void IRAM_ATTR boutonVertAppuye();
void IRAM_ATTR boutonRougeAppuye();

void setup() {
  Serial.begin(115200);
  Serial.println("setup");

  //initialisation de l'ecran en noir, orientation protrait
  tft.initR(INITR_BLACKTAB);  
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  //initialisation des boutons
  pinMode(PIN_BOUTON_BLEU, INPUT_PULLUP);
  pinMode(PIN_BOUTON_VERT, INPUT_PULLUP);
  pinMode(PIN_BOUTON_ROUGE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOUTON_BLEU), boutonBleuAppuye, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOUTON_VERT), boutonVertAppuye, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOUTON_ROUGE), boutonRougeAppuye, FALLING);

  //initialisation de la structure pour les fichiers utilisables
  if (!SPIFFS.begin(false)) {
    Serial.println("Échec du montage SPIFFS");
    while (1);
  } else {
    Serial.println("SPIFFS monté sans formatage");
  }
  Serial.println(">> VERSION DEBUG V1.1 <<");
  recenserFichiersLisibles();
  lireFichierEtAfficher(fichiers.noms[fichierCourant].c_str());
}

void IRAM_ATTR boutonBleuAppuye() {
  dernierAppui  = BTN_BLEU;  
}
void IRAM_ATTR boutonVertAppuye() {
  dernierAppui  = BTN_VERT;  
}
void IRAM_ATTR boutonRougeAppuye() {
  dernierAppui  = BTN_ROUGE;  
}
/**
 * @brief repmpli la structire fichiers de la liste des fichiers à afficher
 */
void recenserFichiersLisibles() {
  Serial.println("recenserFichiersLisibles");
  fichiers.nbFichiers = 0;

  File root = SPIFFS.open("/");
  File fichier = root.openNextFile();

  while (fichier && fichiers.nbFichiers < MAX_FICHIERS) {
    String nom = fichier.name();
    Serial.println(nom);
    if (nom.startsWith("lire_")) {
      fichiers.noms[fichiers.nbFichiers] = "/" +nom;
      fichiers.nbFichiers++;
    }
    fichier = root.openNextFile();
  }

  Serial.print("Nombre de fichiers lisibles : ");
  Serial.println(fichiers.nbFichiers);
}

/**
 * @brief retourne la valeur associee au symbole lu du fichier pour afficher le pixel sur l'ecran
 * @param c caracture lu
 * @return la valeur de la couleur associee dans PixelMapping
 */
uint16_t getCouleur(char c) {
  for (int i = 0; i < nbSymboles; i++) {
      if (correspondance[i].symbole == c) {
          return correspondance[i].couleur;
      }
  }
  return ST77XX_BLACK; // valeur par défaut si non trouvé
}

/**
 * @brief calcul la taille d'une unite a afficher en fonction du nombre de detail de la premiere ligne
 * @param ligneTexte la ligne du texte
 * @param largeurEcran la largeur de l'ecran en pixel
 * @return la taille que doit prendre chaque detail du smiley sur l'ecran
 */
int calculerEchellePixel(const String& ligneTexte, int largeurEcran) {
  int nbColonnes = ligneTexte.length();
  if (nbColonnes == 0) return 1;
  return largeurEcran / nbColonnes;
}

/**
 * @brief affiche le fichier lu a l'ecran
 * @param nomFichier le nom du fichier dans lequel le smiley est enregistre
 */
void lireFichierEtAfficher(const char* nomFichier) {
  File fichier = SPIFFS.open(nomFichier);
  if (!fichier || fichier.isDirectory()) {
    Serial.print("Erreur à l'ouverture du fichier : ");
    Serial.println(nomFichier);
    return;
  }

  // --- 1ère passe : lire première ligne + compter les lignes
  String ligneTemp = fichier.readStringUntil('\n');
  ligneTemp.trim();
  int pixelSize = calculerEchellePixel(ligneTemp, tft.width());

  int totalLines = 1;
  while (fichier.available()) {
    fichier.readStringUntil('\n');
    totalLines++;
  }

  // --- calcul de l'offset vertical
  int totalHeight = totalLines * pixelSize;
  int offsetY = (tft.height() - totalHeight) / 2;

  // --- remise au début pour la 2ème passe
  fichier.seek(0);

  // --- effacer et afficher toutes les lignes décalées
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

/**
 * @brief affiche ligne par ligne le smiley sur l'ecran
 * @param ligneTexte la ligne en cour de lecture
 * @param ligneIndex le numero de la ligne dans le fichier
 * @param offsetY créer un décalage en hauteur
 * @param pixelSize la taille que fera chaque element de la ligne sur l'ecran
 */
void afficherLigneSmiley(const String& ligneTexte, int ligneIndex, int pixelSize, int offsetY) {
  for (int col = 0; col < ligneTexte.length(); col++) {
    uint16_t couleur = getCouleur(ligneTexte[col]);
    for (int dx = 0; dx < pixelSize; dx++) {
      for (int dy = 0; dy < pixelSize; dy++) {
        int x = col * pixelSize + dx;
        int y = ligneIndex * pixelSize + dy + offsetY;
        tft.drawPixel(x, y, couleur);
      }
    }
  }
}

void loop() {

  // Si une touche a été enregistrée
  if (dernierAppui != BTN_NONE) {
    // Debounce simple
    if (millis() - lastDebounce > DEBOUNCE_MS) {
      lastDebounce = millis();

      switch (dernierAppui) {
        case BTN_BLEU:
          fichierCourant = (fichierCourant + 1) % fichiers.nbFichiers;
          Serial.println("Suivant → index = " + String(fichierCourant));
          break;

        case BTN_VERT:
          fichierCourant = (fichierCourant + fichiers.nbFichiers - 1) % fichiers.nbFichiers;
          Serial.println("Précédent → index = " + String(fichierCourant));
          break;

        case BTN_ROUGE:
          // action “aléatoire”
          fichierCourant = random(0, fichiers.nbFichiers);
          Serial.println("Aléatoire → index = " + String(fichierCourant));
          break;
      }

      // Affichage hors ISR
      lireFichierEtAfficher(fichiers.noms[fichierCourant].c_str());

      // Réinitialisation pour le prochain appui
      dernierAppui = BTN_NONE;
    }
  }
  delay(1000);
}
