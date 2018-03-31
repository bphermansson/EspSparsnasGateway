String htmlcode="<!DOCTYPE html> \
<html> \
<body style='background:black;'> \
<div id='title' style='color:green;'>\
<h1>EspSparsnasGateway</h1>\
</div> \
<div id='seqdiv' style='color:yellow;'>\
  <h3>\
  Sequence: <span id='seq'>0</span><br>\
  </h3>\
</div>\
<div id='powerdiv' style='color:yellow;'>\
  <h3>\
  Current power: <span id='power'>0</span> W<br>\
  </h3>\
</div>\
<div id='battdiv' style='color:yellow;'>\
  <h3>\
  Battery: <span id='battery'>0</span> %<br>\
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
      var fields = this.responseText.split(';');\
      document.getElementById('power').innerHTML = fields[0];\
      document.getElementById('battery').innerHTML = fields[1];\
      document.getElementById('seq').innerHTML = fields[2];\
    }\
  };\
  xhttp.open('GET', 'handleDataHttp', true);\
  xhttp.send();\
}\
</script>\ 
<div id='info' style='color=white;'>\
<a href='https://github.com/bphermansson/EspSparsnasGateway'>Latest version</a>\
<!--\
<br><br><h5>Web code inspired by <a href='https://circuits4you.com'>Circuits4you.com</a><br>\
<a href='https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/'>ESP8266 (ajax) update part of web page without refreshing</a>\
</h5>-->\
</div>\
</body>\
</html>";
