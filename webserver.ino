void handleRoot() {
  server.send ( 200, "text/html", html );   
}

void handleDataHttp() {
  //Serial.println("handleDataHttp");
  String hwatt = String(watt);
  String hbattery = String(battery);
  String data = hwatt +";"+ hbattery;
  server.send(200, "text/plane", data); //Send value only to client ajax request
}

