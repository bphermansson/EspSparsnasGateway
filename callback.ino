// When a Mqtt message has arrived
void callback(char* topic, byte* payload, unsigned int length) {
  //client.publish(mqtt_status_topic, "Got a Mqtt mess.");
  //Serial.print("Topic: ");
  //Serial.println(topic);
  // Extract topic
  Serial.print("Message arrived [");
  Serial.println(topic);

  // Extract payload
  String stringPayload = "";
  for (int i = 0; i < length; i++) {
    stringPayload += (char)payload[i];
  }  
  #ifdef DEBUG
    Serial.print ("Payload: ");
    Serial.println(stringPayload);
  #endif

  int addr = 0;
  
  if(strcmp(topic, "EspSparsnasGateway/settings/frequency") == 0) {  

      Serial.println("Frequency change");

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
      delay(10);
      ESP.reset();      
  }
  if(strcmp(topic, "EspSparsnasGateway/settings/clear") == 0) {  
      Serial.println("Clear settings");
      for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();
      client.publish(mqtt_status_topic, "Settings cleared");
      delay(10);
      ESP.reset();
  }
    if(strcmp(topic, "EspSparsnasGateway/settings/reset") == 0) {  
      Serial.println("Reset");
      ESP.reset();
    }
}
