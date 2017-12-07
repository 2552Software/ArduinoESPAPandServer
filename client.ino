//https://arduinojson.org/faq/esp32/

#define ARDUINOJSON_ENABLE_PROGMEM 0
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduCAM.h>
#include <PubSubClient.h>
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>

#include "memorysaver.h"

#if !(defined ESP32 )
#error Please select the some sort of ESP32 board in the Tools/Board
#endif

#define WIFI1 "SAM-Home" // look here first, its a different AP than ours something like a cell based wifi (passwords added soon)
#define WIFI1PWD ""
#define WIFI2 "SAMIAM" // working access point
#define MAX_PWD_LEN 16
#define MQTT_SERVER "SAMMQTT"
#define MQTT_PORT "1883"
#define CONFIG_FILE "/config.json"

const char* ssid = WIFI1; // default
const char* password = WIFI1PWD;
static const uint8_t D10 = 4;
const int CAM_POWER_ON = D10;
// set GPIO16 as the slave select :
const int CS = 17;
//Version 2,set GPIO0 as the slave select :

byte buf[256];
static int i = 0;
static int k = 0;
uint8_t temp = 0, temp_last = 0;
uint32_t length = 0;
bool is_header = false;

char blynk_token[33] = "YOUR_BLYNK_TOKEN";//todo bugbug


// ip types vary by api expecations
char* ipServer = "192.168.88.10";// for now assume this, 2-9 reserved, 10-100 is a server, 101-250 are devices, 251+ reserved
IPAddress ipMe(192,168,88,101);// for now assume this, 2-9 reserved, 10-100 is a server, 101-250 are devices, 251+ reserved
IPAddress ipSubnet(255,255,255,0);
WiFiClient espClient;
PubSubClient mqttClient(ipServer, 1883, espClient);

//https://github.com/thijse/Arduino-Log/blob/master/ArduinoLog.h
int logLevel = LOG_LEVEL_VERBOSE; // 0-LOG_LEVEL_VERBOSE, will be very slow unless 0 (none) or 1 (LOG_LEVEL_FATAL)

// this mod requires some arudcam stuff
//This can work on OV2640_MINI_2MP/OV5640_MINI_5MP_PLUS/OV5642_MINI_5MP_PLUS/OV5642_MINI_5MP_PLUS/
#if !(defined (OV2640_MINI_2MP)||defined (OV5640_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP) || defined (OV2640_CAM) || defined (OV5640_CAM) || defined (OV5642_CAM))
#error Please select the hardware platform and camera module in the ../libraries/ArduCAM/memorysaver.h file
#endif

#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
ArduCAM myCAM(OV2640, CS);
#elif defined (OV5640_MINI_5MP_PLUS) || defined (OV5640_CAM)
ArduCAM myCAM(OV5640, CS); //todo use the better camera in doors or in general? or when wifi? todo bugbug
#elif defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP)  ||(defined (OV5642_CAM))
ArduCAM myCAM(OV5642, CS);
#endif

void safestr(char *s1, const char *s2, int len) {
  if (s1 && s2 && len > 0){
    strncpy(s1, s2, len);
    s1[len-1] = '\0';
  }
}

// saved configuration, unique for each device. Must be set each time a new OS is installed as data is stored on the device
class Conf {
public:
  Conf() {
      chipid = ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
      wifi1[0] = '\0';
      wifi2[0] = '\0';
      password1[0] = '\0';
      password2[0] = '\0';
      other[0] = '\0';
      priority = 1;
      sleepTimeS = 10; // 10 seconds sleep between loops
  }
  char wifi1[16]; // check here first
  char wifi2[16];
  char password1[MAX_PWD_LEN];
  char password2[MAX_PWD_LEN];
  char other[16];
  uint64_t chipid;  // read at setup time
  uint32_t priority;
  int sleepTimeS;
  void set();
  void get();
  void setDefault();
  void echo();
} conf;

void Conf::echo(){
  Log.notice(F("ESP32 Chip ID = %04X"),(uint16_t)(chipid>>32));//print High 2 bytes
  Log.notice(F("%08X"),(uint32_t)chipid);//print High 2 bytes
  Log.notice(F("wifi1: %s wifi2: %s password1: %s password2: %s other: %s"), wifi1, wifi2, password1, password2, other);
}

void Conf::setDefault(){
  Log.notice(F("set defaults"));
  safestr(wifi1, WIFI1, sizeof(wifi1));
  safestr(wifi2, WIFI2, sizeof(wifi2));
  safestr(password1, "", sizeof(password1));
  safestr(password2, "", sizeof(password2)); // future phases can sync pwds or such
  safestr(other, "", sizeof(other));
  sleepTimeS=10;
  echo();
}

