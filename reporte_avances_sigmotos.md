# Reporte de Avances: Firmware IoT con MQTT-TLS y CI/CD (Sigmotos)

## Resumen Ejecutivo
Este documento recopila el progreso técnico del firmware y pipeline de desarrollo para **Sigmotos-MQTT**, un sistema embebido modular y seguro para ESP32-S3 con actualizaciones OTA autónomas. El proyecto implementa un firmware producción-ready con comunicación cifrada TLS, aprovisionamiento WiFi por portal AP, testing automático de 57 casos de prueba, y un pipeline CI/CD robusto con GitHub Actions que compila, prueba, cubre cobertura de código y publica actualizaciones remotas en AWS S3.

---

## 1. Arquitectura del Firmware (C++ + PlatformIO)
El firmware se organiza en una arquitectura modular con responsabilidades bien definidas, basada en bibliotecas especializadas.

### Stack Tecnológico
- **Lenguaje:** C++ con Arduino Framework
- **Build System:** PlatformIO (compatible con VS Code)
- **Plataforma:** ESP32-S3-DevKitM-1 (SoC Xtensa dual-core 2.4 GHz, 8 MB RAM, 16 MB Flash)
- **Versión Firmware:** v1.1.1
- **Dependencias Key:**
  - `PubSubClient` v2.8 (cliente MQTT con TLS)
  - `Adafruit SSD1306` v2.5.12 (pantalla OLED 128x64)
  - `Sensirion SHT` v1.2.5 (sensor temperatura/humedad)
  - `ArduinoJson` v6.21.3 (parsing JSON para OTA messages)

### Módulos del Firmware (librerías)
```
libwifi/      → Gestión de conexión WiFi y listas de redes disponibles
libiot/       → Core MQTT: conexión TLS, publicación/suscripción, mensajes
libota/       → Descarga e instalación de actualizaciones remotas
libdisplay/   → Interfaz OLED para mostrar estado y mensajes
libstorage/   → Persistencia en NVS (WiFi credentials, firmware version)
libprovision/ → Portal AP para aprovisionamiento WiFi en primera ejecución
libsecrets/   → Constantes de seguridad (certificados raíz, configuración)
```

### Flujo de Inicialización
1. Inicializa puerto serie (115200 baud)
2. Detecta factory reset si BOOT (GPIO0) presionado > 3 segundos
3. Lista redes WiFi disponibles en el entorno
4. Inicializa pantalla OLED
5. Si no hay credenciales almacenadas → inicia modo Provisioning AP (SSID: `ESP32-Setup-XXXXXX`, IP: 192.168.4.1)
6. Si hay credenciales → conecta a WiFi y luego a broker MQTT
7. Sincroniza hora mediante SNTP
8. Queda en loop esperando: datos de sensores, mensajes MQTT de OTA, o solicitudes del portal AP

## 2. Seguridad y Comunicación MQTT-TLS
La comunicación entre el firmware y el broker MQTT es completamente cifrada mediante TLS 1.2, garantizando confidencialidad e integridad de los datos de telemetría.

### Flujo de Autenticación TLS
- **Certificado Raíz Embebido:** El firmware incluye el certificado raíz de LetsEncrypt (ISRG Root X1) compilado en la imagen
- **Verificación de Servidor:** Antes de conectar, la ESP32 valida matemáticamente que el certificado del broker es válido y emitido por una CA confiable
- **CrAprovisionamiento WiFi (Portal AP + NVS Persistente)
El dispositivo implementa un mecanismo de aprovisionamiento sin cableado, guardando credenciales en memoria no volátil para reinicios posteriores.

