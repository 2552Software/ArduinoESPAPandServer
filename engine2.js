'use strict'

var mqtt = require('mqtt')
var fs = require('fs');
var client = mqtt.connect({ port: 1883, host: '192.168.88.100', keepalive: 10000});
var cam1 = "none"
var wstream = fs.createWriteStream(cam1);

client.on('connect', function () {
  console.log('connect');
  client.subscribe('dataready')
  client.subscribe('datafinal')
  client.subscribe('trace')
  client.subscribe('error')
})

client.on('message', function (topic, message) {
 console.log(topic);
  if (topic === 'trace'){
	 console.log('trace')
	 var jsonContent = JSON.parse(message);
	 console.log(jsonContent);
  }
  else if (topic === 'error'){
	 console.log('error')
	 var jsonContent = JSON.parse(message);
	 console.log('ERROR! ' + jsonContent);
  }
  else if (topic === 'dataready'){
	 var jsonContent = JSON.parse(message);
	 cam1 = jsonContent["path"];
	 console.log('sof ' + cam1);
	 client.subscribe(cam1)
	 wstream = fs.createWriteStream(cam1);
	 console.log(jsonContent)
  }
  else if (topic === 'datafinal'){
	 var jsonContent = JSON.parse(message);
	 cam1 = jsonContent["path"];
	 console.log('eof ' + cam1);
	 wstream.end();
  }
  else if (topic === cam1){
	 console.log('DATA');
	 console.log(message.length);
	 wstream.write(message);
  }
})

client.on('error', function(err) {
    console.log(err);
});

(function wait () {
   if (1) setTimeout(wait, 1000);
})();
