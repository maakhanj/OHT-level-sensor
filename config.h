//#define DEBUG_TRACE

#ifdef DEBUG_TRACE
#define TRACE(...) Serial.printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

#define UNUSED __attribute__((unused))
#define HOSTNAME "webserver"
#define MY_NTP_SERVER "pk.pool.ntp.org"           
#define TIMEZONE TZ_Asia_Karachi   
#define RTC_RESET_EPOCH 1730359801
#define JSNSR04T_LOW_PULSE_uS 5
#define JSNSR04T_HIGH_PULSE_uS 30 
#define CONVERSION_FACTOR 58
#define TEMP_FACTOR 1.0
#define TRIG 12
#define ECHO 13
#define RC_PIN 4
#define LVL_SENSOR_PIN 5
#define LED_BUILTIN_2 14

const char* host = "script.google.com";
//const char* GScriptId = "";
//const char* GScriptId2 = "";
IPAddress local_IP(192, 168, 1, 184);  // Desired static IP
IPAddress gateway(192, 168, 1, 1);     // Router's gateway
IPAddress subnet(255, 255, 255, 0);    // Subnet mask
IPAddress primaryDNS(8, 8, 8, 8);      // Optional: Primary DNS
IPAddress secondaryDNS(8, 8, 4, 4);    // Optional: Secondary DNS