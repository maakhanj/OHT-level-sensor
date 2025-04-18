
#include <TZ.h>
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <time.h>
#include <PolledTimeout.h>
#include "config.h"      
#include <FS.h>           
#include <LittleFS.h>     
#include "builtinfiles.h" 
#include <Wire.h>
#include "HTTPSRedirect.h"
#include <ArduinoJson.h>


class SettingsHandler : public RequestHandler {
public:
  SettingsHandler(ESP8266WebServer &serverInstance, DynamicJsonDocument &set1Instance):server(serverInstance),set1(set1Instance){}
  bool canHandle(HTTPMethod method, const String &uri) override {
    return uri.startsWith("/settings")|| uri.startsWith("/login");
  }
  bool handle(ESP8266WebServer &server, HTTPMethod requestMethod, const String &uri) override {
    if (uri.startsWith("/login")) {
      handleLogin();
      return true;
    }
    if (!is_authenticated()) {
      server.send(401, "text/plain", "not_authenticated");
      return true;
    }
    File file;
    String subRoute;
    subRoute = uri.substring(9);
    String req = requestMethod==HTTP_GET ? "GET" : "POST";
    Serial.println("SubRoute: " + subRoute);
    Serial.println("Request Method: " + req);
    Serial.println("Request uri: " + uri);
    if (subRoute == "" && requestMethod == HTTP_GET){
      file = LittleFS.open("/set1.htm", "r");
      server.streamFile(file, "text/html");
      file.close();
      return true;
    }
    if (!subRoute.startsWith("/set1") && !subRoute.startsWith("/set2")) {
      return false;
    }
    if (requestMethod == HTTP_GET) {   
      String fname2=subRoute+String(".json");
      file = LittleFS.open(fname2, "r");
      if (!file) {
        //server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
        server.send(200);
        return true;
      } 
      server.streamFile(file, "application/json");
      file.seek(0);
      Serial.print("File read: ");
      Serial.println(fname2);
      Serial.print("Json data: ");
      Serial.println(file.readString());
      file.close();
    } else if(requestMethod == HTTP_POST) {      
      String jsonData = server.arg("plain");
      String fname = subRoute+String(".json");
      Serial.println("Received JSON Data: " + jsonData);
      file = LittleFS.open(fname, "w");
      if (!file) {
        server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
        return true;
      }
      file.print(jsonData);
      file.close();
      String setFile = (subRoute == "/set1") ? "/set2.htm" : "/set1.htm";
      file = LittleFS.open(setFile, "r");
      if (!file) {
        server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
        return true;
      }
      server.streamFile(file, "text/html");
      file.close();
    }
    return true;
  }
  private:
  ESP8266WebServer &server;
  DynamicJsonDocument &set1;
  bool is_authenticated(){
    String cookie = server.header("Cookie");
    return (cookie.indexOf("ESPSESSIONID=1")!= -1 ? true: false);
  }
  void handleLogin(){
  if (server.hasArg("DISCONNECT")){
    server.sendHeader("Location","/login");
    server.sendHeader("Cache-Control","no-cache");
    server.sendHeader("Set-Cookie","ESPSESSIONID=0");
    server.send(301);
    return;
  }
  if (server.hasArg("username") && server.hasArg("password")){
    if (server.arg("username") == set1["username2"].as<String>() &&  server.arg("password") == set1["password2"].as<String>() ){
      server.sendHeader("Set-Cookie","ESPSESSIONID=1");
      server.sendHeader("Access-Control-Allow-Origin", "*"); // Allow all origins
      server.sendHeader("Access-Control-Expose-Headers", "numPages");
      server.sendHeader("numPages","2");
      server.sendHeader("Cache-Control","no-cache");      
      File file = LittleFS.open("/set1.htm","r");
      if(!file){
        server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
      }
      server.streamFile(file,"text/html");
      Serial.println("Login Successful");
    } else  Serial.println("Login Failed");
  }
}

};

