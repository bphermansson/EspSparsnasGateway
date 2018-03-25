const char html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<body>

<div id="demo">
<h1>EspSparnasGateway</h1>
<!--  <button type="button" onclick="sendData(1)">LED ON</button>
  <button type="button" onclick="sendData(0)">LED OFF</button><BR>-->
</div>

<div>
  <h3>
  Current power: <span id="power">0</span> W.<br>
  </h3>
</div>
<script>
/*
function sendData(led) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("LEDState").innerHTML =
      this.responseText;
    }
  };
  xhttp.open("GET", "setLED?LEDstate="+led, true);
  xhttp.send();
}
*/
setInterval(function() {
  // Call a function repetatively with 2 Second interval
  getData();
}, 2000); //2000 mSeconds update rate

function getData() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("power").innerHTML =
      this.responseText;
    }
  };
  xhttp.open("GET", "data", true);
  xhttp.send();
  
}
</script>
<br><br><h4>Web code inspired by <a href="https://circuits4you.com">Circuits4you.com</a><br>
<a href="https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/">ESP8266 (ajax) update part of web page without refreshing</a>
</h4>
</body>
</html>
)=====";
