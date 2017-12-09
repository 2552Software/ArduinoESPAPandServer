//https://arduinojson.org/faq/esp32/

/* core camera, uses MQTT to send data in small chuncks.  
 *  need to figure license and copy right etc yet
 */
 // tested cam
#define OV2640_MINI_2MP
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
#include <TimeLib.h>
#include <esp_event.h>

#include "memorysaver.h"

#if !(defined ESP32 )
#error Please select the some sort of ESP32 board in the Tools/Board
#endif

// ip types vary by api expecations bugbug todo get all these in a class home
const char* ipServer = "192.168.88.100";// for now assume this, 100-200 are servers
WiFiClient espClient;
PubSubClient mqttClient(ipServer, 1883, espClient);
char blynk_token[33] = "YOUR_BLYNK_TOKEN";//todo bugbug

//Version 2,set GPIO0 as the slave select :

//https://github.com/thijse/Arduino-Log/blob/master/ArduinoLog.h
int logLevel = LOG_LEVEL_VERBOSE; // 0-LOG_LEVEL_VERBOSE, will be very slow unless 0 (none) or 1 (LOG_LEVEL_FATAL)

// this mod requires some arudcam stuff
//This can work on OV2640_MINI_2MP/OV5640_MINI_5MP_PLUS/OV5642_MINI_5MP_PLUS/OV5642_MINI_5MP_PLUS/
#if !(defined (OV2640_MINI_2MP)||defined (OV5640_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP) || defined (OV2640_CAM) || defined (OV5640_CAM) || defined (OV5642_CAM))
#error Please select the hardware platform and camera module in the ../libraries/ArduCAM/memorysaver.h file
#endif

void safestr(char *s1, const char *s2, int len) {
  if (s1 && s2 && len > 0){
    strncpy(s1, s2, len);
    s1[len-1] = '\0';
  }
}


const char* SSID = "SAM-Home"; // create a SSID of WIFI1 outside of this env. to use 3rd party wifi like mobile access point or such. If no such SSID is created we will create one here
const char* PWD = NULL; // never set here, set in the run time that stores config, todo bugbug make api to change

class Connections {
public:
  Connections(){isSoftAP = false;}
  void setup();
  void sendToMQTT(const char*topic, JsonObject& JSONencoder);
  static const int MAX_PWD_LEN = 16;
  static const int MAX_SSID_LEN = 16;
  void makeAP(char *ssid, char*pwd);
private:
  static void WiFiEvent(WiFiEvent_t event);
  bool isSoftAP;
  void connect(); // call inside sends so we can optimzie as needed
  uint8_t waitForResult(int connectTimeout);
} connections;