class FileServerHandler : public RequestHandler {
public:
  FileServerHandler() {
    TRACE("FileServerHandler is registered\n");
  }  
  bool canHandle(HTTPMethod requestMethod, const String UNUSED &_uri) override {
    return (requestMethod == HTTP_DELETE);
  }
  bool canUpload(const String &uri) override {
    return (uri == "/");
  }
  bool handle(ESP8266WebServer &server, HTTPMethod requestMethod, const String &requestUri) override {
    String fName = requestUri;
    if (!fName.startsWith("/")) { 
      fName = "/" + fName; 
    }
    if (requestMethod == HTTP_DELETE) {
      if (LittleFS.exists(fName)) { 
        LittleFS.remove(fName); 
      }
    }
    server.send(200);
    return (true);
  }
  void upload(ESP8266WebServer UNUSED &server, const String UNUSED &_requestUri, HTTPUpload &upload) override {
    String fName = upload.filename;
    if (!fName.startsWith("/")) { 
      fName = "/" + fName; 
    }
    if (upload.status == UPLOAD_FILE_START) {
      if (LittleFS.exists(fName)) { 
        LittleFS.remove(fName); 
      } 
      _fsUploadFile = LittleFS.open(fName, "w");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (_fsUploadFile) { 
        _fsUploadFile.write(upload.buf, upload.currentSize); 
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (_fsUploadFile) { 
        _fsUploadFile.close(); 
      }
    }
  }
protected:
  File _fsUploadFile;
};

struct SensorData {
  int rawLevel;
  time_t timeStamp;
};

static SensorData data;
static time_t now;
static esp8266::polledTimeout::periodicMs tick(60000);
static bool ntpTimeSet = false;
static time_t resetTime = 0;
static bool  por= true;
static int dateFileSent = 0;
static int dateToday = 0;
static int i = 0;
const int httpsPort = 443;
static char mode = '2';
static bool confSet = false;

static bool pumpOn = false;

bool captivePortalMode = false;
const char *ssid;
const char *password;

DynamicJsonDocument set1(256);
DynamicJsonDocument set2(256);

ESP8266WebServer captivePortalServer(80); 
DNSServer dnsServer;
ESP8266WebServer server(80);
HTTPSRedirect *client = new HTTPSRedirect(httpsPort);


void setup(void) {
  initProject();
}

void loop(void) {
  if (captivePortalMode) {
    dnsServer.processNextRequest();
    captivePortalServer.handleClient();
  } else {
    server.handleClient();
    MDNS.update();
  }
  if(tick){
    tickExec();
    switch (mode) {
      case '1':
      sendData();
      break;
      case '2':
      storeData();
      break;
      case '3':
      break;
      default:
      break;
    }
  }
  switchWaterPump();
}

void initProject(){
  pinMode (TRIG, OUTPUT);
  pinMode (ECHO, INPUT);
  pinMode(ON_Board_LED,OUTPUT);
  delay(3000);
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  TRACE("Mounting the filesystem...\n");
  if (!LittleFS.begin()) {
    Serial.println("could not mount the filesystem!");
    delay(2000);
    ESP.restart();
  }
  if (LittleFS.exists("/set1.json")){
    readJsonFile("/set1.json", set1);
    readJsonFile("/set2.json", set2);
    serializeJsonPretty(set1, Serial);
    serializeJsonPretty(set2, Serial);
  } else {
    Serial.println("\nFailed to connect to Wi-Fi. Starting Captive Portal...");
    captivePortalMode = true;
    startCaptivePortal();
  }
  connectToWifi(); 
}

void readJsonFile(const char* filePath, DynamicJsonDocument &doc){
  File file = LittleFS.open(filePath, "r");
  if(file){
    DeserializationError err1 = deserializeJson(doc,file);
    file.close();
    if(err1){
      Serial.print("Error parsing file: ");
      Serial.println(err1.c_str());
    }
  } else {
    Serial.println("Failed to open file");
  }
}

void connectToWifi(){
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Failed to configure static IP");
  }
  ssid = set1["ssid"];
  password = set1["psk"];
  digitalWrite(ON_Board_LED, HIGH);
  WiFi.begin(ssid, password);
  TRACE("Connecting to WiFi...");
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) { 
    TRACE(".");
    digitalWrite(ON_Board_LED, LOW);
    delay(250);
    digitalWrite(ON_Board_LED, HIGH);
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi. IP Address: " + WiFi.localIP().toString());
    stopCaptivePortal(); // Release Captive Portal resources
    captivePortalMode = false;
    WiFi.softAPdisconnect(true); // Disable the Access Point mode
    Serial.println("Access Point Mode disabled.");
    WiFi.setHostname(HOSTNAME);
    digitalWrite(ON_Board_LED, HIGH);
    startMainServer();
  } else {
    Serial.println("\nFailed to connect to Wi-Fi. Starting Captive Portal...");
    captivePortalMode = true;
    startCaptivePortal();
  }
}

void startCaptivePortal(){
  WiFi.softAP("ESP8266_Config");
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53,"*",apIP);
  Serial.println("Captive portal started at: " + apIP.toString());
  captivePortalServer.on("/", [](){
    File file = LittleFS.open("/cportal.htm","r");
    captivePortalServer.streamFile(file, "text/html");
    file.close();
  });
  captivePortalServer.on("/save", HTTP_POST, []() {
    set1["ssid"]=captivePortalServer.arg("ssid");
    set1["psk"]=captivePortalServer.arg("psk");
    saveDataToFile("/set1.json", set1); //obj1);
    captivePortalServer.send(200,"text/html", "Restarting......");
    Serial.println("Wi-Fi credentials saved. Restarting...");
    delay(5000);
    ESP.restart(); 
  });
  captivePortalServer.begin();
  Serial.println("Captive Portal Server Running...");
}

