//  BLOOMY - ESP32 + Firebase HTTP REST API

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// WIFI 
#define WIFI_SSID "OPPO A92"
#define WIFI_PASSWORD "asma462005"

//  FIREBASE 
#define FIREBASE_PROJECT_ID "bloomy-serre-connectee"
#define API_KEY "AIzaSyAZbvfoH5t9RQVX7RAqrypXDfrcWcPHKoo"
#define DEVICE_ID "esp32_serre_01"

// URLs Firestore REST API
#define SENSORS_URL "https://firestore.googleapis.com/v1/projects/" FIREBASE_PROJECT_ID "/databases/(default)/documents/sensors/" DEVICE_ID
#define ACTUATORS_URL "https://firestore.googleapis.com/v1/projects/" FIREBASE_PROJECT_ID "/databases/(default)/documents/actuators/" DEVICE_ID

// PINS 
#define DHTPIN 4
#define DHTTYPE DHT11
#define RELAY_PUMP 23
#define RELAY_FAN 19
#define LED_PIN 5
#define SOIL_PIN 34
#define SERVO_PIN 18

//  LIMITES 
#define TEMP_LIMIT 30.0
#define SOIL_LIMIT 3000

//  OBJECTS 
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

// VARIABLES 
float temperature = 0;
float humidity = 0;
int soilMoisture = 0;
int lightLevel = 400;

bool pumpActive = false;
bool fanActive = false;
bool ledActive = false;
bool doorOpen = false;

// TIMERS 
unsigned long lastSensorRead = 0;
unsigned long lastFirebaseSend = 0;
unsigned long lastFirebaseRead = 0;
unsigned long lastLcdUpdate = 0;

bool wifiConnected = false;

//  FONCTIONS 

// Connexion WiFi
void connectWiFi() {
  Serial.print("Connexion WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n WiFi connecté!");
    Serial.print("   IP: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
  } else {
    Serial.println("\n WiFi ÉCHEC!");
    wifiConnected = false;
  }
}

// Activer/désactiver un relais (actif BAS)
void controlRelay(int pin, bool activate, const char* name) {
  digitalWrite(pin, activate ? LOW : HIGH);
  Serial.printf("   %s: %s\n", name, activate ? "ON" : "OFF");
}

// Ouvrir la porte (servo)
void openDoor() {
  Serial.println("  Ouverture porte ");
  doorServo.attach(SERVO_PIN);
  doorServo.write(90);
  delay(1500);
  doorServo.write(0);
  delay(500);
  doorServo.detach();
  doorOpen = false;
}

// Lire les capteurs
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int s = analogRead(SOIL_PIN);
  
  if (!isnan(t) && t >= -40 && t <= 80) temperature = t;
  if (!isnan(h) && h >= 0 && h <= 100) humidity = h;
  if (s >= 0 && s <= 4095) soilMoisture = s;
  
  // Contrôle AUTO local
  fanActive = (temperature > TEMP_LIMIT);
  pumpActive = (soilMoisture > SOIL_LIMIT);
  
  controlRelay(RELAY_FAN, fanActive, "Ventilateur");
  controlRelay(RELAY_PUMP, pumpActive, "Pompe");
  digitalWrite(LED_PIN, ledActive ? HIGH : LOW);
  
  Serial.printf(" Capteurs → T: %.1f°C | H: %.1f%% | Sol: %d\n", 
                temperature, humidity, soilMoisture);
}

//  ENVOYER données à Firebase via HTTP PATCH
void sendToFirebase() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  String url = String(SENSORS_URL) + "?key=" + String(API_KEY);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> doc;
  JsonObject fields = doc.createNestedObject("fields");
  
  fields["temperature"]["doubleValue"] = temperature;
  fields["humidity"]["doubleValue"] = humidity;
  fields["soilMoisture"]["integerValue"] = soilMoisture;
  fields["light"]["integerValue"] = lightLevel;
  fields["pump"]["booleanValue"] = pumpActive;
  fields["fan"]["booleanValue"] = fanActive;
  fields["led"]["booleanValue"] = ledActive;
  fields["doorOpen"]["booleanValue"] = doorOpen;
  fields["timestamp"]["serverValue"] = "REQUEST_TIME";
  
  String jsonBuffer;
  serializeJson(doc, jsonBuffer);
  
  Serial.print(" Envoi Firebase... ");
  int httpCode = http.PATCH(jsonBuffer);
  
  if (httpCode > 0) {
    Serial.printf(" HTTP %d\n", httpCode);
  } else {
    Serial.printf(" Erreur HTTP: %d\n", httpCode);
  }
  
  http.end();
}

