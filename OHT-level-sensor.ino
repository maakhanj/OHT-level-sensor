
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
#include <rcswitch.h>

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
static int i = 0;
const int httpsPort = 443;
static int mode = 2;
bool captivePortalMode = false;
const char *ssid;
const char *password;
static bool pumpOn = false;
static bool scheduledOn = false;
static bool scheduledOff = false;
static bool lvlLow = false;
static bool lvlReached = false;
static bool switchedOn = false;
static bool switchedOff = false;
static bool overflo = false;
static bool fileSent = false;
static bool ticked = false;
static bool timerIconDisable = false;
static bool pumpIconDisable = false;
//static bool repeatDaily = false;

DynamicJsonDocument set1(256);
DynamicJsonDocument set2(768);

ESP8266WebServer captivePortalServer(80); 
DNSServer dnsServer;
ESP8266WebServer server(80);

HTTPSRedirect *client = new HTTPSRedirect(httpsPort);
RCSwitch mySwitch = RCSwitch();

void handleEssentialData();
void handleSchTimings();

void setup(void) {
  initNetConnection();
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
      case 1:
      sendDataToGoogleDrive();
      break;
      case 2:
      storeData();
      break;
      case 3:
      break;
      default:
      break;
    }
  }
  checkWaterLevel();
  if (ticked) {
    checkScheduledTime();
    ticked = false;
  }
  switchWaterPump();
}

void initNetConnection(){
  pinMode(LED_BUILTIN,OUTPUT);
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
    if (!set1.containsKey("ssid")){  //(set1["ssid"].isNull()) {
      Serial.println("Wifi credentials not set. Please configure.");
      captivePortalMode = true;
      startCaptivePortal();
      return;
    }
    serializeJsonPretty(set1, Serial);
  } else {
    Serial.println("\nConfiguration file not available. Starting Captive Portal...");
    captivePortalMode = true;
    startCaptivePortal();
    return;
  }
  if (LittleFS.exists("/set2.json")){
    readJsonFile("/set2.json", set2);   
    serializeJsonPretty(set2, Serial);
  } 
  connectToWifi(); 
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
    saveDataToFile("/set1.json", set1);
    captivePortalServer.send(200,"text/html", "Restarting......");
    Serial.println("Wi-Fi credentials saved. Restarting...");
    delay(1000);
    stopCaptivePortal();
    ESP.restart(); 
  });
  captivePortalServer.on("/generate_204", []() {
    captivePortalServer.send(200, "text/html", "Redirecting...");
  });
  captivePortalServer.on("/captive.apple.com", []() {
    captivePortalServer.send(200, "text/html", "Redirecting...");
  });
  captivePortalServer.onNotFound([]() {
    captivePortalServer.send(404, "text/html", FPSTR(notFoundContent));
  });
  captivePortalServer.begin();
  Serial.println("Captive Portal Server Running...");
}

void stopCaptivePortal() {
  dnsServer.stop();
  captivePortalServer.stop();
  Serial.println("Captive Portal resources released.");
}

void connectToWifi(){
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Failed to configure static IP");
  }
  ssid = set1["ssid"];
  password = set1["psk"];
  digitalWrite(LED_BUILTIN, HIGH);
  WiFi.begin(ssid, password);
  TRACE("Connecting to WiFi...");
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) { 
    TRACE(".");
    digitalWrite(LED_BUILTIN, LOW);
    delay(250);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi. IP Address: " + WiFi.localIP().toString());
    captivePortalMode = false;
    WiFi.softAPdisconnect(true);
    Serial.println("Access Point Mode disabled.");
    WiFi.setHostname(HOSTNAME);
    digitalWrite(LED_BUILTIN, HIGH);
    startMainServer();
    Serial.println("Main server started.");
  } else {
    Serial.println("\nFailed to connect to Wi-Fi. Starting Captive Portal...");
    captivePortalMode = true;
    startCaptivePortal();
  }
}

void readJsonFile(const char* filePath, DynamicJsonDocument &doc){
  File file = LittleFS.open(filePath, "r");
  if(file){
    DeserializationError err = deserializeJson(doc,file);
    file.close();
    if(err){
      Serial.print("Error parsing file: ");
      Serial.println(err.c_str());
    }
  } else {
    Serial.println("Failed to open file");
  }
}