void stopCaptivePortal() {
  dnsServer.stop();
  captivePortalServer.stop();
  Serial.println("Captive Portal resources released.");
}

void saveDataToFile(const char* filePath, DynamicJsonDocument &doc){  // JsonObject& data){
  File file = LittleFS.open(filePath, "w");
  if (!file){
    Serial.println("Failed to open file");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void startMainServer(){
  mode = set1["mode"].as<unsigned char>();
  TRACE("Registering service handlers of webserver\n");  
  server.on("/$upload.htm", []() {
    server.send(200, "text/html", FPSTR(uploadContent));
  });
  server.on("/", HTTP_GET, handleRedirect);
  server.on("/getData", HTTP_GET, sendHtmlData);
  server.on("/$list", HTTP_GET, handleListFiles);
  server.on("/$sysinfo", HTTP_GET, handleSysInfo);
  //server.on("/login", HTTP_POST, handleLogin);
  server.onNotFound([]() {
    server.send(404, "text/html", FPSTR(notFoundContent));
  });
  server.addHandler(new SettingsHandler(server,set1));
  server.addHandler(new FileServerHandler());
  server.enableCORS(true);
  server.enableETag(true);
  server.serveStatic("/", LittleFS, "/"); 
  const char* headerKeys[]={"User-Agent", "Cookie"};
  size_t sizeKeys=sizeof(headerKeys)/sizeof(char*);
  server.collectHeaders(headerKeys,sizeKeys);
  server.begin();

  settimeofday_cb(ntpTimeSetCb);
  configTime(TIMEZONE, MY_NTP_SERVER);
  while (!ntpTimeSet) {
    yield();
  }
  time(&now);
  int remainderMsecs = (60-now%60)*1000;
  delay(remainderMsecs);
  tick.reset();
  resetTime = time(&now);
  TRACE("Reset Time: %s\n", ctime(&now));
  i = getDataCounter();
  
  TRACE("%s started.\n", WiFi.getHostname());

  MDNS.begin("LevelSensor");
  MDNS.addService("http", "tcp", 80);

  client->setInsecure();
  client->setPrintResponseBody(true);
  client->setContentTypeHeader("application/json");
}

void tickExec() {
  i++;
  data.timeStamp = time(&now);
  data.rawLevel = measureDistanceCm();
}

void sendData() { 
  TRACE("connecting to: %s  \n", host);
  String strTstamp = String(data.timeStamp); 
  String strLvl = String(data.rawLevel); 
  String url = String("/macros/s/") + GScriptId2 + String("/exec?timestamp=") + strTstamp + String("&level=") + strLvl;
  TRACE("requesting URL: %s \n", url.c_str());
  if (!client->connect(host, httpsPort)) {
  Serial.println("connection failed");
  return;
  }
  client->GET(url, host);
}

void storeData(){
  dateToday = (data.timeStamp+18000)/86400; //local time to utc
  if(dateFileSent != dateToday){      
    if(!por){
      sendFile("/data");
    }      
    writeToCsvFile("/data", data);
    dateFileSent = dateToday;
    por=false;
  } else {
    appendToCsvFile("/data", data);
  }    
}

void sendFile(const char* path) {
  String url = String("/macros/s/") + GScriptId2 + String("/dev");
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }
  String fileData = file.readString();
  file.close();
  if (!client->connect(host, httpsPort)) {
    Serial.println("Connection failed");
    return;
  }
  client->POST(url, host, fileData);
}

void switchWaterPump(){
  bool scheduledOn = false;
  bool scheduledOff = false;
  bool lvlLow = false;
  bool lvlReached = false;
  bool switchedOn = false;
  bool switchedOff = false;
  if(!pumpOn && (scheduledOn || lvlLow || switchedOn)){
    switchPumpOn();
  }
  if(pumpOn && (scheduledOff || lvlReached || switchedOff)){
    switchPumpOff();
  }
}

void switchPumpOn() {
  digitalWrite(ON_Board_LED, LOW);
}
void switchPumpOff() {
  digitalWrite(ON_Board_LED, HIGH);
}

// ===== Webserver service handlers =====

void handleRedirect() {
  TRACE("\nRedirect...");
  String url = "/index.htm";
  if (!LittleFS.exists(url)) { 
    url = "/$update.htm"; 
    }
  server.sendHeader("Location", url, true);
  server.send(302);
}

void handleListFiles() {
  Dir dir = LittleFS.openDir("/");
  String result;

  result += "[\n";
  while (dir.next()) {
    if (result.length() > 4) { result += ","; }
    result += "  {";
    result += " \"name\": \"" + dir.fileName() + "\", ";
    result += " \"size\": " + String(dir.fileSize()) + ", ";
    result += " \"time\": " + String(dir.fileTime());
    result += " }\n";
  }
  result += "]";
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "text/javascript; charset=utf-8", result);
}

