const char html[] PROGMEM = R"=====(
//String html = "<meta name='viewport' content='width=device-width, initial-scale=1' />"
              "<link rel='stylesheet' href='/style.css' type='text/css' />"
              "<h1>Simple web page test</h1>";

//)=====";

String teststring ="hej \
vad \
gör \
du";

String htmlcode="<!DOCTYPE html> \
<html> \
<body> \
<div id='demo'>\
<h1>EspSparnasGateway</h1>\
</div> \
<div>\
  <h3>\
  Current power: <span id='power'>0</span> W.<br>\
  </h3>\
</div>\
<script>\
setInterval(function() { \
  getData();\
}, 2000);\
function getData() {\
  var xhttp = new XMLHttpRequest();\
  xhttp.onreadystatechange = function() {\
    if (this.readyState == 4 && this.status == 200) {\
      document.getElementById('power').innerHTML =\
      this.responseText;\
    }\
  };\
  xhttp.open('GET', 'data', true);\
  xhttp.send();\
}\
</script>\
<br><br><h4>Web code inspired by <a href='https://circuits4you.com'>Circuits4you.com</a><br>\
<a href='https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/'>ESP8266 (ajax) update part of web page without refreshing</a>\
</h4>\
</body>\
</html>";
String ajaxcode="<!DOCTYPE html>\
<html>\
<body>\
<div id='demo'>\
<h1>EspSparnasGateway</h1>\
</div>\
<div>\
  <h3>\
  Current power: <span id='power'>0</span> W.<br>\
  </h3>\
</div>\
<script>\
setInterval(function() {\
  getData();\
}, 2000); \
function getData() {\
  var xhttp = new XMLHttpRequest();\
  xhttp.onreadystatechange = function() {\
    if (this.readyState == 4 && this.status == 200) {\
      document.getElementById('power').innerHTML =\
      this.responseText;\
    }\
  };\
  xhttp.open('GET', 'data', true);\
  xhttp.send();\  
}\
</script>\
<br><br><h4>Web code inspired by <a href='https://circuits4you.com'>Circuits4you.com</a><br>\
<a href='https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/'>ESP8266 (ajax) update part of web page without refreshing</a>\
</h4>\
</body>\
</html>";


