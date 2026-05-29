#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include "INA236.h"


// ==========================================
// CONFIGURATION I2C & INA236
// ==========================================
int I2C_SDA = 21;
int I2C_SCL = 22;


INA236 INA(0x42);


float INA_TensionBus = 0.0;
float INA_TensionShunt = 0.0;
float INA_Courant = 0.0;
float INA_Puissance = 0.0;


// ==========================================
// CONFIGURATION WI-FI & WEB SERVER
// ==========================================
const char *ssid = "ESP32_Dashboard";
const char *password = "superstar";


AsyncWebServer server(80);


// ==========================================
// CONFIGURATION DES BROCHES (Matériel)
// ==========================================
int PD1 = 34;
int PD2 = 39;
int PD3 = 36;
int PD4 = 35;
int D1_HAUT = 16;
int D1_BAS = 17;
int D2_HAUT = 18;
int D2_BAS = 19;
int D3_HAUT = 26;
int D3_BAS = 25;
int D4_HAUT = 33;
int D4_BAS = 32;


float val_PD1, val_PD2, val_PD3, val_PD4;
float tension_a_vide = 36.0; // AJUSTÉ : Doit être supérieure à batterie_plein (33.6V)
float batterie_plein = 33.6;
float batterie_demi_charge = 28.8;
float batterie_decharge = 25.0;
float etat_critique_batterie = 24.0;
float seuil_masse = 5.0;


// ==== SEUILS DE SÉCURITÉ ET TIMERS ====
const float SEUIL_COURANT_MIN = 0.05;
const unsigned long TEMPS_STABILISATION = 2000;
const unsigned long DELAI_AMORCAGE = 500;
const unsigned long INTERVAL_CAPTEURS = 50; // Lecture des capteurs toutes les 50 ms


unsigned long debutStabilisation = 0;
unsigned long debutRoutage = 0;
unsigned long dernierReleveCapteurs = 0; // Timer non-bloquant pour les capteurs


String etat_PD1 = "Attente...";
String etat_PD2 = "Attente...";
String etat_PD3 = "Attente...";
String etat_PD4 = "Attente...";


// ==========================================
// LES 6 ÉTATS DE LA MACHINE (Ajout de CHARGE_TERMINEE)
// ==========================================
enum EtatStation
{
  ETAT_INITIAL,
  ETAT_TEMPORISATION,
  ETAT_CHARGE,
  ETAT_TENSION_BASSE,
  ETAT_SURTENSION,
  ETAT_CHARGE_TERMINEE // Évite l'effet Yoyo en fin de charge
};


EtatStation etatActuel = ETAT_INITIAL;
int padActif = 0;


// Prototypes
void couperTousLesPads();
bool detecterChien();
int identifierPadActif();
float lireTensionPad(int pad);
bool verifierChienLeve();


// ==========================================
// SETUP
// ==========================================
void setup()
{
  Serial.begin(115200);


  pinMode(D1_HAUT, OUTPUT);
  pinMode(D1_BAS, OUTPUT);
  pinMode(D2_HAUT, OUTPUT);
  pinMode(D2_BAS, OUTPUT);
  pinMode(D3_HAUT, OUTPUT);
  pinMode(D3_BAS, OUTPUT);
  pinMode(D4_HAUT, OUTPUT);
  pinMode(D4_BAS, OUTPUT);


  couperTousLesPads();


  Serial.println("\n--- Initialisation I2C ---");
  Wire.begin(I2C_SDA, I2C_SCL);


  if (INA.begin())
  {
    Serial.println("INA236 détecté (0x42).");
    INA.setMaxCurrentShunt(9.0, 0.005);
  }


  WiFi.softAP(ssid, password);
  LittleFS.begin(true);


  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html"); });


  server.on("/donnees", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String json = "";
    json.reserve(512); // OPTIMISATION : Évite la fragmentation de la mémoire RAM
    json += "{";
    json += "\"pd1\":" + String(val_PD1) + ",";
    json += "\"pd2\":" + String(val_PD2) + ",";
    json += "\"pd3\":" + String(val_PD3) + ",";
    json += "\"pd4\":" + String(val_PD4) + ",";
    json += "\"etat_pd1\":\"" + etat_PD1 + "\",";
    json += "\"etat_pd2\":\"" + etat_PD2 + "\",";
    json += "\"etat_pd3\":\"" + etat_PD3 + "\",";
    json += "\"etat_pd4\":\"" + etat_PD4 + "\",";
    json += "\"ina_vbus\":" + String(INA_TensionBus, 3) + ",";
    json += "\"ina_vshunt\":" + String(INA_TensionShunt, 3) + ",";
    json += "\"ina_current\":" + String(INA_Courant, 3) + ",";
    json += "\"ina_power\":" + String(INA_Puissance, 3);
    json += "}";
    request->send(200, "application/json", json); });


  server.begin();
}


