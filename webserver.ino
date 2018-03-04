void handleRoot() {
  Serial.println("Got Http request");
  String content = "<!DOCTYPE HTML>\r\n<html>Hello from " + String(appname);
        content += "<form method='get' action='setting'>
        <label>Frequency: </label><input name='ssid' length=32>
        <label>Senderid: </label><input name='pass' length=64><input type='submit'></form>";
        content += "</html>";
  server.send(200, "text/html", content); 
}