void saveDataToFile(const char* filePath, DynamicJsonDocument &doc){ 
  File file = LittleFS.open(filePath, "w");
  if (!file){
    Serial.println("Failed to open file");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void startMainServer(){
  settimeofday_cb(ntpTimeSetCb);
  configTime(TIMEZONE, MY_NTP_SERVER);

  pinMode (TRIG, OUTPUT);
  pinMode (ECHO, INPUT);
  pinMode(RC_PIN,OUTPUT);
  pinMode(LVL_SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN_2, OUTPUT);
  digitalWrite(LED_BUILTIN_2, HIGH);

  mode = set1["mode"].as<int>();
  mySwitch.enableTransmit(RC_PIN);
  mySwitch.setProtocol(1);
  mySwitch.setPulseLength(345);
  mySwitch.setRepeatTransmit(10);

  //LINK OHT-level-sensor#routeHandlers
  
  TRACE("Registering service handlers of webserver\n"); 
  server.on("/saveEssentialData", HTTP_POST, handleSaveEssentialData);
  server.on("/", HTTP_GET, handleRedirect);
  server.on("/getData", HTTP_GET, sendHtmlData);
  server.on("/switchPump", HTTP_GET, hadlePumpSwitching);
  server.on("/schTimings", HTTP_GET, handleSchTimings);
  server.on("/saveSchTimings", HTTP_POST, handleSaveSchTimings);
  server.on("/$list", HTTP_GET, handleListFiles);
  server.on("/$sysinfo", HTTP_GET, handleSysInfo);
  server.on("/getStoreValues", HTTP_GET, handleGetStoreValues);
  server.on("/saveGdriveFolderId", HTTP_POST, handleSaveGdriveFolderId);
  server.on("/$upload.htm", []() {
    server.send(200, "text/html", FPSTR(uploadContent));
  });
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

  delay(1000);

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
  
  MDNS.begin("LevelSensor");
  MDNS.addService("http", "tcp", 80);

  client->setInsecure();
  client->setPrintResponseBody(true);
  client->setContentTypeHeader("application/json");
  attachInterrupt(LVL_SENSOR_PIN, isrCallback, FALLING);
}

void IRAM_ATTR isrCallback() {
  if (digitalRead(LVL_SENSOR_PIN) == LOW) {
    overflo = true;
  }
}

void tickExec() {
  i++;
  data.timeStamp = time(&now);
  data.rawLevel = measureDistanceCm();
  ticked = true;
}

void sendDataToGoogleDrive() { 
  TRACE("connecting to: %s  \n", host);
  String strTstamp = String(data.timeStamp); 
  String strLvl = String(data.rawLevel); 
  String url = String("/macros/s/") + set2["gscriptId"].as<String>() + String("/exec?timestamp=") + strTstamp + String("&level=") + strLvl;
  TRACE("requesting URL: %s \n", url.c_str());
  if (!client->connect(host, httpsPort)) {
  Serial.println("connection failed");
  return;
  }
  client->GET(url, host);
}

void storeData(){
  String strData;
  const char* attr;
  struct tm *timeCap;
  timeCap = localtime(&now);
  int mDay = timeCap->tm_mday;
  strData = String(data.timeStamp) +"," + String(data.rawLevel) + "," + "\n";
  if (LittleFS.exists("/data.csv")){
    attr = "a";
  } else {
    attr = "w";
    strData = String("Time") + "," + String("Level") + "," + "\n" + strData;
  }
  if (mDay == 2) {
    fileSent = false;
  }
  //dateToday = (data.timeStamp+18000)/86400; //local time to  
  if((mDay==1)&& !fileSent){      
      sendFileToGoogleDrive("/data.csv");
      fileSent = true;
      attr = "w";
      strData = String("Time") + "," + String("Level") + "," + "\n" + strData;
  }   
    storeToCsvFile("/data.csv", attr, strData.c_str());
}

void sendFileToGoogleDrive(const char* path) {
  String url = String("/macros/s/") + set2["gscriptId"].as<String>() + String("/dev");
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
  const char* attr;
  String strData;
  if (LittleFS.exists("/pumpData.csv")){
    attr = "a";
  } else {
    attr = "w";
    storeToCsvFile("/pumpData.csv", attr, "Category Switch On,Time On,Level at Switch On,Category Switch Off,Time Off,Level at Switch Off,Consumption Last Day,\n");
  }
  if(!pumpOn && (scheduledOn || lvlLow || switchedOn)){
    
    if (scheduledOn) {
      storeToCsvFile("/pumpData.csv", attr, "ScheduledOn,");
      scheduledOn = false;
    }
    if (lvlLow) {
      storeToCsvFile("/pumpData.csv", attr, "LvlLow,");
      lvlLow = false;
    }
    if (switchedOn) {
      storeToCsvFile("/pumpData.csv", attr, "SwitchedOn,");
      switchedOn = false;
    }
    switchPumpOn();
  }
  if(pumpOn && (scheduledOff || lvlReached || switchedOff || overflo)){
    
    if (scheduledOff) {
      storeToCsvFile("/pumpData.csv", attr, "ScheduledOff,");
      scheduledOff = false;
    }
    if (lvlReached) {
      storeToCsvFile("/pumpData.csv", attr, "LvlReached,");
      lvlReached = false;
    }
    if (switchedOff) {
      storeToCsvFile("/pumpData.csv", attr, "SwitchedOff,");
      switchedOff = false;
    }
    if (overflo) {
      storeToCsvFile("/pumpData.csv", attr, "Overflo,");
      overflo = false;
    }
    switchPumpOff();
  }
}

void switchPumpOn() {
  String strData;
  digitalWrite(LED_BUILTIN, LOW);
  time(&now);
  mySwitch.send(set2["rfcode"].as<int>(), 24);
  pumpOn = true;
  set2["lvlAtOn"] = set2["ht"].as<int>()- data.rawLevel;
  saveDataToFile("/set2.json", set2);
  strData = String(now) + "," + set2["lvlAtOn"].as<String>() + ",";
  storeToCsvFile("/pumpData.csv", "a", strData.c_str());
}

void switchPumpOff() {
  struct tm timeCap;
  String strData;
  time(&now);
  localtime_r(&now, &timeCap);
  int mDay = timeCap.tm_mday;
  digitalWrite(LED_BUILTIN, HIGH);
  mySwitch.send(set2["rfcode"].as<int>(), 24);  //10755076
  pumpOn = false;
  set2["lvlAtOff"] = set2["ht"].as<int>()- data.rawLevel;
  set2["consumpLast"] = (1- set2["lvlAtOn"].as<int>()/set2["lvlAtOff"].as<int>())*set2["capGal"].as<int>();
  if (mDay==1){
    set2["consumpMonth"] = set2["consumpLast"].as<int>();
  } else {
    set2["consumpMonth"] = set2["consumpMonth"].as<int>() + set2["consumpLast"].as<int>();
  }
  if (scheduledOff && set2["checked"].as<int>()!= 1) {
    set2.remove("onTime");
    set2.remove("offTime");
    scheduledOff = false;
    scheduledOn = false;
  }
  pumpIconDisable = true;
  saveDataToFile("/set2.json", set2);
  strData = String(now) + "," + String(set2["lvlAtOff"]) + "," + set2["consumpLast"].as<String>() + "\n";
  storeToCsvFile("/pumpData.csv", "a", strData.c_str());
}

void checkWaterLevel() {
  int lvl = set2["ht"].as<int>() - data.rawLevel;
  int percent = lvl*100.0/set2["ht"].as<int>();
  if (lvl < 0) {
    return;
  }
  percent <= set2["low"].as<int>() ? lvlLow = true : lvlLow = false;
  percent >= set2["high"].as<int>() ? lvlReached = true : lvlReached = false;
}

void checkScheduledTime() {
  if (set2.containsKey("onTime")) {
    struct tm timeCap;
    localtime_r(&now, &timeCap);
    int currentHour = timeCap.tm_hour;
    int currentMinute = timeCap.tm_min;
    int scheduledOnHour, scheduledOnMinute, scheduledOffHour, scheduledOffMinute;
    sscanf(set2["onTime"].as<String>().c_str(), "%d:%d", &scheduledOnHour, &scheduledOnMinute);
    sscanf(set2["offTime"].as<String>().c_str(), "%d:%d", &scheduledOffHour, &scheduledOffMinute);
    Serial.printf("Current Time: %02d:%02d\n", currentHour, currentMinute);
    Serial.printf("Scheduled On Time: %02d:%02d\n", scheduledOnHour, scheduledOnMinute);
    Serial.printf("Scheduled Off Time: %02d:%02d\n", scheduledOffHour, scheduledOffMinute);
    if (currentHour == scheduledOnHour && currentMinute == scheduledOnMinute && !scheduledOn) {
      scheduledOn = true;
    }
    if (currentHour == scheduledOffHour && currentMinute == scheduledOffMinute && !scheduledOff) {
      scheduledOff = true;
      timerIconDisable = true;
    }
  }
}

void ntpTimeSetCb () { //ntp callback
  ntpTimeSet = true;
}

//ANCHOR [routeHandlers]

void handleSaveEssentialData(){
  set2["rfcode"] = server.arg("rfcode");
  set2["wd"] = server.arg("wd");
  set2["ht"] = server.arg("ht");
  set2["tkType"] = server.arg("tkType");
  set2["gscriptId"] = server.arg("gscriptId");
  if (server.hasArg("len")) {
    set2["len"] = server.arg("len");
  }
  float cap;
  int ht = set2["ht"].as<int>();
  int wd = set2["wd"].as<int>(); 
  if (set2["tkType"] == "1") {
    int len = set2["len"].as<int>();
    cap = ht*wd*len/1000000.0;
    Serial.printf("Tank capacity(foramt spec): %f m3\n", cap);
  } else {
    cap = ht*wd*wd*PI/1000000.0;
  }
  Serial.println("Tank capacity: " + String(cap) + " m3");
  set2["cap"] = cap;
  set2["capGal"] = int(cap*264.172);
  set2["consumpLast"] = 0;
  set2["consumpMonth"] = 0;
  saveDataToFile("/set2.json", set2);
  server.sendHeader("Location", "/index.htm", true);
  server.send(302);
}

void handleRedirect() {
  TRACE("\nRedirect...");
  if(!set2.containsKey("rfcode")){
   server.sendHeader("Location", "/essential.htm", true);
   server.send(302);
   return; 
  }
    String url = "/index.htm";
    if (!LittleFS.exists(url)) { 
      url = "/$update.htm"; 
    }
    server.sendHeader("Location", url, true);
    server.send(302);
}

void sendHtmlData() {
  String result;
  int ht = set2["ht"].as<int>();
  int lvl = ht - data.rawLevel;
  int waterGals = (set2["capGal"].as<float>() * lvl) / ht;
  int cPres = set2["capGal"].as<int>() - waterGals;
  saveDataToFile("/set2.json", set2);

  result += "{";
  result += "\"lvl\":" + String(lvl) + ", ";
  result += "\"waterGals\":" + String(waterGals) + ",";
  result += "\"percent\":" + String((lvl*100.0/ht)) + ",";
  result += "\"cPres\":" + String(cPres) + ", ";
  result += "\"cLast\":" + String(set2["consumpLast"]) + ",";
  result += "\"cTot\":" + String((set2["consumpMonth"].as<int>() + cPres) / 264.172) + ",";
  result += "\"timerIconDisable\":" + String(timerIconDisable) + ",";
  result += "\"pumpIconDisable\":" + String(pumpIconDisable);
  result += "}\n";
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json; charset=utf-8", result);  //textjavascript
  timerIconDisable = false;
  pumpIconDisable = false;
}

void handleSchTimings() {
  File file = LittleFS.open("/schTimings.htm", "r");
  if (!file) {
    server.send(500, "text/html", "Failed to open file");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleSaveSchTimings(){
  set2["onTime"] = server.arg("onTime");
  set2["offTime"] = server.arg("offTime");
  set2["checkBox"] = server.arg("checkBox");
  saveDataToFile("/set2.json", set2);
  server.send(200, "text/html", "Data saved");
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

void hadlePumpSwitching(){
  String reqStr = server.arg("req");
  String jsonResponse = "{\"status\":\"on\"}";
  if (reqStr == "on") {
    switchedOn = true;
    server.send(200, "application/json", jsonResponse);
  } else if (reqStr == "off") {
    switchedOff = true;
    jsonResponse = "{\"status\":\"off\"}";
    server.send(200, "application/json", jsonResponse);
  } else {
    jsonResponse = "{\"error\":\"Invalid state\"}";
    server.send(400, "application/json", jsonResponse);
  }
}

void handleGetStoreValues(){
  File file = LittleFS.open("/set2.json", "r");
  if (!file) {
    server.send(500, "application/json", "{\"error\":\"Failed to open file\"}");
    return;
  }
  server.streamFile(file, "application/json");
  file.close();
}

void handleSaveGdriveFolderId(){
  set2["gfolderId"] = server.arg("folderId");
  Serial.println(server.arg("folderId"));
  saveDataToFile("/set2.json", set2);
  server.send(200, "text/html", "Data saved");
}

//******* Sensor and time routines ********************

int measureDistanceCm () { //measure water level using JSNSR04T us sensor
  digitalWrite (TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite (TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite (TRIG, LOW);
  long result_us = pulseIn(ECHO, HIGH);
  int dist_cm = (int) result_us / CONVERSION_FACTOR * TEMP_FACTOR;
  return dist_cm;
}

//*************** LittleFS files manipulating routines *******

unsigned long getDataCounter (){
  File file = LittleFS.open("/data", "r");
  if(!file) return 0;
  unsigned long dataCounter = file.size() / sizeof(data);
  file.close();
  return dataCounter;
}

void storeToFileDataLvlLocal(const char * path, const char* attr) {
  File file = LittleFS.open(path, attr);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", path);
    return;
  }
  if (!file.write((byte *)&data, sizeof(data))) {
    Serial.println("File operation failed");
  } 
  file.close();
}

void storeToCsvFile(const char* path, const char* attr, const char* strData) {
  File file = LittleFS.open(path, attr);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", path);
    return;
  }
  if (!file.write(strData)) {
    Serial.println("File operation failed");
  } 
  file.close();
}

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
    Serial.printf("Data[%d] = %d, Time = % lld\n", j, dataRead.rawLevel, dataRead.timeStamp);
  }
  Serial.println();
  file.close();
}