#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// ================= CONFIGURATION =================
const char* ssid     = "samsam";
const char* password = "Sagri7@!";

const String firebaseHost = "smart-vote-edeee-default-rtdb.firebaseio.com";
// → PAS DE SECRET NÉCESSAIRE grâce au .json + règles Firebase bien configurées

// Pins MFRC522
#define SS_PIN  21
#define RST_PIN 22
MFRC522 rfid(SS_PIN, RST_PIN);

// Feedback
#define LED_OK     2   // LED intégrée ESP32
#define LED_ERROR  4
#define BUZZER     5

// Anti double-scan
unsigned long lastScanTime = 0;
const long scanDelay = 2500;  // 2,5 secondes
String lastUID = "";

// ================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_OK, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // Démarrage
  digitalWrite(LED_OK, HIGH);
  delay(300);
  digitalWrite(LED_OK, LOW);

  SPI.begin();
  rfid.PCD_Init();
  Serial.println(F("\n=== SMART VOTE 2025 - LECTEUR RFID ==="));
  Serial.println(F("Approchez votre carte de vote..."));

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_ERROR, LOW);
    Serial.println(F("\nWiFi connecté !"));
    Serial.println("IP: " + WiFi.localIP().toString());
  } else {
    Serial.println(F("\nÉCHEC WiFi - Redémarrage..."));
    errorFeedback();
    delay(3000);
    ESP.restart();
  }
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getUID();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  unsigned long now = millis();
  if (uid == lastUID && (now - lastScanTime) < scanDelay) {
    Serial.println("Double scan ignoré");
    delay(300);
    return;
  }

  lastUID = uid;
  lastScanTime = now;

  Serial.println("CARTE DÉTECTÉE → " + uid);
  checkCardEligibility(uid);
  sendToFirebase(uid);
  delay(100);
}

// =============== FONCTIONS ===============
String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void checkCardEligibility(String uid) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  String url = "https://" + firebaseHost + "/cards/" + uid + ".json";
  http.begin(url);
  http.setConnectTimeout(1000);     // timeout court
  http.setTimeout(2000);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("JSON error");
    } else if (doc.isNull() || !doc.containsKey("used")) {
      // Carte inconnue
      errorFeedback();
    } else if (doc["used"] == true) {
      // Déjà voté
      errorFeedback();
    } else {
      // ÉLIGIBLE
      successFeedback();
    }
  } else {
    Serial.printf("HTTP error %d\n", httpCode);
  }

  http.end();
}

void sendToFirebase(String uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi perdu !");
    errorFeedback();
    return;
  }

  //digitalWrite(LED_OK, HIGH);
  //beep(80);

  HTTPClient http;
  String url = "https://" + firebaseHost + "/scan.json";  // ← .json = pas besoin de secret

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(256);
  doc["uid"] = uid;
  doc["timestamp"] = millis() / 1000;  // en secondes

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.PUT(payload);

  if (httpCode == 200) {
    Serial.println("Scan envoyé avec succès !");
    //successFeedback();
  } else {
    Serial.printf("Erreur HTTP %d\n", httpCode);
    Serial.println(http.getString());
    //errorFeedback();
  }

  http.end();
  digitalWrite(LED_OK, LOW);
}

// Feedback
void successFeedback() {
  digitalWrite(LED_OK, HIGH);
  delay(100);
  delay(100);
  digitalWrite(LED_OK, LOW);
}

void errorFeedback() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_ERROR, HIGH);
    delay(100);
    digitalWrite(LED_ERROR, LOW);
    delay(100);
  }
}
