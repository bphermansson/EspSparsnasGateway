void handleRoot() {
  Serial.println("Web request");
  server.send ( 200, "text/html", htmlcode );   
}

void handleDataHttp() {
  //Serial.println("handleDataHttp");
  String hwatt = String(watt);
  String hbattery = String(battery);
  String data = hwatt +";"+ hbattery;
  Serial.println(data);
  server.send(200, "text/plain", data); //Send value only to client ajax request
}

/*
// Webclient
void showpage() {
  Serial.println("Web client connected");
  while (webclient.connected()) {            // loop while the client's connected
      if (webclient.available()) {             // if there's bytes to read from the client,
        char c = webclient.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            webclient.println("HTTP/1.1 200 OK");
            webclient.println("Content-type:text/html");
            webclient.println("Connection: close");
            webclient.println();
            webclient.println("<!DOCTYPE html><html>");
            webclient.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            webclient.println("<body><h1>ESP8266 Web Server</h1>");
            webclient.println("</body></html>");
            webclient.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
   }
      // Clear the header variable
    header = "";
    // Close the connection
    webclient.stop();
    Serial.println("Client disconnected.");
    Serial.println("");

}

*/
