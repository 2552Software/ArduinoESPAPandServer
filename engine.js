var mqtt = require('mqtt')
var SerialPort = require('serialport');

//var client = mqtt.connect({ port: 1883, host: '192.168.4.2', keepalive: 10000});

var client = mqtt.connect()

client.subscribe('forward')
client.publish('forward', 'bin hier')

client.on('error', function (err) {
  console.log('client error:');
  console.log('error!' + err);
  client.end();
  client.stream.end();
  console.log('try to reconnect:');
  client = mqtt.connect()
})


client.on('reconnect', function () {
  console.log('reconnect to mqtt')
  client.subscribe('forward')
})

client.on('connect', function () {
  console.log('port120wx connect to mqtt')
  client.subscribe('forward')
})

client.on('message', function (topic, message) {
  // message is Buffer
  console.log("writeit port120wx");
  // send to xbee
  port120wx.write(message.toString(), (err) => {
   if (err) { return console.log('port120wx Error: ', err.message) }
    console.log('message written too port120wx');
   });
  client.end()
})



var port1 = new SerialPort('COM10', { baudRate: 115200}, function (err) {
  if (err) {
    return console.log('Error: ', err.message);
  }
});

var portcaptain = new SerialPort('COM6', { baudRate: 115200}, function (err) {
  if (err) {
    return console.log('Error: ', err.message);
  }
});


var port120wx = new SerialPort('COM7', { baudRate: 115200}, function (err) {
  if (err) {
    return console.log('Error: ', err.message);
  }
});

// Open errors will be emitted as an error event

// Quit on any error todo figure this all out
port1.on('error', (err) => {
  console.log('port 1 error:');
  console.log(err.message);
  process.exit(1);
});

portcaptain.on('error', (err) => {
  console.log('portcaptain error:');
  console.log(err.message);
  process.exit(1);
});

port120wx.on('error', (err) => {
  console.log('port120wx error:');
  console.log(err.message);
  process.exit(1);
});

// The open event is always emitted
port120wx.on('open', function() {
  // open logic
 console.log("port120wx open")
});

// The open event is always emitted
port1.on('open', function() {
  // open logic
 console.log("port1 open")
});

// The open event is always emitted
portcaptain.on('open', function() {
  // open logic
 console.log("portcaptain open")
});

// Switches the port into "flowing mode"
port120wx.on('data', function (data) {
  //console.log('port120wx Data:', data);
  //console.log(data.toString('ascii'));
  client.publish('forward', data.toString('ascii'))
});

// Switches the port into "flowing mode"
portcaptain.on('data', function (data) {
  console.log('portcaptain Data:', data);
//  console.log(data.toString('ascii'));
});


// Switches the port into "flowing mode"
port1.on('data', function (data) {
  //console.log('port 1 Data:', data);
  //console.log(data.toString('ascii'));
  client.publish('forward', data.toString('ascii'))
});


process.on('unhandledRejection', (reason, p) => {
  console.log('Unhandled Rejection at: Promise', p, 'reason:', reason);
  // application specific logging, throwing an error, or other logic here
});

