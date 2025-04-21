#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <U8g2lib.h>
#include <math.h>

// Agregadas para OTA
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Configuración inicial
#define RESET_BUTTON_PIN 0         // GPIO0 (botón FLASH en NodeMCU)
#define RESET_HOLD_TIME 3000       // 3 segundos para activar el reset
#define LED_INTERNO 2              // GPIO2 (D4 en NodeMCU)
#define INTERVALO_PARPADEO 250     // 250ms

// Hostname
uint32_t chipId = ESP.getChipId();
String hostname = "ESP-" + String(chipId, HEX);

// Access Point SSID
String AP_SSID = hostname + " WiFi Config";

// Configuración en EEPROM
struct EspConfig {
  char title[17];
  char ssid[33];
  char passwd[33];
};

// Datos Ambientales
struct S_AhtData {
    float T;    // temperatura
    float RH;   // humedad relativa
    float Td;   // punto de rocío
    float VPD;  // déficit de presión de vapor
};

// Inicializaciones Globales
EspConfig espConfig;
ESP8266WebServer server(80);
DNSServer dnsServer;
Adafruit_AHTX0 aht;
S_AhtData AhtData;
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

bool inAPMode = false;
unsigned long buttonPressedTime = 0;
bool resetTriggered = false;
unsigned long tiempoAnteriorLED = 0;
bool estadoLED = false;