// todo bugbug mqtt can send us these parameters too once we get security going
void Conf::set(){
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    File configFile = SPIFFS.open(CONFIG_FILE, "w");
    if (!configFile) {
      Log.error("failed to open config file for writing");
    }
    else {
      json["wifi1"] = wifi1;
      json["wifi2"] = wifi2;
      json["pwd1"] = password1;
      json["pwd2"] = password2;
      json["other"] = other;
      json["priority"] = priority;
      json["sleepTimeSity"] = sleepTimeS;
      Log.notice("config.json contents");
      echo();
      json.prettyPrintTo(Serial);
      json.printTo(configFile);
      configFile.close();
    }
}
void Conf::get(){
  //read configuration from FS json
  if (SPIFFS.exists("/config.json")) {
    //file exists, reading and loading
    Log.notice("reading config file");
    File configFile = SPIFFS.open(CONFIG_FILE, "r");
    if (configFile) {
      Log.notice("opened config file");
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(1) + MAX_PWD_LEN);
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      json.printTo(Serial);
      if (json.success()) {
        Log.notice("parsed json A OK");
        safestr(wifi1, json["wifi1"], sizeof(wifi1));
        safestr(wifi2, json["wifi2"], sizeof(wifi2));
        safestr(password1, json["password1"], sizeof(password1));
        safestr(password2, json["password2"], sizeof(password2));
        safestr(other, json["other"], sizeof(other));
        priority = atoi(json["priority"]);
        sleepTimeS = json["sleepTimeSity"];
        echo();
      } 
      else {
        Log.error("failed to load json config");
      }
    }
    else {
      Log.error("internal error");
    }
  }
  else {
    Log.notice("no config file, set defaults");
    conf.setDefault();
  }

  echo();

}

// get from the camera
void capture(){
  int total_time = 0;

  Log.notice(F("start capture"));
  myCAM.flush_fifo(); // added this
  myCAM.clear_fifo_flag();
  myCAM.start_capture();

  total_time = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  total_time = millis() - total_time;

  Log.notice(F("capture total_time used (in miliseconds): %D"), total_time);

 }

//send via mqtt, mqtt server will turn to B&W and see if there are changes ie motion, of so it will send it on
void captureAndSend(const char * path){
  capture();
  
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["device"] = "ESP32";//give it a unique name
  JSONencoder["type"] = "camera";
  
  length = myCAM.read_fifo_length();
  
  // send MQTT start xfer message
  JSONencoder["cmd"] = "newjpg";
  JSONencoder["path"] = path;
  JSONencoder["len"] = length;
  
  if (length >= MAX_FIFO_SIZE) {//8M
    JSONencoder["error"] = "Over size";
  }
  if (length == 0 )   {
    JSONencoder["error"] = "Size is 0";
  }
  
  send("cmd", JSONencoder);
  
  i = 0;
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  while ( length-- )  {
    temp_last = temp;
    temp =  SPI.transfer(0x00);
    //Read JPEG data from FIFO until EOF, send send the image
    if ( (temp == 0xD9) && (temp_last == 0xFF) ){ //If find the end ,break while,
        buf[i++] = temp;  //save the last  0XD9     
       //Write the remain bytes in the buffer
       // #define MQTT_MAX_PACKET_SIZE 128 so we are ok
       // todo get the save code from before, then use that to store CAMID (already there I think) and use that as part of xfer
        myCAM.CS_HIGH();
        // send buffer via mqtt here  
        JSONencoder["cmd"] = "final";
        String s = String(i);
        for (int j = 0; j < i; ++j){
          s.setCharAt(j, buf[j]);
        }
        JSONencoder["buf"] = s;
        send("cmd", JSONencoder);
        is_header = false;
        i = 0;
    }  
    if (is_header)    { 
       //Write image data to buffer if not full
        if (i < MQTT_MAX_PACKET_SIZE){
          buf[i++] = temp;
        }
        else        {
          //Write MQTT_MAX_PACKET_SIZE bytes image data
          myCAM.CS_HIGH();
          //file.write(buf, 256);
          JSONencoder["cmd"] = "more";
          String s = String(i);
          for (int j = 0; j < i; ++j){
            s.setCharAt(j, buf[j]);
          }
          JSONencoder["buf"] = s;
          send("cmd", JSONencoder);
  
          i = 0;
          buf[i++] = temp;
          myCAM.CS_LOW();
          myCAM.set_fifo_burst();
        }        
    }
    else if ((temp == 0xD8) & (temp_last == 0xFF)) { // start of file
      is_header = true;
      buf[i++] = temp_last;
      buf[i++] = temp;   
    } 
  } 
}

