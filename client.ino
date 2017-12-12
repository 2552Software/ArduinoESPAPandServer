//https://arduinojson.org/faq/esp32/

/* core camera, uses MQTT to send data in small chuncks.  
 *  need to figure license and copy right etc yet
 */
 // tested cam
#define OV2640_MINI_2MP 
#define ARDUINOJSON_ENABLE_PROGMEM 0
#define MQTT_MAX_PACKET_SIZE 1024
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduCAM.h>
#include <PubSubClient.h>
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>
#include <TimeLib.h> //todo bugbug just use static count to name files and drop this, or get it to do dates nicely
#include <esp_event.h>
#include "memorysaver.h" 

#if !(defined ESP32 )
#error Please select the some sort of ESP32 board in the Tools/Board
#endif


const int MAX_PWD_LEN = 16;//todo make lower case
const int MAX_SSID_LEN = 16;
const char* SSID = "SAM-Home"; // create a SSID of WIFI1 outside of this env. to use 3rd party wifi like mobile access point or such. If no such SSID is created we will create one here
const char* PWD = NULL; // never set here, set in the run time that stores config, todo bugbug make api to change

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

// saved configuration etc, can be set via mqtt, can be unique for each device. Must be set each time a new OS is installed as data is stored on the device for things like pwd so pwd is never 
// stored in open source
class State {
public:
  State() {   setDefault(); }
  void setup();
  void powerSleep(); // sleep when its ok to save power
  char ssid[MAX_SSID_LEN]; 
  char password[MAX_PWD_LEN];
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


class Connections {
public:
  Connections(){isSoftAP = false;}
  void setup();
  void loop();
  bool sendToMQTT(const char*topic, JsonObject& root);
  bool sendToMQTT(const char*topic, const uint8_t * payload, unsigned int plength);
  static const int MQTTport = 1883;
  // ip types vary by api expecations bugbug todo get all these in a class home
  const char* ipServer = "192.168.88.100";// for now assume this, 100-200 are servers
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  //char blynk_token[33] = "YOUR_BLYNK_TOKEN";//todo bugbug
  void makeAP(char *ssid, char*pwd);
  void connectMQTT();
private:
  static void WiFiEvent(WiFiEvent_t event);
  bool isSoftAP;
  void connect(); // call inside sends so we can optimzie as needed
  uint8_t waitForResult(int connectTimeout);
  static void  input(char* topic, byte* payload, unsigned int length);
} connections;

void Connections::loop(){
  if (!wifiClient.connected()) {
    connect();
  }
  mqttClient.loop();

}
// send to mqtt, binary is only partily echoed
bool Connections::sendToMQTT(const char*topic, JsonObject& root){
  String output;
  root.printTo(output);
  Log.trace(F("sending message to MQTT, topic is %s ;"), topic);
  Log.trace(output.c_str());
  connect(); // do as much as we can to work with bad networks
  if (!mqttClient.publish(topic, output.c_str())) {
    Log.error("sending message to mqtt");
    return false;
  }
  Log.trace("sent message to mqtt");
  return true;
}
boolean Connections::sendToMQTT(const char* topic, const uint8_t * payload, unsigned int length){
  Log.trace(F("sending binary message to MQTT, topic is %s size is %d"), topic, length);
  connect(); // do as much as we can to work with bad networks
  if (!mqttClient.publish(topic, payload, length)) {
    Log.error("sending binary message to mqtt");
    return false;
  }
  Log.trace("sent binary message to mqtt just fine");
  return true;
  
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
      Log.error(F("something went horribly wrong"));
      return;
    }
  }  
  connectMQTT();
}

// reconnect to mqtt
void Connections::connectMQTT() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
   Log.trace("Connecting to MQTT... %s", ipServer);
    // Create a random client ID bugbug todo clean this up, use process id etc
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str())) {
      Log.trace("connected");
      // Once connected, publish an announcement...
      mqttClient.publish("outTopic", "hello world");
      // ... and resubscribe
      mqttClient.subscribe("inTopic");
    } else {
      Log.notice("failed, rc=%d try again in 5 seconds", mqttClient.state());
      delay(5000);
    }
  }

}
void  Connections::input(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  /* example Switch on the LED if an 1 was received as first character
   *  we would set a config item like turn camera on etc maybe reboot, maybe set in the .config file
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
  */
}

//bugbug if an AP is needed just use one $10 esp32, mixing AP with client kind of works but its not desired as one radio is shared and it leads to problems
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
  Log.trace(F("Connections::setup, server %s, port %d"), ipServer, MQTTport);
  //put a copy in here when ready blynk_token[33] = "YOUR_BLYNK_TOKEN";//todo bugbug
  mqttClient.setServer(ipServer, MQTTport);
  mqttClient.setCallback(input);
  mqttClient.setClient(wifiClient);
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA); // any ESP can be an AP if main AP is not found.
  WiFi.setAutoReconnect(true); // let system auto reconnect for us
}


