#include <WiFiClient.h>
#include <WiFi.h>
#include <libwifi.h>
#include <libdisplay.h>
#include <libstorage.h>
#include <Arduino.h>


/**
 * Verifica si el dispositivo está conectado al WiFi.
 * Si no está conectado intenta reconectar a la red.
 * Si está conectado, intenta conectarse a MQTT si aún 
 * no se tiene conexión.
 */
void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    WiFi.reconnect();
    
    // Wait for reconnection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected");
    } else {
      Serial.println("\nWiFi reconnection failed");
    }
  }
}

/**
 * Imprime en consola la cantidad de redes WiFi disponibles y
 * sus nombres.
 */
void listWiFiNetworks() {
  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    Serial.println("No networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.println("dBm)");
    }
  }
  
  // Delete the scan result to free memory
  WiFi.scanDelete();
}

/**
 * Adquiere la dirección MAC del dispositivo y la retorna en formato de cadena.
 */
String getHostname() {
  uint8_t mac[6];
  char macStr[18];
  WiFi.macAddress(mac);
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

/**
 * Inicia el servicio de WiFi usando ÚNICAMENTE las credenciales
 * almacenadas en NVS (ingresadas a través del portal web de configuración).
 * Si no hay credenciales almacenadas, no intenta conectarse.
 */
void startWiFi(const char* hostname) {
  // Asegurar que el Wi-Fi está en modo Estación (STA)
  WiFi.mode(WIFI_STA);
  delay(100);

  if (hostname && strlen(hostname) > 0) {
    WiFi.setHostname(hostname);
  }

  String s, p;
  if (!loadWiFiCredentials(s, p)) {
    Serial.println("No hay credenciales WiFi en NVS. Use el portal de configuración.");
    return;
  }

  Serial.print("Conectando a SSID: \"");
  Serial.print(s);
  Serial.println("\"");
  Serial.print("Longitud contraseña: ");
  Serial.println(p.length());

  WiFi.begin(s.c_str(), p.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");
    Serial.print("Direccion IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS1: ");
    Serial.println(WiFi.dnsIP(0));
    Serial.print("DNS2: ");
    Serial.println(WiFi.dnsIP(1));
    
    delay(500); // Esperar a que se estabilice la conexión
    
    // Intentar múltiples estrategias de DNS
    Serial.println("\n--- Configurando DNS ---");
    
    // Estrategia 1: Usar DNS del router local
    IPAddress localDns = WiFi.gatewayIP();
    Serial.print("Intentando DNS del router (");
    Serial.print(localDns);
    Serial.println(")...");
    
    // Estrategia 2: Google DNS como respaldo
    IPAddress dns1(8, 8, 8, 8);        // Google DNS primario
    IPAddress dns2(1, 1, 1, 1);        // Cloudflare DNS secundario
    
    // WiFi.config() con DNS personalizado
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
    
    Serial.println("DNS configurado:");
    Serial.print("  Primario (Google): ");
    Serial.println(dns1);
    Serial.print("  Secundario (Cloudflare): ");
    Serial.println(dns2);
    
  } else {
    Serial.println("\nWiFi: fallo la conexion");
  }
}

bool hasStoredWiFi() {
  return hasWiFiCredentials();
}

bool saveWiFi(const String &s, const String &pwd) {
  return saveWiFiCredentials(s, pwd);
}

bool clearStoredWiFi() {
  return clearWiFiCredentials();
}

void factoryReset() {
  Serial.println("Factory reset: clearing WiFi credentials and restarting...");
  clearWiFiCredentials();
  WiFi.disconnect(true, true);
  delay(500);
  ESP.restart();
}
