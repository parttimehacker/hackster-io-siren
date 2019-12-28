#ifndef MQTT_Config_h
#define MQTT_Config_h

#define MQTT_HOST 		IPAddress(x, x, x, x)
#define MQTT_PORT 		1883

#define MQTT_TOPICS 5
char mqttTopics[MQTT_TOPICS][64] = {
  "diy/system/who",
  "diy/system/panic",
  "diy/system/fire"
};

#endif /* MQTT_Config_h */