// Function called when sensor data is requested from a browser
void sendHtmlData() {
  String result;
  float ht = set2["ht"].as<float>();
  float wd = set2["wd"].as<float>();
  float len = set2["len"].as<float>();
  float cap = ht*wd*len;
  int lvl = ht*100 - data.rawLevel;
  int waterGals = lvl * cap * 264.172 / ht /100;
  float cTotLast = 15.0;
  float cPres = cap * 264.172 - waterGals;

  result += "{";
  result += "\"lvl\":" + String(lvl) + ", ";
  result += "\"waterGals\":" + String(int(waterGals)) + ", ";
  result += "\"percent\":" + String(int(lvl/ht)) + ", ";
  result += "\"cPres\":" + String(int(cPres)) + ", ";
  result += "\"cLast\":" + String(200) + ", ";
  result += "\"cTot\":" + String(cTotLast + cPres/264.172);
  result += "}\n";
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json; charset=utf-8", result);  //textjavascript
}  


// This function is called when the sysInfo service was requested.
void handleSysInfo() {
  String result;
  FSInfo fs_info;
  LittleFS.info(fs_info);

  result += "{\n";
  result += "  \"flashSize\": " + String(ESP.getFlashChipSize()) + ",\n";
  result += "  \"freeHeap\": " + String(ESP.getFreeHeap()) + ",\n";
  result += "  \"fsTotalBytes\": " + String(fs_info.totalBytes) + ",\n";
  result += "  \"fsUsedBytes\": " + String(fs_info.usedBytes) + ",\n";
  result += "}";
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "text/javascript; charset=utf-8", result);
}

bool authenticated(){
  String cookie = server.header("Cookie");
  return (cookie.indexOf("ESPSESSIONID=1")!= -1) ? true: false;
}

//******* Sensor and time routines ********************

void ntpTimeSetCb () { //ntp callback
  ntpTimeSet = true;
}

int measureDistanceCm () { //measure water level using JSNSR04T us sensor
  using esp8266::polledTimeout::oneShotFastUs;
  digitalWrite (TRIG, LOW);
  oneShotFastUs timeOutLow(JSNSR04T_LOW_PULSE_uS);
  while (!timeOutLow) {
    yield();
  }
  digitalWrite (TRIG, HIGH);
  oneShotFastUs timeOutHigh(JSNSR04T_HIGH_PULSE_uS);
  while (!timeOutLow) {
    yield();
  }
  digitalWrite (TRIG, LOW);
  long result_us = pulseIn(ECHO, HIGH); //, PULSEIN_TIMEOUT_uS);
  int dist_cm = (int) result_us / CONVERSION_FACTOR * TEMP_FACTOR;
  return dist_cm;
}

unsigned long getDataCounter (){
  File file = LittleFS.open("/data", "r");
  if(!file) return 0;
  unsigned long dataCounter = file.size() / sizeof(data);
  file.close();
  return dataCounter;
}

//*************** LittleFS files manipulating routines *******

void dispDataFile() {
  struct SensorData dataRead;
  int j = 0;
  Serial.println("Reading data from /data");
  File file = LittleFS.open("/data", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    j++;
    file.read((byte *)&dataRead, sizeof(dataRead));
    Serial.printf("Data[%d] = %d, Time = % u\n", j, dataRead.rawLevel, dataRead.timeStamp);
  }
  Serial.println();
  file.close();
}

void writeToFile(const char * path, struct SensorData data) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (!file.write((byte *)&data, sizeof(data))) {
    Serial.println("Writing failed");
  } 
  file.close();
}


void appendToFile(const char * path, struct SensorData data) {
  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (!file.write((byte *)&data, sizeof(data))) {
    Serial.println("Append failed");
  } 
  file.close();
}

void appendToCsvFile(const char * path, struct SensorData data) {
  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  String strData = String(data.timeStamp)+","+String(data.rawLevel)+"\n";
  if (!file.write(strData.c_str(), sizeof(strData))) {
    Serial.println("Append failed");
  } 
  file.close();
}

void writeToCsvFile(const char * path, struct SensorData data) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  String strData = String(data.timeStamp)+","+String(data.rawLevel)+"\n";
  if (!file.write(strData.c_str(), sizeof(strData))) {
    Serial.println("Append failed");
  } 
  file.close();
}