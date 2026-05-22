/*
 * The MIT License
 *
 * Copyright 2024 Alvaro Salazar <alvaro@denkitronik.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <libiot.h>
#include <SHTSensor.h>
#include <libota.h>
#include <libstorage.h>

// Versión del firmware (debe coincidir con main.cpp)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.1.1"
#endif

//#define PRINT
#ifdef PRINT
#define PRINTD(x, y) Serial.println(x, y)
#define PRINTLN(x) Serial.println(x)
#define PRINT(x) Serial.print(x)
#else
#define PRINTD(x, y)
#define PRINTLN(x)
#define PRINT(x)
#endif

SHTSensor sht;     //Sensor SHT21
String alert = ""; //Mensaje de alerta

// Definiciones de las variables globales declaradas en libiot.h
WiFiClientSecure espClient;
PubSubClient client(espClient);

time_t now = 0;
long long int measureTime = 0;
long long int alertTime = 0;

const char* client_id = "";  // ID del cliente MQTT (se configurará en setupIoT())


/**
 * Consulta y guarda el tiempo actual con servidores SNTP.
 */
time_t setTime() {
  //Sincroniza la hora del dispositivo con el servidor SNTP (Simple Network Time Protocol)
  Serial.print("Ajustando el tiempo usando SNTP");
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov"); //Configura la zona horaria y los servidores SNTP
  now = time(nullptr);              //Obtiene la hora actual
  int attempts = 0;
  while (now < 1700000000 && attempts < 40) {        //Espera a que el tiempo sea mayor a 1700000000 (1 de enero de 2024)
    delay(500);                     //Espera 500ms antes de volver a intentar obtener la hora
    Serial.print(".");
    now = time(nullptr);            //Obtiene la hora actual
    attempts++;
  }
  if (now >= 1700000000) {
    Serial.println(" hecho!");
    struct tm timeinfo;               //Estructura que almacena la información de la hora
    gmtime_r(&now, &timeinfo);        //Obtiene la hora actual
    Serial.print("Tiempo actual: ");  //Una vez obtiene la hora, imprime en el monitor el tiempo actual
    Serial.print(asctime(&timeinfo));
  } else {
    Serial.println(" fallido (timeout)!");
  }
  return now;
}

// Variable para debugging periódico
static unsigned long lastMQTTDebug = 0;
static const unsigned long MQTT_DEBUG_INTERVAL = 5000; // 30 segundos

/**
 * Conecta el dispositivo con el bróker MQTT usando
 * las credenciales establecidas.
 * Si ocurre un error lo imprime en la consola.
 */
void checkMQTT() {
  if (!client.connected()) {
    reconnect();
  }
  // Procesa mensajes MQTT entrantes (esto es crítico para recibir mensajes)
  // IMPORTANTE: client.loop() debe llamarse frecuentemente para recibir mensajes
  bool loopResult = client.loop();
  if (!loopResult && client.connected()) {
    // Si loop() retorna false pero estamos conectados, podría haber un problema
    Serial.println("⚠ client.loop() retornó false (podría indicar problema de conexión)");
  }
  
  // Debug periódico cada 30 segundos
  unsigned long now = millis();
  if (now - lastMQTTDebug > MQTT_DEBUG_INTERVAL) {
    lastMQTTDebug = now;
    Serial.println("=== Healthcheck MQTT (cada 5s) ===");
    Serial.print("Conectado: ");
    Serial.println(client.connected() ? "✅UP" : "❌DOWN");
  }
}

/**
 * Adquiere la dirección MAC del dispositivo y la retorna en formato de cadena.
 */
