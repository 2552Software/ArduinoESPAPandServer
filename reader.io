/* read from serial port */
'use strict';
const assert = require('assert');
const SerialPort = require('serialport');
var fs = require('fs');
var bytes = 0;
var toType = function(obj) {
  return ({}).toString.call(obj).match(/\s([a-zA-Z]+)/)[1].toLowerCase()
}
var options = { encoding: 'binary' };
var wstream = fs.createWriteStream('dm.jpg', options);

wstream.on('error', function (err) {
    console.log(err);
  });

wstream.on('open', function(fd) {
    console.log('open');

portR.on('data', (data) => {
console.log(toType(data));
var buf = Buffer.from(data.buffer);
wstream.write(data);
	/* get a buffer of data from the serial port */
	bytes += data.length;
	console.log(bytes + ' hot dog!' + data.length);
// search for FFD9 https://stackoverflow.com/questions/4585527/detect-eof-for-jpg-images for coorrect way, follow FFD9 with checksum? or follow with file size and chksum, good way to make sure, if 
// something wrong not sure 

	// assume EOF at end of buffer and SOF at start is reliable enough for now, read about link for more info on how to tighten this up
	if (data.length > 2 && data[data.length-1] == 0xD9 && data[data.length-2] == 0xFF){
		console.log('eof');
		wstream.end();
	}
	// 0xFF, 0xD8 start of image
	if (data.length > 2 && data[0] == 0xFF && data[1] == 0xD8){
		console.log('sof'); // start of file, once found no commands until eof found
	}
});

});

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
});


wstream.on('finish', function () {
 console.log('file has been written');
});