//  LIRE commandes depuis Firebase via HTTP GET
void readFromFirebase() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  String url = String(ACTUATORS_URL) + "?key=" + String(API_KEY);
  
  http.begin(url);
  Serial.print(" Lecture Firebase ");
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      JsonObject fields = doc["fields"];
      
      if (fields.containsKey("pump")) {
        bool newPump = fields["pump"]["booleanValue"];
        if (newPump != pumpActive) {
          pumpActive = newPump;
          controlRelay(RELAY_PUMP, pumpActive, "Pompe [Firebase]");
        }
      }
      
      if (fields.containsKey("fan")) {
        bool newFan = fields["fan"]["booleanValue"];
        if (newFan != fanActive) {
          fanActive = newFan;
          controlRelay(RELAY_FAN, fanActive, "Ventilateur [Firebase]");
        }
      }
      
      if (fields.containsKey("led")) {
        bool newLed = fields["led"]["booleanValue"];
        if (newLed != ledActive) {
          ledActive = newLed;
          digitalWrite(LED_PIN, ledActive ? HIGH : LOW);
        }
      }
      
      if (fields.containsKey("doorOpen")) {
        bool newDoor = fields["doorOpen"]["booleanValue"];
        if (newDoor && !doorOpen) {
          openDoor();
          doorOpen = true;
        }
      }
      
      Serial.println(" Commandes lues");
    } else {
      Serial.printf(" JSON Error: %s\n", error.c_str());
    }
  } else if (httpCode == 404) {
    Serial.println(" Document non trouvé");
  } else {
    Serial.printf(" HTTP Error: %d\n", httpCode);
  }
  
  http.end();
}

//  METTRE À JOUR LCD - affichage:  "Welcome to Bloomy" + Capteurs
void updateLCD() {
  //  LIGNE 1 : TOUJOURS "Welcome to Bloomy" 
 
  lcd.setCursor(0, 0);
  lcd.print("Welcome to Bloomy");  
  
  //  LIGNE 2 : CAPTEURS (T, H, Sol) 
  // Format: "T:24.5 H:58% S:1850" 
  lcd.setCursor(0, 1);
  
  // Température 
  lcd.print("T:");
  if (isnan(temperature)) {
    lcd.print("--.-");
  } else {
    lcd.print(temperature, 1);  
  }
  
  // Humidité Air 
  lcd.print(" H:");
  if (isnan(humidity)) {
    lcd.print("--");
  } else {
    lcd.print((int)humidity);
    lcd.print("%");
  }
  
  // Humidité Sol (6 chars: " S:1850")
  lcd.print(" S:");
  if (soilMoisture == 0) {
    lcd.print("---");
  } else {
    // Afficher seulement 3-4 chiffres pour tenir dans 16 chars
    if (soilMoisture > 9999) {
      lcd.print(soilMoisture / 100);
      lcd.print("k");
    } else {
      lcd.print(soilMoisture);
    }
  }
  
  // Debug Serial
  Serial.printf("🖥️ LCD: T=%.1f°C H=%.1f%% Sol=%d\n", temperature, humidity, soilMoisture);
}

// SETUP
void setup() {
  Serial.begin(115200);
  Serial.println("\n BLOOMY - ESP32 (LCD Welcome)");
 
  
  // WiFi
  connectWiFi();
  
  // Capteurs
  dht.begin();
  
  // LCD
  lcd.init();
  lcd.backlight();
  
  // Message de bienvenue initial (2 secondes)
  lcd.setCursor(0, 0);
  lcd.print("Welcome to");
  lcd.setCursor(0, 1);
  lcd.print("Bloomy!");
  delay(2000);
  
  // Pins
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  digitalWrite(RELAY_PUMP, HIGH);  // OFF
  digitalWrite(RELAY_FAN, HIGH);   // OFF
  digitalWrite(LED_PIN, LOW);      // OFF
  
  // Servo
  doorServo.attach(SERVO_PIN);
  doorServo.write(0);
  delay(500);
  doorServo.detach();
  
  // Status initial
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Welcome 2 Bloomy");  
  lcd.setCursor(0, 1);
  lcd.print("Init");
  delay(1000);
  
  Serial.println("Setup terminé");
  Serial.println("\n");
}

//  LOOP 
void loop() {
  unsigned long now = millis();
  
  //  Reconnecter WiFi si perdu
  if (wifiConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println(" WiFi perdu , Reconnexion..");
    wifiConnected = false;
    delay(2000);
    connectWiFi();
  }
  
  //  Lire capteurs toutes les 3s
  if (now - lastSensorRead >= 3000) {
    lastSensorRead = now;
    readSensors();
  }
  
  //  Envoyer à Firebase toutes les 5s
  if (now - lastFirebaseSend >= 5000 && wifiConnected) {
    lastFirebaseSend = now;
    sendToFirebase();
  }
  
  //  Lire commandes Firebase toutes les 2s
  if (now - lastFirebaseRead >= 2000 && wifiConnected) {
    lastFirebaseRead = now;
    readFromFirebase();
  }
  
  //  Mettre à jour LCD toutes les 2s
  if (now - lastLcdUpdate >= 2000) {
    lastLcdUpdate = now;
    updateLCD();
  }
  
  delay(100);
}
