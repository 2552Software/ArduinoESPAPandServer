/* read from serial port */
'use strict';
const assert = require('assert');
const SerialPort = require('serialport');
var fs = require('fs');
// see if we can read at max baud
// test more when large data is sent
const defaultOptions = {
          baudRate: 230400,
          parity: 'none',
          xon: false,
          xoff: false,
          xany: false,
          rtscts: true,
          dataBits: 8,
          stopBits: 1,
	  highWaterMark:64*1024
        };

// flush on open
var portR = new SerialPort('COM6', defaultOptions, function (err) {
  if (err) {
    return console.log('6 Error: ', err.message);
  }
 portR.set({dtr: true, rts: true});  // this is the only line I added.
});

portR.on('open', () => {
	console.log('PortR Opened');
	portR.flush();
});

var bytes = 0;

portR.on('data', (data) => {
	/* get a buffer of data from the serial port */
	bytes += data.length;
	console.log(bytes + ' hot dog!' + data.length);
	//wstream.write(data);
	//if (toRead < 0){
	//	wstream.end();
	//}
	//console.log(data.toString());
});



//readStream.setEncoding('utf8');
//readStream.on('data', (chunk) => {
//  assert.equal(typeof chunk, 'string');
//  toRead  += chunk.length;
 // console.log(chunk.length + ' got characters of string data, to read ' + toRead);
//   portW.write(chunk, (err) => {
//	  if (err) { return console.log('Error: ', err.message) }
	  //console.log('message written');
//	});
//});

//readStream.on('end', function () {
 //    console.log('stream end');
 //});
 
//wstream.on('finish', function () {
 // console.log('file has been written');
//});

