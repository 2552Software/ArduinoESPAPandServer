/* write to serial port */
'use strict';
const assert = require('assert');
const SerialPort = require('serialport');
var fs = require('fs');

// add parser -- 
// write this for testing https://github.com/node-serialport/node-serialport/blob/c65bfc310ed85b61b192d3224402ea93e06ae378/examples/mocking.js

//var buf = splitString(buf1.toString(), 256);
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
});

function inifi() {
	setTimeout(inifi, 1000);
}

function pad(pad, str, padLeft) {
  if (typeof str === 'undefined') 
    return pad;
  if (padLeft) {
    return (pad + str).slice(-pad.length);
  } else {
    return (str + pad).substring(0, pad.length);
  }
}

portW.on('open', () => {
  	console.log('PortW Opened');
	var buf1 = fs.readFileSync('blob.jpg');
	console.log(buf1.length.toString());
	// always send a fixed length size to start out
	var padded = pad("00000000", buf1.length, true);
	console.log(padded);
	portW.write(padded, () => {
	  console.log('Write callback padded returned');
	});
	portW.write(buf1, () => {
	  console.log('Write callback returned');
	});

	console.log('Calling drain');

	portW.drain(() => {
  	console.log('Drain callback returned');
	});
	setTimeout(inifi, 1000);
});