// Header & Style HTML en Flash Pointer String
// todo esto se almacena en flash
// y en SRAM sólo se crea un puntero.
String htmlHeader = FPSTR(R"=====(
<!DOCTYPE html>
<html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<meta http-equiv='Cache-Control' content='no-cache, no-store, must-revalidate'>
<meta http-equiv='Pragma' content='no-cache'>
<meta http-equiv='Expires' content='0'>
<title>)=====") + hostname + FPSTR(R"=====(</title>
<style>
body { font-family: 'Courier New', monospace; text-align: center; background-color: #282c34; color: #abb2bf; margin: 0; padding: 10px; }
h2 { color: #61dafb; margin-bottom: 15px; }
h3 { color: #61dafb; margin-bottom: 10px; }
h5 { text-align: left; margin-top: 10px; margin-bottom: 3px; }
div { text-align: center; }
form { max-width: 200px; margin: 0 auto; padding: 10px; border-radius: 10px;}
input { width: 90%; padding: 5px; margin: 5px 0; border: none; border-radius: 5px; }
button { background: #4CAF50; color: white; padding: 10px 15px; margin: 10px; border: none; border-radius: 5px; }
.data { margin: 10px auto; padding: 5px; background: #21252b; border-radius: 5px; width: 80%; max-width: 250px; }
</style></head>
)=====");


void setup() {
  Serial.begin(9600);

  pinMode(LED_INTERNO, OUTPUT);
  digitalWrite(LED_INTERNO, HIGH);  // Apaga LED al inicio (active low)

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);  // Configura el botón con pull-up interno
  
  // Lee configuración en eeprom
  EEPROM.begin(sizeof(EspConfig));
  EEPROM.get(0, espConfig);

  // Si no custom title, hostname default
  if (espConfig.title[0] == '\0') {
    snprintf(espConfig.title, sizeof(espConfig.title), "%s", hostname.c_str());
  }

  // Verificar si hay credenciales válidas
  if (strlen(espConfig.ssid) > 0) {
    Serial.println("\nEEPROM: Leído SSID " + String (espConfig.ssid));
    connectToWiFi();
  } else {
    Serial.println("\nEEPROM: SSID vacío. Iniciando APMode.");
    startAPMode();
  }

  // Inicializar AHT10
  if (!aht.begin()) {  // default 0x38
    Serial.println("Error inicializando AHT10.");
    while (1);
  }
  delay(250);  // Estabilización

  // Inicializar OLED
  if (!u8g2.begin()) {
    Serial.println("Error inicializando display OLED.");
    /* sólo debug, sigue adelante */
  }

  // Configurar rutas del servidor web
  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.on("/save", HTTP_POST, handleSave);

  // Estas urls son usadas por los distintos os para detectar hot-spots
  // Y redireccionando a la supuesta página de login, que en nuestro
  // caso es la configuración del dispositivo.
  server.on("/generate_204", []() { server.send(204); }); // Android
  server.on("/hotspot-detect.html", []() { handleRoot(); }); // Apple
  server.on("/ncsi.txt", []() { server.send(200, "text/plain", "OK"); }); // Windows

  // Capturar cualquier otra URL no manejada explícitamente
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  // Iniciar Servidor Web
  server.begin();

  // Habilita OTA
  iniciarOTA();
}


/*
/// LOOP PRINCIPAL ///
*/
void loop() {
  // Procesa llamados OTA
  ArduinoOTA.handle();

  // Procesa solicitudes HTTP
  server.handleClient();

  // Procesa solicitudes DNS
  dnsServer.processNextRequest();

  // Actualizar datos Ambientales
  AhtData = ObtenerDatosAht();

  // Actualiza Display
  updateDisplay();

  // Si en APMode, titila led
  checkAPMode();

  // Botón flash presionado borra EEPROM
  checkResetButton();

  delay(500);
}

/* Obtiene datos del sensor aht10
// Cálculo del Punto de Rocío y VPD (Métodos Magnus y Tetens)
// Constantes para agua líquida
// Para T < 0 °C, usar constantes para hielo: A=21.875 y B=265.5 */
S_AhtData ObtenerDatosAht() {
  // Obtiene datos del AHT10
  // Media móvil de 3 lecturas
  // Calcula Punto de Rocío (Td)
  // Calcula VPD (Vapor Pressure Deficit)
  sensors_event_t humidity, temp;
  // Lectura 1
  aht.getEvent(&humidity, &temp);
  float temp1 = temp.temperature;
  float hum1 = humidity.relative_humidity;
  delay(100);
  // Lectura 2
  aht.getEvent(&humidity, &temp);
  float temp2 = temp.temperature;
  float hum2 = humidity.relative_humidity;
  delay(100);
  // Lectura 3
  aht.getEvent(&humidity, &temp);
  float temp3 = temp.temperature;
  float hum3 = humidity.relative_humidity;
  // Promedio
  float T = (temp1 + temp2 + temp3) / 3;  // (°C)
  float RH = (hum1 + hum2 + hum3) / 3;    // (%)

  const float A = 17.27;
  const float B = 237.7;
  // Cálculo intermedio
  float termino = (A * T) / (B + T) + log(RH / 100.0);
  // Punto de rocío
  float Td = (B * termino) / (A - termino);  // (°C)
  // Presión de saturación (es)
  float es = 0.61078 * exp((A * T) / (T + B));  // (kPa)
  // Presión real de vapor (ea)
  float ea = es * (RH / 100.0);  // (kPa)
  // Calcular VPD (en kPa)
  float VPD = es - ea;  // (kPa)

  return {T, RH, Td, VPD};
}

void connectToWiFi() {
  Serial.print("\nConectando a WiFi: ");
  Serial.println(espConfig.ssid);

  WiFi.begin(espConfig.ssid, espConfig.passwd);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado!");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    inAPMode = false;
  } else {
    Serial.println("\nNo se pudo conectar. Iniciando AP-Mode.");
    startAPMode();
  }
}

void startAPMode() {
  WiFi.softAP(AP_SSID);
  inAPMode = true;
  // Inicia DNS Server
  // Reenvía todo a la IP del ESP
  dnsServer.start(53, "*", WiFi.softAPIP());  // Puerto 53, captura todo

  Serial.println("\nModo AP Cautivo Iniciado");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP del Portal: ");
  Serial.println(WiFi.softAPIP());
}

void checkAPMode() {
  // Si está en modo AP, parpadear el LED
  if (inAPMode) {
    if (millis() - tiempoAnteriorLED >= INTERVALO_PARPADEO) {
      tiempoAnteriorLED = millis();
      estadoLED = !estadoLED;                 // Alternar estado
      digitalWrite(LED_INTERNO, !estadoLED);  // Invertir lógica (active low)
    }
  } else {
    digitalWrite(LED_INTERNO, HIGH);  // Apagar LED si no está en modo AP
  }

  // Si estamos en modo cliente y perdemos conexión
  // Intenta reconectar a la WiFi
  if (!inAPMode && WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Intentando reconectar...");
    WiFi.disconnect();
    connectToWiFi();
  }
}

void checkResetButton() {
  // Verifica si el botón de reset está presionado (LOW porque es PULLUP)
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!resetTriggered) {
      buttonPressedTime = millis();  // Guarda el momento en que se presionó
      resetTriggered = true;
    }
    // Si se mantiene presionado por más de RESET_HOLD_TIME
    else if (millis() - buttonPressedTime >= RESET_HOLD_TIME) {
      // Borra la EEPROM
      Serial.println("Borrando EEPROM y reiniciando...");
      EEPROM.begin(sizeof(EspConfig));
      for (int i = 0; i < sizeof(EspConfig); i++) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();
      EEPROM.end();
      delay(500);     // Pequeña pausa antes del reinicio
      ESP.restart();  // Reinicia el ESP
    }
  } else {
    resetTriggered = false;  // Reinicia el estado si se suelta el botón
  }
}

void iniciarOTA() {
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(espConfig.passwd); // misma pass wifi
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("Iniciando actualización de " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nFinalizado");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progreso: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Autenticación fallida");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Falló el inicio");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Falló la conexión");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Falló la recepción");
    else if (error == OTA_END_ERROR) Serial.println("Falló el final");
  });
  ArduinoOTA.begin();
  Serial.println("OTA Iniciado");
}