// ==========================================
// LOOP : EXÉCUTION DE LA MACHINE À ÉTATS
// ==========================================
void loop()
{
  // --- 1. LECTURE CADENCÉE DES CAPTEURS (Toutes les 50ms) ---
  if (millis() - dernierReleveCapteurs >= INTERVAL_CAPTEURS)
  {
    dernierReleveCapteurs = millis();


    if (INA.isConnected())
    {
      INA_TensionBus = INA.getBusVoltage();
      INA_TensionShunt = INA.getShuntVoltage() * 1000.0;
      INA_Courant = INA.getCurrent();
      INA_Puissance = INA.getPower();
    }


    val_PD1 = (analogRead(PD1) * 36.0) / 4095.0;
    val_PD2 = (analogRead(PD2) * 36.0) / 4095.0;
    val_PD3 = (analogRead(PD3) * 36.0) / 4095.0;
    val_PD4 = (analogRead(PD4) * 36.0) / 4095.0;
  }


  // Priorité absolue : Surtension
  if (val_PD1 > tension_a_vide || val_PD2 > tension_a_vide || val_PD3 > tension_a_vide || val_PD4 > tension_a_vide)
  {
    etatActuel = ETAT_SURTENSION;
  }


  // --- 2. LOGIQUE DES ÉTATS ---
  switch (etatActuel)
  {
  // ==========================================
  case ETAT_INITIAL:
    couperTousLesPads();
    etat_PD1 = "À vide / Attente";
    etat_PD2 = "À vide / Attente";
    etat_PD3 = "À vide / Attente";
    etat_PD4 = "À vide / Attente";
    padActif = 0;


    if (detecterChien())
    {
      debutStabilisation = millis();
      etatActuel = ETAT_TEMPORISATION;
    }
    break;


  // ==========================================
  case ETAT_TEMPORISATION:
    if (!detecterChien())
    {
      etatActuel = ETAT_INITIAL;
      break;
    }


    padActif = identifierPadActif();


    etat_PD1 = (padActif == 1) ? "Analyse: Pôle Positif (+)" : "Analyse: Masse (-)";
    etat_PD2 = (padActif == 2) ? "Analyse: Pôle Positif (+)" : "Analyse: Masse (-)";
    etat_PD3 = (padActif == 3) ? "Analyse: Pôle Positif (+)" : "Analyse: Masse (-)";
    etat_PD4 = (padActif == 4) ? "Analyse: Pôle Positif (+)" : "Analyse: Masse (-)";


    if (millis() - debutStabilisation >= TEMPS_STABILISATION)
    {
      if (padActif > 0)
      {
        debutRoutage = millis();
        float tensionBatterie = lireTensionPad(padActif);


        if (tensionBatterie < batterie_decharge)
        {
          etatActuel = ETAT_TENSION_BASSE;
        }
        else
        {
          etatActuel = ETAT_CHARGE;
        }
      }
      else
      {
        etatActuel = ETAT_INITIAL;
      }
    }
    break;


  // ==========================================
  case ETAT_CHARGE:
    // Si le courant chute après le délai d'amorçage
    if (millis() - debutRoutage > DELAI_AMORCAGE && verifierChienLeve())
    {
      // CORRECTIF : Au lieu de retourner directement à INITIAL, on passe à CHARGE_TERMINEE
      // pour vérifier si le robot est encore sur la plateforme ou s'il est parti.
      etatActuel = ETAT_CHARGE_TERMINEE;
    }
    else
    {
      // Routage physique explicite
      if (padActif == 1)
      {
        etat_PD1 = "Présence Pôle Positif (+)"; etat_PD2 = "Présence Masse (-)"; etat_PD3 = "Présence Masse (-)"; etat_PD4 = "Présence Masse (-)";
        digitalWrite(D1_HAUT, HIGH); digitalWrite(D1_BAS, LOW);
        digitalWrite(D2_HAUT, LOW);  digitalWrite(D2_BAS, HIGH);
        digitalWrite(D3_HAUT, LOW);  digitalWrite(D3_BAS, HIGH);
        digitalWrite(D4_HAUT, LOW);  digitalWrite(D4_BAS, HIGH);
      }
      else if (padActif == 2)
      {
        etat_PD2 = "Présence Pôle Positif (+)"; etat_PD1 = "Présence Masse (-)"; etat_PD3 = "Présence Masse (-)"; etat_PD4 = "Présence Masse (-)";
        digitalWrite(D1_HAUT, LOW);  digitalWrite(D1_BAS, HIGH);
        digitalWrite(D2_HAUT, HIGH); digitalWrite(D2_BAS, LOW);
        digitalWrite(D3_HAUT, LOW);  digitalWrite(D3_BAS, HIGH);
        digitalWrite(D4_HAUT, LOW);  digitalWrite(D4_BAS, HIGH);
      }
      else if (padActif == 3)
      {
        etat_PD3 = "Présence Pôle Positif (+)"; etat_PD1 = "Présence Masse (-)"; etat_PD2 = "Présence Masse (-)"; etat_PD4 = "Présence Masse (-)";
        digitalWrite(D1_HAUT, LOW);  digitalWrite(D1_BAS, HIGH);
        digitalWrite(D2_HAUT, LOW);  digitalWrite(D2_BAS, HIGH);
        digitalWrite(D3_HAUT, HIGH); digitalWrite(D3_BAS, LOW);
        digitalWrite(D4_HAUT, LOW);  digitalWrite(D4_BAS, HIGH);
      }
      else if (padActif == 4)
      {
        etat_PD4 = "Présence Pôle Positif (+)"; etat_PD1 = "Présence Masse (-)"; etat_PD2 = "Présence Masse (-)"; etat_PD3 = "Présence Masse (-)";
        digitalWrite(D1_HAUT, LOW);  digitalWrite(D1_BAS, HIGH);
        digitalWrite(D2_HAUT, LOW);  digitalWrite(D2_BAS, HIGH);
        digitalWrite(D3_HAUT, LOW);  digitalWrite(D3_BAS, HIGH);
        digitalWrite(D4_HAUT, HIGH); digitalWrite(D4_BAS, LOW);
      }
    }
    break;


  // ==========================================
  case ETAT_CHARGE_TERMINEE:
    // CORRECTIF YOYO : On coupe le jus du chargeur.
    couperTousLesPads();
    etat_PD1 = (padActif == 1) ? "Charge complète ✔" : "Déconnecté (-)";
    etat_PD2 = (padActif == 2) ? "Charge complète ✔" : "Déconnecté (-)";
    etat_PD3 = (padActif == 3) ? "Charge complète ✔" : "Déconnecté (-)";
    etat_PD4 = (padActif == 4) ? "Charge complète ✔" : "Déconnecté (-)";


    // Tant que la batterie pleine est posée sur les pads (relais ouverts), sa tension reste détectable.
    // Dès que le robot s'en va, la tension tombe à 0V -> Retour à l'état initial.
    if (!detecterChien())
    {
      etatActuel = ETAT_INITIAL;
    }
    break;


  // ==========================================
  case ETAT_TENSION_BASSE:
    couperTousLesPads();
    etat_PD1 = (val_PD1 < etat_critique_batterie) ? "Batterie Morte" : "Verrouillé / Tension très basse";
    etat_PD2 = (val_PD2 < etat_critique_batterie) ? "Batterie Morte" : "Verrouillé / Tension très basse";
    etat_PD3 = (val_PD3 < etat_critique_batterie) ? "Batterie Morte" : "Verrouillé / Tension très basse";
    etat_PD4 = (val_PD4 < etat_critique_batterie) ? "Batterie Morte" : "Verrouillé / Tension très basse";


    // CORRECTIF : Si le robot s'en va, la tension chute à 0V. Il faut pouvoir débloquer la station !
    if (!detecterChien())
    {
      etatActuel = ETAT_INITIAL;
    }
    // Si la tension revient à la normale (ex: batterie récupérée ou changée manuellement)
    else if (val_PD1 >= batterie_decharge || val_PD2 >= batterie_decharge || val_PD3 >= batterie_decharge || val_PD4 >= batterie_decharge)
    {
      etatActuel = ETAT_INITIAL;
    }
    break;


  // ==========================================
  case ETAT_SURTENSION:
    couperTousLesPads();
    etat_PD1 = (val_PD1 > tension_a_vide) ? "Erreur Surtension" : "Verrouillé / Sécurité";
    etat_PD2 = (val_PD2 > tension_a_vide) ? "Erreur Surtension" : "Verrouillé / Sécurité";
    etat_PD3 = (val_PD3 > tension_a_vide) ? "Erreur Surtension" : "Verrouillé / Sécurité";
    etat_PD4 = (val_PD4 > tension_a_vide) ? "Erreur Surtension" : "Verrouillé / Sécurité";


    if (val_PD1 <= tension_a_vide && val_PD2 <= tension_a_vide && val_PD3 <= tension_a_vide && val_PD4 <= tension_a_vide)
    {
      etatActuel = ETAT_INITIAL;
    }
    break;
  }
}