### Primer Arranque
1. Si no hay credenciales almacenadas → crea un Access Point (AP) con SSID `ESP32-Setup-XXXXXX`
2. El usuario se conecta a este AP desde su smartphone/laptop
3. Abre navegador a `Robusto (GitHub Actions + Testing + OTA)
El desarrollo está automatizado desde el commit hasta la actualización remota del dispositivo, con múltiples capas de validación.

### Stages del Pipeline (`.github/workflows/ci.yml`)

#### Job 1: Unit Tests & Code Coverage (Bloqueante)
- **Entorno:** native (compilación sin dependencias de Arduino/Hardware)
- **Cantidad de tests:** 38 tests de lógica pura
- **Duración:** ~2 segundos
- **Funciones probadas:**
  - `buildSensorPayload()` → validación del formato JSON del payload MQTT
  - `isAlertMessage()` → detección de alertas en mensajes
  - `isOTAMessage()` → filtrado de tópicos MQTT
  - `isValidSSID()` → validación de estándar IEEE 802.11
  - `isValidVersion()` → formato semántico vX.Y.Z
  - `formatDeviceID()` → generación de identificador único
  - `isMeasureTime()` → temporización de mediciones (overflow de millis)
  - `isAlertExpired()` → expiración de alertas basada en timestamp
- **CoComunicación MQTT y Payloads de Sensores
El dispositivo publica datos de telemetría periódicamente en formato JSON estructurado.

### Tópicos MQTT
- **Publicación:** `/devices/{country}/{state}/{city}/{device_id}/telemetry`
  - Formato: `{"temperatura": 25.5, "humedad": 60.3}`
  - Frecuencia: Configurable (default ~5 segundos entre lecturas)
  - Sensor: SHT21 (Sensirion, 0.5°C de precisión)

- **Suscripción:** `/devices/{country}/{state}/{city}/{device_id}/ota/update`
  - Escucha mensajes de actualización de firmware
  - Payload esperado: `{"version": "vX.Y.Z", "url": "https://..."}`

### Validaciones en Payload
- SSID válido: 1-32 caracteres, permitidos caracteres SSID
- Versión: formato semántico vX.Y.Z estricto
- Timestamps: detección y manejo de overflow de `millis()` (cada 49.7 días)
- Expiración de alertas: si no se recibe confirmación en 30s, se reintenta

## 6. Compilación y Deployment Local

### Requisitos
- Python 3.11+
- PlatformIO CLI o VS Code extension
- ESP32-S3-DevKitM-1 conectado por USB

### Configuración Inicial
Crear `.env` en raíz del proyecto:
```ini
COUNTRY=colombia
STATE=valle
CITY=tulua
MQTT_SERVER=mqtt.tu-dominio.com
MQTT_PORT=8883
MQTT_USER=miuser
MQTT_PASSWORD=supersecreto
WIFI_SSID=TuRed
WIFI_PASSWORD=TuPassword
```

### Build & Upload
```bash
# Opción 1: Script automático (recomendado)
python scripts/build_with_env.py upload

# Opción 2: Manual
set -a && source .env && set +a
pio run -t upload
```

### Velocidades de Transferencia
- **Monitor Serial:** 115200 baud
- **Upload firmware:** 460800 baud (USB)

## 7. Testing Automático

### Ejecutar Tests Localmente
```bash
# Unit tests (lógica pura, ~2s)
./run_tests.sh unit

# Hardware tests (requiere ESP32, ~35s)
./run_tests.sh hardware

# Todo + cobertura
./run_tests.sh coverage
```

### Suite de Tests Actual
- **Unit tests:** 38 tests (lógica, JSON, validaciones)
- **Hardware tests:** 19 tests (integración real con ESP32)
- **Total:** 57 tests
- **Cobertura:** Medida con lcov, reporte en `coverage_html/`

## 8. Documentación del Proyecto

| Documento | Propósito |
|-----------|-----------|
| `QUICK_START.md` | Guía rápida: compilar y subir |
| `SECRETS_SETUP.md` | Gestión de secretos y variables de entorno |
| `WINDOWS_SETUP.md` | Configuración específica para Windows |
| `WIFI_SETUP.md` | Portal AP y aprovisionamiento WiFi |
| `OTA_SETUP.md` | Configuración del sistema OTA |

---

## 9. Estado Actual y Próximos Pasos (Roadmap)

### Completado ✅
- ✅ Arquitectura modular de firmware con 8 librerías
- ✅ Comunicación MQTT-TLS con certificados válidos
- ✅ Portal AP para aprovisionamiento WiFi sin cables
- ✅ Persistencia de credenciales en NVS
- ✅ Pipeline CI/CD con 3 stages (unit tests → hardware tests → OTA deploy)
- ✅ 57 tests automatizados con cobertura de código
- ✅ Gestión segura de secretos (build-time injection)
- ✅ Actualizaciones OTA autónomas desde S3
- ✅ Factory reset con botón BOOT
- ✅ Soporte para Windows, Mac, Linux

### En Progreso 🔄
- 🔄 Validación end-to-end completa (inyectar datos simulados → verificar en backend)
- 🔄 Integración con backend receiver-iot para persistencia en TimescaleDB

### Próximos Pasos 📋
1. **Consolidación de Tópicos MQTT:** Estandarizar nombre de tópicos según estructura de negocio
2. **Monitoreo y Logging Remoto:** Agregar capacidad de enviar logs críticos a backend
3. **Retry Policy Avanzada:** Implementar backoff exponencial para reconexión MQTT
4. **Resistencia a Caídas de Red:** Queue local en SPIFFS para mensajes mientras no hay conectividad
5. **Optimización de Potencia:** Reducir consumo con deep sleep entre mediciones
6. **Soporte Multi-Sensor:** Expandir para más sensores (GPS, acelerómetro, etc.)
- **Trigger:** main branch O tags v*.*.* (requiere que unit-tests pasen)
- **Pasos:**
  1. **Build:** `platformio run` con variables de entorno del repo secrets
  2. **Upload a S3:** Sube binario a `s3://{BUCKET}/{firmware_vX.Y.Z.bin}`
  3. **Publish MQTT:** Publica mensaje JSON a tópico de OTA con URL de descarga
