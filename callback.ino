// When a Mqtt message has arrived
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonBuffer<150> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  char msg[150];
  
  //client.publish(mqtt_status_topic, "Got a Mqtt mess.");
  //Serial.print("Topic: ");
  //Serial.println(topic);
  // Extract topic
  Serial.print(F("Message arrived ["));
  Serial.println(topic);

  // Extract payload
  String stringPayload = "";
  for (int i = 0; i < length; i++) {
    stringPayload += (char)payload[i];
  }  
  #ifdef DEBUG
    Serial.print (F("Payload: "));
    Serial.println(stringPayload);
  #endif

  int addr = 0;
  
  if(strcmp(topic, "EspSparsnasGateway/settings/frequency") == 0) {  

      Serial.println(F("Frequency change"));

      // Write bit that indicates stored settings
      EEPROM.write(addr, 1);
      addr = addr + 1;
      
      int charLength=stringPayload.length();
      for (int i = addr; i < stringPayload.length()+1; ++i)
      {
        EEPROM.write(i, stringPayload[i-1]);
        //Serial.print("Wrote: ");
        //Serial.println(stringPayload[i-1]);

      }
      EEPROM.commit();

      root["status"] = "Frequency changed";
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
      
      delay(10);
      ESP.reset();

  }
  if(strcmp(topic, "EspSparsnasGateway/settings/senderid") == 0) {  
      Serial.println("Senderid change");
      EEPROM.write(addr, 1);
      addr = addr + 10;
      Serial.println(addr);
      int charLength=stringPayload.length();
      for (int i = addr; i < charLength+10; ++i)
      {
        EEPROM.write(i, stringPayload[i-10]);
        Serial.print("Wrote: ");
        Serial.println(stringPayload[i-10]);
      }
      EEPROM.commit();

      root["status"] = "Sender id changed";
      root["NewSenderId"] = stringPayload;
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
      
      delay(10);
      ESP.reset();      
  }
  if(strcmp(topic, "EspSparsnasGateway/settings/clear") == 0) {  
      Serial.println("Clear settings");
      for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();
      
      root["status"] = "Settings cleared";
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);

      delay(10);
      ESP.reset();
  }
    if(strcmp(topic, "EspSparsnasGateway/settings/reset") == 0) {  
      Serial.println("Reset");
      root["status"] = "Reset";
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
      ESP.reset();
    }
}