// send to mqtt
void send(const char*topic, JsonObject& JSONencoder){
  char JSONmessageBuffer[MQTT_MAX_PACKET_SIZE];
  JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  JSONencoder["id"] = "getthis"; // bugbug todo make this a global etc
  JSONencoder["ipMe"] = ipMe.toString();
  JSONencoder["IAM"] = "cam1";
  JSONencoder["mqtt"] =  ipServer;
  Log.trace(F("ending message to MQTT, topic is %s ;"), topic);
  Log.trace(JSONmessageBuffer);
  if (mqttClient.publish(topic, JSONmessageBuffer) != true) {
    Log.error("sending message to mqtt");
  }
}

void setup(){
  uint8_t vid, pid;
  uint8_t temp;
  static int i = 0;
  Serial.begin(115200);

  Log.begin(logLevel, &Serial);
  Log.notice(F("ArduCAM Start!"));
  
  //set the CS as an output:
  pinMode(CS,OUTPUT);
  pinMode(CAM_POWER_ON , OUTPUT);
  digitalWrite(CAM_POWER_ON, HIGH);
  
  // make sure we can connect
  WiFi.config(ipMe, ipMe, ipSubnet); // each device gets is own ip so we can double check things etc
  WiFi.begin(ssid, password); // add in the save code todo bugbug
 
  while (WiFi.status() != WL_CONNECTED) { // bugbug todo add code to test for different wifi
    delay(500);
    Log.trace("Connecting to WiFi..");
  }
  Log.trace("Connected to the WiFi network"); 
  while (!mqttClient.connected()) { // add code to try different mqtt todo bugbug
    Log.trace("Connecting to MQTT...");
     if (mqttClient.connect(ipServer)) { //todo bugbug add user/pwd, store pwd local like others
      Log.trace("connected");
     } else {
      Log.error(F("failed with state %D will retry every 2 seconds"), mqttClient.state());
      delay(2000);
     }
  }

  // send many signs on help with debugging etc
  StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> JSONbuffer;
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["cmd"] = "signon";
  JSONencoder["wifi"] = ssid;

  Wire.begin();
  //initialize SPI:
  SPI.begin();
  while(1){
    //Check if the ArduCAM SPI bus is OK
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    temp = myCAM.read_reg(ARDUCHIP_TEST1);
    if(temp != 0x55){
      Log.error(F("SPI interface Error!"));
      delay(2);
      continue;
    }
    else
      break;
    } 
   
#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  //Check if the camera module type is OV2640
  myCAM.wrSensorReg8_8(0xff, 0x01);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))){
    JSONencoder["error"] = "Can't find OV2640 module!";
  }
  else {
    Log.trace(F("OV2640 detected."));
  }
#elif defined (OV5640_MINI_5MP_PLUS) || defined (OV5640_CAM)
  //Check if the camera module type is OV5640
  myCAM.wrSensorReg16_8(0xff, 0x01);
  myCAM.rdSensorReg16_8(OV5640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg16_8(OV5640_CHIPID_LOW, &pid);
  if((vid != 0x56) || (pid != 0x40)){
    JSONencoder["error"] = "Can't find OV5640 module!";
  }
  else {
    Log.trace(F("OV5640 detected."));
  }
#elif defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP) || (defined (OV5642_CAM))
 //Check if the camera module type is OV5642
  myCAM.wrSensorReg16_8(0xff, 0x01);
  myCAM.rdSensorReg16_8(OV5642_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg16_8(OV5642_CHIPID_LOW, &pid);
   if((vid != 0x56) || (pid != 0x42)){
       JSONencoder["error"] = "Can't find OV5642 module!";
   }
   else {
    Log.trace(F("OV5642 detected."));
   }
#endif
 
  //Change to JPEG capture mode and initialize the camera module
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  
#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
    myCAM.OV2640_set_JPEG_size(OV2640_320x240);
    JSONencoder["camType"] = "2640, 320x240";
#elif defined (OV5640_MINI_5MP_PLUS) || defined (OV5640_CAM)
    myCAM.write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);   //VSYNC is active HIGH
    myCAM.OV5640_set_JPEG_size(OV5640_320x240);
    JSONencoder["camType"];
#elif defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP) ||(defined (OV5642_CAM))
    myCAM.write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);   //VSYNC is active HIGH
    myCAM.OV5642_set_JPEG_size(OV5642_320x240);  
    JSONencoder["camType"] = "5640, 320x240";
#endif

  send("msg", JSONencoder);
  delay(1000);
}
void loop(){
  char name[11];
 sprintf(name,"/%05d.jpg",k);
 captureAndSend(name);
 digitalWrite(CAM_POWER_ON, LOW);//camera power off
 if (conf.sleepTimeS) {
   ESP.deepSleep(conf.sleepTimeS * 1000000);//ESP32 sleep 10s by default
 }
}






