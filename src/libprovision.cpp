#include <WiFi.h>
#include <WebServer.h>
#include <libstorage.h>
#include <libprovision.h>

static WebServer server(80);
static bool s_isProvisioning = false;

static void handleRoot() {
  // Escanear redes en el momento
  int n = WiFi.scanNetworks();
  
  String html = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Configuracion Wi-Fi</title>
  <style>
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      margin: 0;
      padding: 0;
      background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
      color: #333;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }
    .container {
      background: rgba(255, 255, 255, 0.95);
      padding: 30px;
      border-radius: 12px;
      box-shadow: 0 8px 32px rgba(0,0,0,0.3);
      width: 100%;
      max-width: 400px;
      box-sizing: border-box;
    }
    h2 {
      margin-top: 0;
      color: #1e3c72;
      text-align: center;
      font-weight: 600;
    }
    p {
      text-align: center;
      color: #666;
      font-size: 14px;
      margin-bottom: 20px;
    }
    label {
      font-weight: bold;
      font-size: 14px;
      color: #444;
      display: block;
      margin-top: 12px;
    }
    select, input, button {
      font-size: 16px;
      padding: 12px;
      margin: 8px 0 16px 0;
      width: 100%;
      border-radius: 6px;
      border: 1px solid #ccc;
      box-sizing: border-box;
      outline: none;
      transition: all 0.3s ease;
    }
    select:focus, input:focus {
      border-color: #1e3c72;
      box-shadow: 0 0 8px rgba(30, 60, 114, 0.2);
    }
    button {
      background: #1e3c72;
      color: #fff;
      border: none;
      cursor: pointer;
      font-weight: bold;
      margin-top: 8px;
    }
    button:hover {
      background: #2a5298;
    }
    .footer {
      text-align: center;
      font-size: 11px;
      color: #aaa;
      margin-top: 20px;
    }
  </style>
</head>
<body>
  <div class='container'>
    <h2>Configurar Wi-Fi</h2>
    <p>Selecciona tu red de la lista o escribela manualmente</p>
    <form method='POST' action='/save'>
      <label for='ssid'>Redes Wi-Fi Detectadas (2.4 GHz)</label>
      <select id='ssid' name='ssid'>
  )HTML";

  if (n <= 0) {
    html += "<option value=''>No se encontraron redes (Escribe abajo)</option>";
  } else {
    html += "<option value=''>-- Selecciona una red --</option>";
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      html += "<option value='" + ssid + "'>" + ssid + " (" + String(rssi) + " dBm)</option>";
    }
  }
  WiFi.scanDelete();

  html += R"HTML(
      </select>
      
      <label for='manual_ssid'>O escribe el SSID manualmente</label>
      <input type='text' id='manual_ssid' name='manual_ssid' placeholder='SSID manual (si no aparece arriba)'>
      
      <label for='password'>Contrasena</label>
      <input type='password' id='password' name='password' placeholder='Contrasena de la red' required>
      
      <button type='submit'>Guardar y Conectar</button>
    </form>
    <div class='footer'>IoT MQTT TLS Device Setup Portal</div>
  </div>
</body>
</html>
  )HTML";

  server.send(200, "text/html", html);
}

static void handleSave() {
  String ssid = "";
  if (server.hasArg("manual_ssid") && server.arg("manual_ssid").length() > 0) {
    ssid = server.arg("manual_ssid");
  } else if (server.hasArg("ssid") && server.arg("ssid").length() > 0) {
    ssid = server.arg("ssid");
  }
  
  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID invalido o requerido");
    return;
  }
  
  String pwd = server.arg("password");
  if (!saveWiFiCredentials(ssid, pwd)) {
    server.send(500, "text/plain", "No se pudo guardar en memoria NVS");
    return;
  }
  
  server.send(200, "text/plain", "Datos guardados exitosamente. El dispositivo se reiniciara ahora para conectarse.");
  delay(1000);
  ESP.restart();
}

static String apName;

void startProvisioningAP() {
  // Desconectar cualquier conexión STA previa y limpiar el estado
  WiFi.disconnect(true, true);
  delay(100);
  
  WiFi.mode(WIFI_AP_STA);
  apName = String("ESP32-Setup-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  WiFi.softAP(apName.c_str());
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Provisioning AP "); Serial.print(apName); Serial.print(" IP "); Serial.println(ip);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  s_isProvisioning = true;
}

void provisioningLoop() {
  if (!s_isProvisioning) return;
  server.handleClient();
}

bool isProvisioning() { return s_isProvisioning; }