void handleRoot() {
  if (inAPMode) {
    htmlConfig();
  } else {
    htmlDatos();
  }
}

void htmlDatos() {
    // Muestra Datos en Página Web
    String html = htmlHeader;
    html += "<body>";
    html += "<h2>" + String(espConfig.title) + "</h2>";
    html += "<div class='data'>Temperatura: " + String(AhtData.T, 1) + " &deg;C 🌡️</div>";
    html += "<div class='data'>Humedad: " + String(AhtData.RH, 1) + " % 💧</div>";
    html += "<div class='data'>Punto de Rocío: " + String(AhtData.Td, 1) + " &deg;C 🌨</div>";
    html += "<div class='data'>VPD: " + String(AhtData.VPD, 1) + " kPa 🌱</div>";
    html += "<script> setTimeout(function() { location.reload(); }, 10000); </script>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void htmlConfig() {
    // http://192.168.4.1
    String html = htmlHeader;
    html += "<body>";
    html += "<h3>Configuración</h3>";
    html += "<h2>" + hostname + "</h2>";
    html += "<form action='/save' method='post'>";
    html += "<h5>Título:</h5>";
    html += "<input type='text' name='title' placeholder='Título Personalizado' maxlength='16' value='" + String(espConfig.title) + "'><br>";
    html += "<h5>Nombre WiFi:</h5>";
    html += "<input type='text' name='ssid' placeholder='SSID de la red WiFi' maxlength='32' value='" + String(espConfig.ssid) + "' required><br>";
    html += "<h5>Password:</h5>";
    html += "<input type='text' name='passwd' placeholder='Contraseña' maxlength='32' value='" + String(espConfig.passwd) + "'><br>";
    html += "<button type='submit'>Guardar</button>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid")) {
    strncpy(espConfig.title, server.arg("title").c_str(), sizeof(espConfig.title));
    strncpy(espConfig.ssid, server.arg("ssid").c_str(), sizeof(espConfig.ssid));
    strncpy(espConfig.passwd, server.arg("passwd").c_str(), sizeof(espConfig.passwd));
    // Guardar en EEPROM
    EEPROM.put(0, espConfig);
    EEPROM.commit();
    String html = htmlHeader;
    html += "<body>";
    html += "<h2>Configuración guardada!</h2>";
    html += "<p>El dispositivo se reiniciará e intentará conectar a la red.</p>";
    html += "<br>";
    html += "<p>Puede cerrar esta página.</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Parámetros incorrectos");
  }
}

void handleJson() {
  String json = "{";
  json += "\"hostname\": \"" + hostname + "\",";
  json += "\"T\": " + String(AhtData.T, 1) + ",";
  json += "\"RH\": " + String(AhtData.RH, 1) + ",";
  json += "\"Td\": " + String(AhtData.Td, 1) + ",";
  json += "\"VPD\": " + String(AhtData.VPD, 1);
  json += "}";
  server.send(200, "application/json", json);
}

void updateDisplay() {
  u8g2.clearBuffer();

  // Línea 1: Título
  u8g2.setFont(u8g2_font_t0_15b_mf);  // 8x10px entran 16 chars
  u8g2_uint_t centrado = int((128 - strlen(espConfig.title) * 8) / 2);
  u8g2.setCursor(centrado, 10);
  u8g2.print(espConfig.title);

  // Línea 2: Humedad Y Temperatura
  u8g2.setFont(u8g2_font_9x15B_tf);  // 9x12px
  u8g2.setCursor(0, 28);
  u8g2.print(AhtData.T, 1);
  u8g2.print("\xB0 ");
  u8g2.print(AhtData.RH, 0);
  u8g2.print("% ");
  u8g2.print(AhtData.Td, 1);
  u8g2.print("\xB0");

  // Línea 3: VPD
  u8g2.setFont(u8g2_font_luIS14_tf);  //14px
  u8g2.setCursor(0, 47);
  u8g2.print("vpd: ");
  u8g2.print(AhtData.VPD, 1);
  u8g2.print(" kPa");

  // Línea 4: Dirección IP
  u8g2.setFont(u8g2_font_8x13O_mn);  // 8x10px entran 16 chars
  u8g2.setCursor(0, 64);
  u8g2.print(WiFi.localIP());

  u8g2.sendBuffer();
}
