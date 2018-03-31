void handleRoot() {
  Serial.println("Web request");
  server.send ( 200, "text/html", htmlcode );   
}

void handleDataHttp() {
  //Serial.println("handleDataHttp");
  String hwatt = String(round(watt));
  String hbattery = String(battery);
  String hseq = String(seq);
  String webdata = hwatt +";"+ hbattery + ";" + seq;
  Serial.print("Web data: ");
  Serial.println(webdata);
  server.send(200, "text/plain", webdata); //Send value only to client ajax request
}
