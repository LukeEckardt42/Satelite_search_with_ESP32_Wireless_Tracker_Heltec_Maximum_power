#include "Arduino.h"
#include "HT_TinyGPS++.h" // Die Heltec-Spezial-Bibliothek

TinyGPSPlus GPS; 

// --- Konfiguration (aus deinem Beispiel-Code) ---
#define VGNSS_CTRL 3 // Pin 3 (GPS_VCC_EN)
#define GPS_BAUD 115200
#define GPS_RX_PIN 34
#define GPS_TX_PIN 33

void setup(){
  Serial.begin(115200);
  Serial.println("Starte GPS-Test mit Heltec-Bibliothek (mit NMEA-Ausgabe)...");

  // GPS einschalten (Pin 3)
  pinMode(VGNSS_CTRL,OUTPUT);
  digitalWrite(VGNSS_CTRL,HIGH);
  delay(100);

  // GPS-Serial-Port starten
  // WICHTIG: Die Pins sind (Baud, Config, TX-Pin, RX-Pin)
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN); 
  Serial.println("GPS mit Strom versorgt. Warte auf Daten...");
}

void loop(){
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    
    // --- HIER DIE NEUE ZEILE ---
    // Gibt jedes Zeichen vom GPS direkt im Monitor aus
    Serial.write(c); 
    // --- ENDE NEUE ZEILE ---
    
    if (GPS.encode(c)) {
      // Wenn ein vollständiger Satz verarbeitet wurde:
      Serial.println(); // Zeilenumbruch nach dem NMEA-Satz
      
      if (GPS.location.isUpdated()) {
        Serial.print("==> Position: ");
        Serial.print(GPS.location.lat(), 6);
        Serial.print(", ");
        Serial.println(GPS.location.lng(), 6);
      }
      
      if (GPS.satellites.isUpdated()) {
        Serial.print("==> Satelliten: ");
        Serial.println(GPS.satellites.value());
      }
      Serial.println("--------------------"); // Trennlinie
    }
  }
}
