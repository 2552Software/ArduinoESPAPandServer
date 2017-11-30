/* write to serial port */
'use strict';
const assert = require('assert');
const SerialPort = require('serialport');
var fs = require('fs');
var buf = fs.readFileSync('test.jpg');
// add parser -- ?

// test more when large data is sent
const defaultOptions = {
          baudRate: 19200, // try faster on non windows 10, 9600 works, 38400 breaks or is a slower, maybe bacause of more errors?, before trying buffer size changes,  19200 works
          parity: 'none',
          xon: false,
          xoff: false,
          xany: false,
          rtscts: true,
          dataBits: 8,
          stopBits: 1
        };

var portW = new SerialPort('COM7', defaultOptions, function (err) {
  if (err) {
    return console.log('7 Error: ', err.message);
  }
 portW.set({dtr: true, rts: true});  // this is the only line I added.
 console.log('7 open ');
});

// keep things around forever
function inifi() {
	console.log(buf.length);
// in range test mode
	portW.write("hi", () => {
	  console.log('Write callback returned');
	});

	console.log('Calling drain');

	portW.drain(() => {
  	console.log('Drain callback returned');
	});
	setTimeout(inifi, 100);
}

portW.on('open', () => {
  	console.log('PortW Opened');
	setTimeout(inifi, 1000);
});