void Connections::WiFiEvent(WiFiEvent_t event) {
  //https://github.com/espressif/arduino-esp32/blob/master/tools/sdk/include/esp32/esp_event.h
  switch (event){
  case SYSTEM_EVENT_STA_START:
    Log.notice("[WiFi-event] SYSTEM_EVENT_STA_START");
    break;
  case SYSTEM_EVENT_STA_CONNECTED:
    Log.notice("[WiFi-event] SYSTEM_EVENT_STA_CONNECTED");
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    Log.notice("[WiFi-event] SYSTEM_EVENT_STA_GOT_IP");
    WiFi.setHostname("Station_Tester_02"); //bugbug todo make this unqiue and refelective and get from config
    Log.notice("Connected, host name is %s", WiFi.getHostname()); //bugbug todo make this unqiue and refelective
    Log.notice("RSSI: %d dBm", WiFi.RSSI());
    Log.notice("BSSID: %d", *WiFi.BSSID());
    Log.notice("LocalIP: %s", WiFi.localIP().toString().c_str());
    break;
  default:
    Log.notice("[WiFi-event] event: %d", event);
  }
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
  bool findCamera();
  void initCam();
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
      // Allocate a buffer to store contents of the file. crash if no memory
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer(JSON_OBJECT_SIZE(1) + MAX_PWD_LEN);
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
 
bool  Camera::findCamera(){
  uint8_t vid, pid;

#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  //Check if the camera module type is OV2640
  myCAM.wrSensorReg8_8(0xff, 0x01);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))){
    Log.trace(F("OV2640 not detected"));
  }
  else {
    Log.notice(F("OV2640 detected"));
    return true;
  }
#elif defined (OV5640_MINI_5MP_PLUS) || defined (OV5640_CAM)
  //Check if the camera module type is OV5640
  myCAM.wrSensorReg16_8(0xff, 0x01);
  myCAM.rdSensorReg16_8(OV5640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg16_8(OV5640_CHIPID_LOW, &pid);
  if((vid != 0x56) || (pid != 0x40)){
     Log.trace(F("OV5640 not detected"));
  }
  else {
    Log.notice(F("OV5640 detected"));
    return true;
  }
#elif defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP) || (defined (OV5642_CAM))
 //Check if the camera module type is OV5642
  myCAM.wrSensorReg16_8(0xff, 0x01);
  myCAM.rdSensorReg16_8(OV5642_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg16_8(OV5642_CHIPID_LOW, &pid);
   if((vid != 0x56) || (pid != 0x42)){
     Log.trace(F("OV5642 not detected"));
   }
   else {
    Log.notice(F("OV5642 detected"));
    return true;
   }
#endif
  Log.notice(F("no cam detected"));
  return false;// no cam
}
void Camera::initCam(){
  Log.trace(F("init camera"));
  //Change to JPEG capture mode and initialize the camera module
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  //bugbug todo allow mqtt based set of these, then a restart if needed, store in config file
#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
    myCAM.OV2640_set_JPEG_size(OV2640_800x600); //OV2640_320x240 OV2640_800x600 OV2640_640x480 OV2640_1024x768 OV2640_1600x1200
    Log.notice(F("init OV2640_800x600"));
#elif defined (OV5640_MINI_5MP_PLUS) || defined (OV5640_CAM)
    myCAM.write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);   //VSYNC is active HIGH
    myCAM.OV5640_set_JPEG_size(OV5640_320x240);
    Log.notice(F("init OV5640_320x240"));
#elif defined (OV5642_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP) ||(defined (OV5642_CAM))
    myCAM.write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);   //VSYNC is active HIGH
    myCAM.OV5642_set_JPEG_size(OV5642_320x240);  
    Log.notice(F("init OV5642_320x240"));
#endif

}
// SPI must be setup
void Camera::setup(){
  //set the CS as an output:
  pinMode(CS,OUTPUT);
  pinMode(CAM_POWER_ON , OUTPUT);
  digitalWrite(CAM_POWER_ON, HIGH);

  while(1){
    //Check if the ArduCAM SPI bus is OK
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    if(myCAM.read_reg(ARDUCHIP_TEST1) != 0x55){
      Log.error(F("SPI interface Error!"));
      delay(2);
      continue;
    }
    else {
      Log.trace(F("SPI interface to camera found"));
      break;
    }
  } 
  // need to check in a loop at least for a bit
  while(1){
    Log.trace(F("try to find camera"));
    if (findCamera()) {
      break;
    }
    delay(1000);
  }

  initCam();
  delay(1000); // let things setup
}