String getMacAddress() {
  uint8_t mac[6];
  char macStr[18];
  WiFi.macAddress(mac);
  snprintf(macStr, sizeof(macStr), "ESP32-%02X%02X%02X%02X%02X%02X", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}


/**
 * Función que se ejecuta cuando se establece conexión con el servidor MQTT
 */
void reconnect() {
  while (!client.connected()) { //Mientras no esté conectado al servidor MQTT
    Serial.println("=== Intentando conectar a MQTT ===");
    Serial.print("Servidor: ");
    Serial.println(mqtt_server);
    Serial.print("Puerto: ");
    Serial.println(mqtt_port);
    Serial.print("Usuario: ");
    Serial.println(mqtt_user);
    Serial.print("Client ID: ");
    Serial.println(client_id);
    Serial.print("Conectando...");
    if (client.connect(client_id, mqtt_user, mqtt_password)) { //Intenta conectarse al servidor MQTT
      Serial.println(" ✓ CONECTADO");
      
      // CRÍTICO: Reconfigurar el callback después de reconectar
      client.setCallback(receivedCallback);
      Serial.println("✓ Callback reconfigurado después de reconexión");
      
      // Imprimir versión del firmware al conectar (usar versión guardada)
      String firmwareVersion = getFirmwareVersion();
      Serial.print("Firmware: ");
      Serial.println(firmwareVersion);
      
      Serial.println("=== Suscripciones MQTT ===");
      // Se suscribe al tópico de suscripción con QoS 1
      bool subResult = client.subscribe(MQTT_TOPIC_SUB, 1);
      if (subResult) {
        Serial.println("✓ Suscrito exitosamente a " + String(MQTT_TOPIC_SUB));
      } else {
        Serial.println("✗ Error al suscribirse a " + String(MQTT_TOPIC_SUB));
      }
      
      // Procesar mensajes para confirmar suscripciones
      client.loop();
      delay(100); // Dar tiempo para procesar
      
      setupOTA(client); //Configura la funcionalidad OTA
      
      // Procesar mensajes nuevamente después de suscribirse a OTA
      client.loop();
      delay(100);
      
      Serial.println("==========================");
      Serial.println("Listo para recibir mensajes MQTT");
    } else {
      Serial.println(" ✗ FALLÓ");
      int state = client.state();
      
      // Diagnóstico detallado del error
      Serial.println("\n=== DIAGNÓSTICO DEL ERROR ===");
      Serial.print("Código de error: ");
      Serial.println(state);
      
      // Mensajes descriptivos por código de error
      switch(state) {
        case -4:
          Serial.println("Error: MQTT_DISCONNECTED - Desconectado");
          break;
        case -3:
          Serial.println("Error: MQTT_CONNECT_FAILED - Conexión rechazada por servidor");
          break;
        case -2:
          Serial.println("Error: MQTT_CONNECTION_LOST - Conexión perdida o timeout");
          Serial.println("  → Podría ser DNS, SSL/TLS, o red");
          break;
        case -1:
          Serial.println("Error: MQTT_CONNECTION_TIMEOUT - Timeout en conexión");
          Serial.println("  → El servidor no responde en tiempo");
          break;
        case 0:
          Serial.println("Error: Conexión establecida pero no autenticada");
          break;
        case 1:
          Serial.println("Error: Versión de protocolo rechazada");
          break;
        case 2:
          Serial.println("Error: ID de cliente rechazado");
          break;
        case 3:
          Serial.println("Error: Servidor no disponible");
          break;
        case 4:
          Serial.println("Error: Usuario/contraseña incorrectos");
          break;
        case 5:
          Serial.println("Error: No autorizado");
          break;
        default:
          Serial.println("Error desconocido");
      }
      
      // Información de diagnóstico
      Serial.println("\n=== INFO DIAGNÓSTICO ===");
      Serial.print("WiFi conectado: ");
      Serial.println(WiFi.status() == WL_CONNECTED ? "✅SÍ" : "❌NO");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("DNS primario: ");
      Serial.println(WiFi.dnsIP(0));
      Serial.print("Señal WiFi (RSSI): ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      
      alert = "MQTT error: " + String(state);
      
      if ( client.state() == MQTT_CONNECT_UNAUTHORIZED ) {
        Serial.println("\n⚠️  Espera profunda de 30s por error de autorización...");
        delay(2000);
        ESP.deepSleep(30e6); // 30 segundos en microsegundos
      }
      
      Serial.println("=== Reintentando en 5s... ===\n");
      delay(5000); // Espera 5 segundos antes de volver a intentar
    }
  }
}


/**
 * Función setupIoT que configura el certificado raíz, el servidor MQTT y el puerto
 */
void setupIoT() {
  // Inicializar dinámicamente el client_id con la dirección MAC única del dispositivo
  static String macStr = getMacAddress();
  client_id = macStr.c_str();

  Wire.begin();                 //Inicializa el bus I2C: (SDA, SCL)
  
  Serial.println("\n=== SETUP TLS/SSL ===");
  Serial.println("Configurando certificado raíz CA...");
  espClient.setCACert(root_ca); //Configura el certificado raíz de la autoridad de certificación
  Serial.println("✓ Certificado raíz configurado");
  
  // Configuración de MQTT
  client.setServer(mqtt_server, mqtt_port);   //Configura el servidor MQTT y el puerto seguro
  
  // Configurar buffer más grande para mensajes grandes (por defecto es 256 bytes)
  client.setBufferSize(1024);
  
  client.setCallback(receivedCallback);       //Configura la función que se ejecutará cuando lleguen mensajes a la suscripción
  
  Serial.println("\n=== Configuración MQTT ===");
  Serial.print("Servidor MQTT: ");
  Serial.println(mqtt_server);
  Serial.print("Puerto MQTT: ");
  Serial.println(mqtt_port);
  Serial.print("Usuario MQTT: ");
  Serial.println(mqtt_user);
  Serial.print("Client ID: ");
  Serial.println(client_id);
  Serial.print("Buffer size: ");
  Serial.println(client.getBufferSize());
  Serial.println("Callback MQTT configurado: receivedCallback");
  
  // TEST DE CONECTIVIDAD
  Serial.println("\n=== TEST DE CONECTIVIDAD ===");
  Serial.print("Probando conexión TCP a ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(mqtt_port);
  
  if (espClient.connect(mqtt_server, mqtt_port)) {
    Serial.println("✓ Conexión TCP exitosa");
    espClient.stop();
  } else {
    Serial.println("✗ Falló conexión TCP - Podría ser firewall o puerto incorrecto");
  }
  
  Serial.println("==========================\n");
  
  setTime();                    //Ajusta el tiempo del dispositivo con servidores SNTP
  setupSHT();                   //Configura el sensor SHT21
}


/**
 * Configura el sensor SHT21
 */
void setupSHT() {
  if (sht.init()) Serial.print("SHT init(): Exitoso\n");
  else Serial.print("SHT init(): Fallido\n");
  sht.setAccuracy(SHTSensor::SHT_ACCURACY_MEDIUM); // soportado solo por el SHT3x
}


/**
 * Verifica si ya es momento de hacer las mediciones de las variables.
 * si ya es tiempo, mide y envía las mediciones.
 */
bool measure(SensorData * data) {
  if ((millis() - measureTime) >= MEASURE_INTERVAL * 1000 ) {
    PRINTLN("\nMidiendo variables...");
    measureTime = millis();    
    if (sht.readSample()) {
        data->temperature = sht.getTemperature();
        data->humidity = sht.getHumidity();
        PRINT(" %RH ❖ Temperatura: ");
        PRINTD(data->humidity, 2);
        PRINT(" %RH ❖ Temperatura: ");
        PRINTD(data->temperature, 2);
        PRINTLN(" °C");
        return true;
    } else {
        Serial.print("Error leyendo la muestra\n");
        return false;
    }
  }
  return false;
}

/**
 * Verifica si ha llegdo alguna alerta al dispositivo.
 * Si no ha llegado devuelve OK, de lo contrario retorna la alerta.
 * También asigna el tiempo en el que se dispara la alerta.
 */
String checkAlert() {
  if (alert.length() != 0) {
    if ((millis() - alertTime) >= ALERT_DURATION * 1000 ) {
      alert = "";
      alertTime = millis();
    }
    return alert;
  } else return "OK";
}

/**
 * Publica la temperatura y humedad dadas al tópico configurado usando el cliente MQTT.
 */
void sendSensorData(float temperatura, float humedad) {
  String data = "{";
  data += "\"temperatura\": "+ String(temperatura, 1) +", ";
  data += "\"humedad\": "+ String(humedad, 1);
  data += "}";
  char payload[data.length()+1];
  data.toCharArray(payload,data.length()+1);
  PRINTLN("client id: " + String(client_id) + "\ntopic: " + String(MQTT_TOPIC_PUB) + "\npayload: " + data);
  client.publish(MQTT_TOPIC_PUB, payload);
}


/**
 * Función que se ejecuta cuando llega un mensaje a la suscripción MQTT.
 * Construye el mensaje que llegó y si contiene ALERT lo asgina a la variable 
 * alert que es la que se lee para mostrar los mensajes.
 * También verifica si el mensaje es para actualización OTA.
 */
void receivedCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n*** CALLBACK MQTT DISPARADO ***");
  Serial.print("Topic recibido: [");
  Serial.print(topic);
  Serial.print("] (longitud payload: ");
  Serial.print(length);
  Serial.println(")");
  
  // Crear buffer para el payload (agregar null terminator)
  // Usar String para evitar problemas con VLA
  String data = "";
  for (unsigned int i = 0; i < length; i++) {
    data += (char)payload[i];
  }
  
  Serial.print("Payload: ");
  Serial.println(data);
  
  // Compara el topic recibido con el topic OTA
  String topicStr = String(topic);
  String otaTopicStr = String(OTA_TOPIC);
  
  Serial.println("--- Comparación de topics ---");
  Serial.print("Topic recibido: '");
  Serial.print(topicStr);
  Serial.print("' (longitud: ");
  Serial.print(topicStr.length());
  Serial.println(")");
  Serial.print("OTA_TOPIC esperado: '");
  Serial.print(otaTopicStr);
  Serial.print("' (longitud: ");
  Serial.print(otaTopicStr.length());
  Serial.println(")");
  Serial.print("¿Coinciden? ");
  Serial.println(topicStr == otaTopicStr ? "SÍ" : "NO");
  
  // Verifica si el mensaje es para actualización OTA
  if (topicStr == otaTopicStr) {
    Serial.println("✓✓✓ Mensaje OTA detectado, procesando...");
    checkOTAUpdate(data.c_str());
    return;
  }
  
  // Verifica si el mensaje contiene una alerta
  if (data.indexOf("ALERT") >= 0) {
    Serial.println("✓ Mensaje ALERT detectado");
    alert = data; // Si el mensaje contiene la palabra ALERT, se asigna a la variable alert
  } else {
    Serial.println("⚠ Mensaje recibido pero no es OTA ni ALERT");
  }
  Serial.println("*** FIN CALLBACK ***\n");
}

/**
 * Función de prueba: Publica un mensaje de prueba y verifica recepción
 * Útil para diagnosticar problemas de MQTT
 */
void testMQTTCallback() {
  if (!client.connected()) {
    Serial.println("⚠ No se puede probar: cliente MQTT no conectado");
    return;
  }
  
  Serial.println("=== TEST MQTT CALLBACK ===");
  Serial.println("Este test verifica que el callback funciona correctamente");
  Serial.println("Publicando mensaje de prueba...");
  
  // Publicar un mensaje de prueba al topic de entrada (para que el dispositivo lo reciba)
  String testTopic = String(MQTT_TOPIC_SUB);
  String testMessage = "TEST_MESSAGE_FROM_SELF";
  
  bool pubResult = client.publish(testTopic.c_str(), testMessage.c_str());
  if (pubResult) {
    Serial.println("✓ Mensaje de prueba publicado");
    Serial.println("Esperando recibirlo en el callback...");
    Serial.println("(Si el callback funciona, deberías ver '*** CALLBACK MQTT DISPARADO ***' arriba)");
  } else {
    Serial.println("✗ Error al publicar mensaje de prueba");
  }
  
  // Procesar mensajes varias veces para asegurar recepción
  for (int i = 0; i < 10; i++) {
    client.loop();
    delay(100);
  }
  
  Serial.println("=== FIN TEST ===");
}