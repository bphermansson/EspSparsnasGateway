// When a Mqtt message has arrived
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonBuffer<150> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  char msg[150];
  
  // Extract topic
  Serial.print("Message arrived: ");
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

/* Settings freq to 868000010 or 868000001 doesnt make a difference
 *  Setting freq to 868000100 gives 868000128hz.
 *                  868000110 ->    868000128hz.
 *  868001000 -> 868001024hz.
 */
      int charLength=stringPayload.length();
      if (charLength==6)
      { 
        Serial.print("Set frequency to: ");
        Serial.println(stringPayload);
        // Write bit that indicates stored settings
        EEPROM.write(addr, 1); // Indicate stored settings by writing 1 to Eeprom address 0
        addr = 2;
        
        for (int i = addr; i < stringPayload.length()+2; ++i)
        {
          EEPROM.write(i, stringPayload[i-2]);
          Serial.print("Wrote: ");
          Serial.println(stringPayload[i-2]);
  
        }
        EEPROM.commit();
  
        root["status"] = "Frequency changed";
        root.printTo((char*)msg, root.measureLength() + 1);
        client.publish(mqtt_debug_topic, msg);
        
        delay(10);
        ESP.restart();
      }
      else 
      {
        Serial.println("Error in frequency length, use 6 digits like '868.23'");
      }

  }
  if(strcmp(topic, "EspSparsnasGateway/settings/senderid") == 0) {  
      Serial.println("Senderid change");
      addr++; 
      EEPROM.write(addr, 1);    // Indicate stored settings by writing 1 to Eeprom address 1
      byte offs = 12;
      addr = addr + offs;
      Serial.println(addr);
      int charLength=stringPayload.length();
      for (int i = addr; i < charLength+offs; ++i)
      {
        EEPROM.write(i, stringPayload[i-offs]);
        Serial.print("Wrote: ");
        Serial.println(stringPayload[i-offs]);
      }
      EEPROM.commit();

      root["status"] = "Sender id changed";
      root["NewSenderId"] = stringPayload;
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
      
      delay(10);
      ESP.restart();      
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
      ESP.restart();
  }
    if(strcmp(topic, "EspSparsnasGateway/settings/reset") == 0) {  
      Serial.println("Reset");
      root["status"] = "Reset";
      root.printTo((char*)msg, root.measureLength() + 1);
      client.publish(mqtt_debug_topic, msg);
      ESP.restart();
    }
}