//send via mqtt, mqtt server will turn to B&W and see if there are changes ie motion, of so it will send it on
void Camera::captureAndSend(const char * path){
  // not sure about this one yet
  #define MQTT_HEADER_SIZE 16

  uint32_t length = 0;
  bool is_header = false;
  
  capture();

  //OV2640_320x240 is 6149 currently at least, not too bad, 640x480 is 15365, still not too bad, 800x600 16389, OV2640_1024x768 is 32773, getting too large? will vary based on image
  // 71685 for OV2640_1600x1200. Next see how each mode looks and how fast OV2640_1600x1200 can be sent. All data sizes around power of 2 with some padding. is this consistant?
  length = myCAM.read_fifo_length();
  
  if (length >= MAX_FIFO_SIZE) {
     Log.error(F("len %d, MAX_FIFO_SIZE is %d, ignore"), length, MAX_FIFO_SIZE);
     return;
  }
  if (length == 0 )   {
     Log.error(F("len is 0 ignore"));
     return;
  }

  // will send file in parts
  std::unique_ptr<uint8_t[]> buf(new uint8_t[MQTT_MAX_PACKET_SIZE+MQTT_HEADER_SIZE]);
  if (buf == nullptr){
     Log.error(F("mem 1 size %d"), MQTT_MAX_PACKET_SIZE+MQTT_HEADER_SIZE);
     return;
  }
  // let reader know we are coming so they can start saving data
  DynamicJsonBuffer jsonBuffer;
  JsonObject& JSONencoder = jsonBuffer.createObject();
  JSONencoder["device"] = "ESP32";//give it a unique name bugbug todo
  JSONencoder["type"] = "camera";
  JSONencoder["cmd"] = "startjpg";
  JSONencoder["path"] = path; // also name of topic with data
  connections.sendToMQTT("dataready", JSONencoder);

  // goto this next https://github.com/Links2004/arduinoWebSockets
  uint8_t temp = 0, temp_last = 0;
  unsigned int i = 0; // current index
  unsigned int total = 0; // total bytes sent
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  while ( length-- )  {
    temp_last = temp;
    temp =  SPI.transfer(0x00);
    //Read JPEG data from FIFO until EOF, send send the image.  Size likely to be smaller than buffer
    if ( (temp == 0xD9) && (temp_last == 0xFF) ){ //If find the end ,break while,
        buf[i++] = temp;  //save the last  0XD9     
       //Write the remain bytes in the buffer
       // todo get the save code from before, then use that to store CAMID (already there I think) and use that as part of xfer
        myCAM.CS_HIGH();
        // send buffer via mqtt here  
        total += i+1;
        connections.sendToMQTT(path, buf.get(), i+1); // will need to namic topic like "Cam1" kind of things once working bugbug
        // now send notice
        // send MQTT start xfer message
        DynamicJsonBuffer jsonBuffer;
        JsonObject& JSONencoder = jsonBuffer.createObject();
        JSONencoder["device"] = "ESP32";//give it a unique name bugbug todo
        JSONencoder["type"] = "camera";
        JSONencoder["cmd"] = "newjpg";
        JSONencoder["path"] = path; // also name of topic with data
        JSONencoder["len"] = total; // server can compare len with actual data lenght to make sure data was not lost
        connections.sendToMQTT("datafinal", JSONencoder);
        is_header = false;
        i = 0;
    }  
    if (is_header)    { 
       //Write image data to buffer if not full
        if (i < MQTT_MAX_PACKET_SIZE-MQTT_HEADER_SIZE){
          buf[i++] = temp;
        }
        else        {
          //Write MQTT_MAX_PACKET_SIZE bytes image data
          myCAM.CS_HIGH();
          total += i+1;
          connections.sendToMQTT(path, buf.get(), i+1);
          i = 0; // back to start of buffer
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
  connections.setup();
  camera.setup();

  // put all other code we can below camera setup to give it time to setup
  
  // stagger the starts to make sure the highest priority is most likely to become AP etc
  if (state.priority != 1){
    delay(1000*state.priority);
  }
}

void loop(){
  connections.loop();

  char name[17];
  snprintf(name, 16, "CAM1%lu.jpg", now()); // just use incrementor and unique name/type bugbug todo
  camera.captureAndSend(name);

  // figure out sleep next bugbug to do
  /*
  camera.turnOff();
  state.powerSleep();
  camera.turnOn(); // do we need this? bugbug todo do we have to wait for start up here? 1 second? what?
  */
}






