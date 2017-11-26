var mqtt = require('mqtt')
var client = mqtt.connect({ port: 1883, host: '192.168.4.2', keepalive: 10000});
var SerialPort = require('serialport');
var port = new SerialPort('COM10', { baudRate: 115200});


// Open errors will be emitted as an error event

// Quit on any error todo figure this all out
port.on('error', (err) => {
  console.log('port error:');
  console.log(err.message);
  process.exit(1);
});

client.on('error', function (err) {
  console.log('client error:');
  console.log('error!' + err);
  client.end();
  client.stream.end();
  client = mqtt.connect({ port: 1883, host: '192.168.4.2', keepalive: 10000});
})

process.on('unhandledRejection', (reason, p) => {
  console.log('Unhandled Rejection at: Promise', p, 'reason:', reason);
  // application specific logging, throwing an error, or other logic here
});


client.on('reconnect', function () {
  console.log('reconnect to mqtt')
  client.subscribe('forward')
})

client.on('connect', function () {
  console.log('connect to mqtt')
  client.subscribe('forward')
})
