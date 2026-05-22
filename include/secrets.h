#ifndef SECRETS_H
#define SECRETS_H

#include <Arduino.h>

// Variables used by libwifi
extern const char* ssid;
extern const char* password;

// Variables used by libiot
extern const char* mqtt_server;
extern const int mqtt_port;
extern const char* mqtt_user;
extern const char* mqtt_password;
extern const char* client_id;

// MQTT topics
extern const char* MQTT_TOPIC_PUB;
extern const char* MQTT_TOPIC_SUB;
extern const char* MQTT_TOPIC_SUB_VALUE;

extern const char* root_ca;

#endif // SECRETS_H
