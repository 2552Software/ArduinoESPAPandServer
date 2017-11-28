/* write to serial port */
'use strict';
const assert = require('assert');
const SerialPort = require('serialport');
const Buffer = require('safe-buffer').Buffer;
var fs = require('fs');
const writeData = Buffer.alloc(1000000, 1);
var buf1 = fs.readFileSync('blob.jpg');
// add parser -- 
// write this for testing https://github.com/node-serialport/node-serialport/blob/c65bfc310ed85b61b192d3224402ea93e06ae378/examples/mocking.js
function splitString (string, size) {
	var re = new RegExp('.{1,' + size + '}', 'g');
	return string.match(re);
}

// 1. no hand shaking at all? as far as I can tell this makes no difference, still stops at 72831 bytes, a buffer? what is the value?  
// 2. just send the same data over and over, 50k one tme time, does it matter? stops at 5759, what does 500k do? is there a pattern 34943, seems like data is just lost,
// 3. sleep before sending 10k
// 3 send data over and over, seems to go on an on but maybe data is still lost, we are just sending more and more. Seems to be true, would need data pattern
// 5 trying CTS one time packet of 50k, stopped at 15864
// try at lowest power level, set baud back to 115* 15487 same results, set rtscts on s/w and h/w, stop at 14601
// 9600 seems to keep things going (windows 10 issue?) see about the 1 time 50k buffer
// for fun, send 1M and time it
// try to echo back? or just save that for test time

var readStream = fs.createReadStream('blob.jpg', {highWaterMark:256});
console.log('b1 ' + buf1.length);

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

var totalbytes = buf1.length;
var sentbytes = 0;

function send() {
//    console.log('Welcome to My Console,');


// handsaking? control characeters? why is is stopping?
// send binary json? size/buffer so we know when to close it?
//for (var j = 0; j < buf1.length; ++j){
// send 256 bytes here, or whats left
// copy 256, or remaning, bytes to a local string
  //portW.write('hello', (err) => {
	//  if (err) { return console.log('Error: ', err.message) }
	  // subtract above from total
	  // console.log('message written');
	//});
// portW.drain((error) => {
  //console.log('Drain callback returned', error);
  // Now the data has "left the pipe" (tcdrain[1]/FlushFileBuffers[2] finished blocking).
  // [1] http://linux.die.net/man/3/tcdrain
  // [2] http://msdn.microsoft.com/en-us/library/windows/desktop/aa364439(v=vs.85).aportx
// });
//}
// set if there is any data left
setTimeout(send, 1000);

}


portW.on('open', () => {
  console.log('PortW Opened');
portW.write(writeData, () => {
  console.log('Write callback returned');
});

console.log('Calling drain');
portW.drain(() => {
  console.log('Drain callback returned');
});var buf1 = fs.readFileSync('blob.jpg');
setTimeout(send, 1000);
});
