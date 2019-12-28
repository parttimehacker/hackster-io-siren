/*
  # The MIT License (MIT)
  #
  # Copyright (c) 2019 parttimehacker@gmail.com
  #
  # Permission is hereby granted, free of charge, to any person obtaining a copy
  # of this software and associated documentation files (the "Software"), to deal
  # in the Software without restriction, including without limitation the rights
  # to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  # copies of the Software, and to permit persons to whom the Software is
  # furnished to do so, subject to the following conditions:
  #
  # The above copyright notice and this permission notice shall be included in
  # all copies or substantial portions of the Software.
  #
  # THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  # IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  # FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  # AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  # LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  # OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  # THE SOFTWARE.
*/

#include "arduinoConfig.h"
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include "networkConfig.h"
#include "mqttConfig.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

char    macAddressTopic[64];
char    locationTopic[64];
char    clientId[64];
uint8_t MAC_array[6];
char    MAC_char[18];

///////
// CONSTANTS

#define MAX_FRAME       10
#define FRAME_INTERVAL  100UL

#define PIEZO_PIN       17

///////
// OBJECTS

uint8_t frame = 1;
unsigned long frameMilliseconds = 0;

bool panicOn = false;
bool panicPingPong = false;

/////////////////////////////////////////////////////////////////////////////
// WIFI STUFF
///////

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());

      WiFi.macAddress(MAC_array);
      for (int i = 0; i < sizeof(MAC_array); ++i) {
        sprintf(MAC_char, "%s%02x:", MAC_char, MAC_array[i]);
      }
      sprintf(macAddressTopic, "diy/esp_%02x%02x%02x", MAC_array[3], MAC_array[4], MAC_array[5]);
      sprintf(clientId, "ESP_%02X%02X%02X", MAC_array[3], MAC_array[4], MAC_array[5]);
      Serial.print("topic="); Serial.println(macAddressTopic);
      Serial.print("clientId="); Serial.println(clientId);

      connectToMqtt();
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}

/////////////////////////////////////////////////////////////////////////////
// MQTT STUFF
///////

void setupTopics(char *payload) {
  sprintf(locationTopic, "%s/alarm", payload);
  Serial.print("setup topics: location=> "); Serial.println(locationTopic);
  for (int i = 0; i < MQTT_TOPICS; i++) {
    mqttClient.subscribe(mqttTopics[i], 2);
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  uint16_t packetIdSub = mqttClient.subscribe(macAddressTopic, 2);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (strcmp((char *)topic, macAddressTopic) == 0) {
    Serial.print("topic="); Serial.print(topic);
    Serial.print("\tpayload="); Serial.println(payload);
    Serial.print("\tlen="); Serial.println(len);
    payload[len] = 0;
    setupTopics(payload);
  } else if (strcmp((char *)topic, "diy/system/panic") == 0) {
    if (strncmp((char *)payload, "ON", 2) == 0) {
      Serial.println("panic ON!");
      panicOn = true;
    } else {
      Serial.println("panic OFF!");
      panicOn = false;
      panicPingPong = false;
      digitalWrite(PIEZO_PIN, LOW);
    }
  } else if (strcmp((char *)topic, "diy/system/fire") == 0) {
    if (strncmp((char *)payload, "ON", 2) == 0) {
      Serial.println("fire ON!");
      digitalWrite(PIEZO_PIN, HIGH);
    } else {
      Serial.println("fire OFF!");
      digitalWrite(PIEZO_PIN, LOW);
    }
  } else if (strcmp((char *)topic, "diy/system/who") == 0) {
    if (strncmp((char *)payload, "ON", 2) == 0) {
      Serial.println("who ON!");
    } else {
      Serial.println("who OFF!");
    }
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
}

void setupMqtt() {
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();

  while (locationTopic[0] == 0) {
    delay(100);
  }
}


/////////////////////////////////////////////////////////////////////////////
// INITIALIZE - SETUP
///////

void setup() {
  Serial.begin(115200);
  Serial.println("diysiren");

  pinMode(PIEZO_PIN, OUTPUT);

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

  WiFi.onEvent(WiFiEvent);

  // prepare MQTT services and event handlers
  setupMqtt();
  Serial.println("MQTT setup");

  Serial.println("Starting loop");
  frameMilliseconds = millis();
}

/////////////////////////////////////////////////////////////////////////////
// MAIN PROCESSING
///////

void loop() {

  // timed events

  unsigned long time = millis();
  unsigned long duration = time - frameMilliseconds;

  if (duration > FRAME_INTERVAL) {

    frameMilliseconds = time;
    frame++;

    // process frame sequenced code
    switch (frame) {
      case 1: {
          if (panicOn) {
            if (panicPingPong) {
              digitalWrite(PIEZO_PIN, LOW);
              panicPingPong = false;
            } else {
              digitalWrite(PIEZO_PIN, HIGH);
              panicPingPong = true;
            }
          }
        }
        break;
      case 2:  {
        }
        break;
      case 3: {
        }
        break;
      case 10: {
          frame = 0;
        }
        break;
      default:
        break;
    }
  }
}
