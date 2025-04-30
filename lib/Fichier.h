#ifndef FICHIER_H
#define FICHIER_H
#include <Arduino.h>

#define MAX_FICHIERS 20 // nomnre de fichier limite lisibles
#define MDP_WIFI "monMotDePass"
/**
 * @brief Structure qui va s'initialiser avec le nombre de fichiers qui contiennent des informations a afficher sur l'ecran et un tableau avec ces noms, le nombre est limite a 10 fichiers
 * les fichiers lisibles commencent par lire_
 */
struct FichiersLisibles {
    int nbFichiers = 0;
    String noms[MAX_FICHIERS];
  };
  
  extern FichiersLisibles fichiers;
  extern int fichierCourant; // numéro du fichier courant affiche
  extern String espName;                   // ex "ESP789B"
  /**
 * @brief Parcourt SPIFFS et remplit la variable globale `fichiers`
 *        avec tous les fichiers dont le nom commence par "lire_".
 */
void recenserFichiersLisibles();

void lireFichierEtAfficher(const char* nomFichier);

#endif // FICHIER_H