//bugbug if an AP is needed just use one $15 esp32, mixing AP with client kind of works but its not desired as one radio is shared and it leads to problems
void Connections::makeAP(char *ssid, char*pwd){
    Log.trace("makeAP %s %s", ssid, pwd);
    if (pwd && *pwd) {
      WiFi.softAP(ssid, pwd);
    }
    else {
      WiFi.softAP(ssid);
    }
    IPAddress ip = WiFi.softAPIP();
    Log.trace("softap IP %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    isSoftAP = true; //todo bugbug PI will need to keep trying to connect to this AP
}

void Connections::setup(){
     Log.trace(F("Connections::setup"));

    WiFi.onEvent(WiFiEvent);
    WiFi.mode(WIFI_STA); // any ESP can be an AP if main AP is not found.
    WiFi.setAutoReconnect(true); // let system auto reconnect for us
    connect();
  }

// saved configuration etc, can be set via mqtt, can be unique for each device. Must be set each time a new OS is installed as data is stored on the device for things like pwd so pwd is never 
// stored in open source
class State {
public:
  State() {   setDefault(); }
  void setup();
  void powerSleep(); // sleep when its ok to save power
  char ssid[Connections::MAX_SSID_LEN]; 
  char password[Connections::MAX_PWD_LEN];
  char other[16];
  uint32_t priority;
  int sleepTimeS;
  void set();
  void get();
  void setDefault();
  void echo();
  private:
  const char *defaultConfig = "/config.json";
} state;

void Connections::WiFiEvent(WiFiEvent_t event) {
  //https://github.com/espressif/arduino-esp32/blob/master/tools/sdk/include/esp32/esp_event.h
  Log.notice("[WiFi-event] event: %d" CR, event);
}

// breaks build when put in class as private, not sure but moving on...
// set GPIO16 as the slave select :
const int CS = 17;
#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
ArduCAM myCAM(OV2640, CS);
#elif defined (OV5640_MINI_5MP_PLUS) || defined (OV5640_CAM)
ArduCAM myCAM(OV5640, CS); //todo use the better camera in doors or in general? or when wifi? todo bugbug
#elif defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP)  ||(defined (OV5642_CAM))
ArduCAM myCAM(OV5642, CS);
#endif

class Camera {
public:
  void setup();
  void captureAndSend(const char * path);
  void turnOff(){
      Log.trace(F("cam off"));
      digitalWrite(CAM_POWER_ON, LOW);//camera power off
  }
  void turnOn(){
     Log.trace(F("cam on"));
     digitalWrite(CAM_POWER_ON, HIGH);
  }
private:
  static const uint8_t D10 = 4;
  const int CAM_POWER_ON = D10;
  void capture();
} camera;

void State::setup(){
  Log.notice(F("mounting file system"));
  // Next lines have to be done ONLY ONCE!!!!!When SPIFFS is formatted ONCE you can comment these lines out!!
  //Serial.println("Please wait 30 secs for SPIFFS to be formatted");
  //SPIFFS.format();
  //sleep(30*1000);
  
  if (SPIFFS.begin()) { // often used
    //clean FS, for testing
    //Serial.println("SPIFFS remove ...");
    //SPIFFS.remove(CONFIG_FILE);
    Log.notice(F("mounted file system bytes: %d/%d"), SPIFFS.usedBytes(), SPIFFS.totalBytes());
    get();
  }
  else {
     Log.error("no SPIFFS maybe you need to call SPIFFS.format one time");
  }

}

void State::powerSleep(){ // sleep when its ok to save power
    if (sleepTimeS) {
      Log.notice("power sleep for %d seconds", sleepTimeS);
      ESP.deepSleep(20e6); // 20e6 is 20 microseconds
      //ESP.deepSleep(sleepTimeS * 1000000);//ESP32 sleep 10s by default
      Log.notice("sleep over" CR);
    }
}

void State::echo(){
  uint64_t chipid = ESP.getEfuseMac();
  Log.notice(F("ESP32 Chip ID = %X:%X"),(uint16_t)(chipid>>32), (uint32_t)chipid);
  Log.notice(F("ssid: %s pwd %s: other: %s, sleep time in seconds %d"), ssid, password, other, sleepTimeS);
}

void State::setDefault(){
  Log.notice(F("set default State"));
  safestr(ssid, SSID, sizeof(ssid));
  safestr(password, PWD, sizeof(password)); // future phases can sync pwds or such
  safestr(other, "", sizeof(other));
  sleepTimeS=10;
  priority = 1; // each esp can have a start priority defined by initial sleep 
  echo();
}

// todo bugbug mqtt can send us these parameters too once we get security going
void State::set(){
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    File configFile = SPIFFS.open(defaultConfig, "w");
    if (!configFile) {
      Log.error("failed to open config file for writing");
    }
    else {
      json["ssid"] = ssid;
      json["pwd"] = password;
      json["other"] = other;
      json["priority"] = priority;
      json["sleepTimeS"] = sleepTimeS;
      Log.notice("config.json contents");
      echo();
      json.prettyPrintTo(Serial);
      json.printTo(configFile);
      configFile.close();
    }
}
void State::get(){
  //read configuration from FS json
  if (SPIFFS.exists(defaultConfig)) {
    //file exists, reading and loading
    Log.notice("reading config file");
    File configFile = SPIFFS.open(defaultConfig, "r");
    if (configFile) {
      Log.notice("opened config file");
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(1) + Connections::MAX_PWD_LEN);
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      json.printTo(Serial);
      if (json.success()) {
        Log.notice("parsed json A OK");
        safestr(ssid, json["ssid"], sizeof(ssid));
        safestr(password, json["pwd"], sizeof(password));
        safestr(other, json["other"], sizeof(other));
        priority = atoi(json["priority"]);
        sleepTimeS = json["sleepTimes"];
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
    state.setDefault();
  }

  echo();
}

// get from the camera
void Camera::capture(){
  int total_time = 0;

  Log.trace(F("start capture"));
  myCAM.flush_fifo(); // added this
  myCAM.clear_fifo_flag();
  myCAM.start_capture();

  total_time = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  total_time = millis() - total_time;

  Log.trace(F("capture total_time used (in miliseconds): %D"), total_time);
 }

// SPI must be setup
void Camera::setup(){
  uint8_t vid, pid;
  uint8_t temp;
  static int i = 0;

  // send many signs on help with debugging etc
  StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> JSONbuffer;
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["cmd"] = "setupCamera";

  //set the CS as an output:
  pinMode(CS,OUTPUT);
  pinMode(CAM_POWER_ON , OUTPUT);
  digitalWrite(CAM_POWER_ON, HIGH);

  while(1){
    //Check if the ArduCAM SPI bus is OK
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    temp = myCAM.read_reg(ARDUCHIP_TEST1);
    if(temp != 0x55){
      Log.error(F("SPI interface Error!"));
      delay(2);
      continue;
    }
    else {
      break;
    }
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
    Log.notice(F("OV5640 detected."));
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
    Log.notice(F("OV5642 detected."));
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

  connections.sendToMQTT("msg", JSONencoder);
  //delay(1000); set wifi after this call, is this enough time?

}

//send via mqtt, mqtt server will turn to B&W and see if there are changes ie motion, of so it will send it on
void Camera::captureAndSend(const char * path){
  byte buf[MQTT_MAX_PACKET_SIZE];
  int i = 0;
  uint8_t temp = 0, temp_last = 0;
  uint32_t length = 0;
  bool is_header = false;
  
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
  
  connections.sendToMQTT("cmd", JSONencoder);
  
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
        connections.sendToMQTT("cmd", JSONencoder);
        is_header = false;
        i = 0;
    }  
    // not sure about this one yet
    #define MQTT_HEADER_SIZE 16
    if (is_header)    { 
       //Write image data to buffer if not full
        if (i < MQTT_MAX_PACKET_SIZE-MQTT_HEADER_SIZE){
          buf[i++] = temp;
        }
        else        {
          //Write MQTT_MAX_PACKET_SIZE bytes image data
          myCAM.CS_HIGH();
          JSONencoder["cmd"] = "more";
          String s = String(i);
          for (int j = 0; j < i; ++j){
            s.setCharAt(j, buf[j]);
          }
          JSONencoder["buf"] = s;
          connections.sendToMQTT("cmd", JSONencoder);
  
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
void Connections::sendToMQTT(const char*topic, JsonObject& JSONencoder){
  char JSONmessageBuffer[MQTT_MAX_PACKET_SIZE];
  JSONencoder.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  JSONencoder["id"] = "getthis"; // bugbug todo make this a global etc
  JSONencoder["IAM"] = "cam1";
  JSONencoder["mqtt"] =  ipServer;
  Log.trace(F("sending message to MQTT, topic is %s ;" CR), topic);
  Log.trace(JSONmessageBuffer);
  Log.trace(CR);
  connect(); // quick connect check, connect, re-connect or do nothing to assure we are doing our best to stay in touch
  if (mqttClient.publish(topic, JSONmessageBuffer) != true) {
    Log.error("sending message to mqtt");
  }
}


// timeout connection attempt
uint8_t Connections::waitForResult(int connectTimeout) {
  if (connectTimeout == 0) {
    Log.trace(F("Waiting for connection result without timeout" CR));
    return WiFi.waitForConnectResult();
  } 
  else {
    Log.trace(F("Waiting for connection result with time out of %d" CR), connectTimeout);
    unsigned long start = millis();
    uint8_t status;
    while (1) {
      status = WiFi.status();
      https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/src/include/wl_definitions.h
      if (millis() > start + connectTimeout) {
        Log.trace(F("Connection timed out"));
        return WL_CONNECT_FAILED;
      }
      if (status == WL_CONNECTED){
        Log.notice(F("hot dog! we connected"));
        return WL_CONNECTED;
      }
      if (status == WL_CONNECT_FAILED) {
        Log.error(F("Connection failed"));
      }
      Log.trace(F("."));
      delay(200);
    }

    return status;
  }
}

// allow for reconnect, can be called often it needs to be fast
void Connections::connect(){
  //https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/src/include/wl_definitions.h
  if (!WiFi.isConnected()){
    Log.trace(F("WiFi.status() != WL_CONNECTED"));
    //check if we have ssid and pass and force those, if not, try with last saved values
    if (state.password[0] != '\0'){
     Log.notice("Connect to WiFi... %s %s", state.ssid, state.password);
     WiFi.begin(state.ssid, state.password);
    }
    else {
     Log.notice("Connect to WiFi... %s", state.ssid);
     WiFi.begin(state.ssid);
    }
    waitForResult(10000);
    if (!WiFi.isConnected()) {
      Log.error(F("something went horribly wrong" CR));
      return;
    }
    WiFi.setHostname("Station_Tester_02"); //bugbug todo make this unqiue and refelective and get from config
    Log.notice("Connected, host name is %s", WiFi.getHostname()); //bugbug todo make this unqiue and refelective
    Log.notice("RSSI: %d dBm", WiFi.RSSI());
    Log.notice("BSSID: %d", *WiFi.BSSID());
    Log.notice("LocalIP: %s", WiFi.localIP().toString().c_str());
  }  
  while (!mqttClient.connected()) { // add code to try different mqtt todo bugbug
    Log.trace("Connecting to MQTT...");
     if (mqttClient.connect(ipServer)) { //todo bugbug add user/pwd, store pwd local like others
      Log.notice("connected to %s (!)", ipServer);
     } else {
      //https://github.com/knolleary/pubsubclient/blob/master/src/PubSubClient.h
      Log.error(F("failed with state %d will retry every 2 seconds server %s"), mqttClient.state(), ipServer);
      delay(2000);
     }
  }
}

//todo bugbug move to a logging class and put a better time date stamp
void printTimestamp(Print* _logOutput) {
  char c[12];
  int m = sprintf(c, "%10lu ", millis());
  _logOutput->print(c);
}

void printNewline(Print* _logOutput) {
  // todo echo to mqtt if set up
  _logOutput->print('\n');
}
void setup(){
  Serial.begin(115200);

  Log.begin(logLevel, &Serial);
  Log.setPrefix(printTimestamp); 
  Log.setSuffix(printNewline); 
  Log.notice(F("ArduCAM Start!"));

  Wire.begin();
  SPI.begin();

  state.setup();
  camera.setup();

  // put all code we can below camera setup to give it time to setup
  
  // stagger the starts to make sure the highest priority is most likely to become AP etc
  if (state.priority != 1){
    delay(1000*state.priority);
  }
  connections.setup();
}

void loop(){
  char name[17];
  snprintf(name, 16, "%lu.jpg", now()); // unique number that means a ton
  camera.captureAndSend(name);

  // figure out sleep next bugbug to do
  /*
  camera.turnOff();
  state.powerSleep();
  camera.turnOn(); // do we need this? bugbug todo do we have to wait for start up here? 1 second? what?
  */
}