// ==========================================
// FONCTIONS DE LECTURE ET SÉCURITÉ
// ==========================================


bool detecterChien()
{
  return (val_PD1 > seuil_masse && val_PD1 < tension_a_vide) ||
         (val_PD2 > seuil_masse && val_PD2 < tension_a_vide) ||
         (val_PD3 > seuil_masse && val_PD3 < tension_a_vide) ||
         (val_PD4 > seuil_masse && val_PD4 < tension_a_vide);
}


int identifierPadActif()
{
  if (val_PD1 < tension_a_vide && val_PD1 > seuil_masse) return 1;
  if (val_PD2 < tension_a_vide && val_PD2 > seuil_masse) return 2;
  if (val_PD3 < tension_a_vide && val_PD3 > seuil_masse) return 3;
  if (val_PD4 < tension_a_vide && val_PD4 > seuil_masse) return 4;
  return 0;
}


float lireTensionPad(int pad)
{
  if (pad == 1) return val_PD1;
  if (pad == 2) return val_PD2;
  if (pad == 3) return val_PD3;
  if (pad == 4) return val_PD4;
  return 0.0;
}


bool verifierChienLeve()
{
  return (INA_Courant < SEUIL_COURANT_MIN);
}


void couperTousLesPads()
{
  digitalWrite(D1_HAUT, LOW); digitalWrite(D1_BAS, LOW);
  digitalWrite(D2_HAUT, LOW); digitalWrite(D2_BAS, LOW);
  digitalWrite(D3_HAUT, LOW); digitalWrite(D3_BAS, LOW);
  digitalWrite(D4_HAUT, LOW); digitalWrite(D4_BAS, LOW);
}