- **Variables usadas:** `COUNTRY`, `STATE`, `CITY`, `MQTT_*`, `WIFI_*`, `S3_*`, `AWS_*`

### Flujo OTA en el Dispositivo
1. ESP32 suscrita al tópico `/devices/{ID}/ota/update`
2. Recibe mensaje JSON: `{"version": "v1.1.2", "url": "https://...firmware.bin"}`
3. Descarga el binario por WiFi directamente desde S3
4. Valida integridad (checks en `libota.cpp`)
5. Escribe en flash (segundo sector)
6. Se reinicia automáticamente
7. Bootloader verifica y activa la nueva imagen
8. NVS se actualiza con la nueva versión
- No requiere intervención del usuario

### Factory Reset
- Mantener BOOT (GPIO0) presionado > 3 segundos durante arranque
- Borra las credenciales almacenadas
- Inicia nuevamente en modo AP

### Datos Almacenados en NVS
- SSID de la red
- Contraseña de la red
- Versión del firmware actual (para validar OTA)
- **Script Python:** `scripts/add_env_defines.py` procesa `.env` y genera macros de compilación
- **Variables soportadas:** `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASSWORD`, `ROOT_CA`
- **GitHub Secrets:** Para CI/CD, los mismos secretos se almacenan en GitHub Actions Secrets (Settings → Secrets and variables)

## 3. Firmware de la ESP32 (C++)
Se desarrolló el software para el microcontrolador ESP32-S3 (el cerebro en la moto) utilizando buenas prácticas de seguridad e ingeniería de software.
* **Gestión Segura de Secretos:** Se implementó un script de Python (`build_with_env.py`) que inyecta dinámicamente las credenciales (WiFi, MQTT, contraseñas) desde un archivo local `.env` en tiempo de compilación. Esto asegura que ninguna contraseña sensible se suba accidentalmente a GitHub.
* **Arquitectura Modular:** El código se dividió en bibliotecas especializadas (`libwifi`, `libiot`, `libota`), aislando responsabilidades y facilitando la mantenibilidad (Principio de Responsabilidad Única).
* **Conexión Validada por TLS:** El firmware incluye incrustado el Certificado Raíz (*ISRG Root X1*) para autenticar la identidad del servidor EMQX. La ESP32 verifica matemáticamente que el servidor es quien dice ser antes de enviarle cualquier dato.

## 4. Pipeline CI/CD y Actualizaciones OTA (Over The Air)
Se automatizó el proceso de llevar mejoras de código desde la computadora de los desarrolladores hasta el hardware físico, sin necesidad de cables.
* **GitHub Actions:** Se estructuró un pipeline (`ota-update.yml`) que se activa automáticamente al crear un *Tag* o versión en GitHub.
* **Flujo Híbrido Nube/Hardware:** 
  1. Los servidores de GitHub compilan el firmware de la moto.
  2. Suben el binario (`.bin`) resultante a un bucket de almacenamiento masivo en AWS S3.
  3. Publican un mensaje MQTT en formato JSON con la URL de descarga hacia el tópico al que la moto está suscrita.
* **Módulo OTA Autónomo:** La ESP32 fue programada para escuchar estos mensajes de alerta, parsear el JSON recibido mediante `ArduinoJson`, descargar la actualización por WiFi directamente desde S3, e instalarla reiniciándose sola.

## 5. Almacenamiento y Visualización
Para persistir los datos de telemetría y mostrarlos gráficamente al equipo o cliente final:
* **TimescaleDB:** Se implementó una base de datos PostgreSQL extendida y optimizada específicamente para almacenar grandes volúmenes de series de tiempo (ideal para guardar un histórico preciso de los sensores de la moto).
* **Receiver IoT Service:** Un microservicio *backend* (`alvarosalazar/receiver-iot`) que actúa como puente: se suscribe al broker EMQX, recibe la información en vivo y la inserta automáticamente en la base de datos.
* **Grafana:** Se desplegó un panel de analítica de datos conectado a TimescaleDB para visualizar el comportamiento de la flota en tiempo real a través del puerto 3000.

---

## 6. Estado Actual y Próximos Pasos (Roadmap)
El proyecto ha superado con éxito la fase de interconexión segura, considerada el mayor reto de la IoT industrial. Los pasos a seguir son:
1. **Consolidación de IP / Dominio:** Mantener la IP de AWS estática (Elastic IP) o actualizar consistentemente FreeDNS para evitar cortes de comunicación por cambios de IP tras reinicios de servidor.
2. **Pruebas de Inyección de Datos (E2E):** Finalizar el testeo completo de todo el recorrido inyectando valores simulados (temperatura, ubicación, velocidad) desde la ESP32 y verificando su correcta representación en los paneles de Grafana.
3. **Puesta a punto de Dashboards:** Diseñar las interfaces gráficas en Grafana adaptadas exactamente a las métricas del negocio de Sigmotos.
