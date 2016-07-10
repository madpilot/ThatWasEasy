#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

#include "WiFiManager.h"

#define KEY_UP 0
#define KEY_DOWN 1

#define BUTTON 4

#define LED_1 13
#define LED_2 14

#define NO_LED 0
#define RED_LED 1
#define GREEN_LED 2

#define DEBOUNCE_DELAY 10

int debounce = 0;
int buttonState = KEY_UP;
int configMode = false;

void keySetup() {
  pinMode(BUTTON, INPUT);
  
  buttonState = digitalRead(BUTTON);
  Serial.print("Button setup. Current State: ");
  Serial.println(buttonState);
}

void keyLoop() {
  long now = millis();
  int newState = digitalRead(BUTTON);
  
  if(debounce == 0 && newState != buttonState) {
    debounce = now;
  } else if(now - debounce > DEBOUNCE_DELAY && newState != buttonState) {
    buttonState = newState;
    debounce = 0;
    
    if(buttonState == KEY_UP) {
      keyUp();
    } else {
      keyDown();
    }
  }
}

void ledSetup() {
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
}

void ledOn(int colour) {
  switch(colour) {
  case NO_LED:
    digitalWrite(LED_1, LOW);
    digitalWrite(LED_2, LOW);
    break;
  case RED_LED:
    digitalWrite(LED_1, HIGH);
    digitalWrite(LED_2, LOW);
    break;
  case GREEN_LED:
    digitalWrite(LED_1, LOW);
    digitalWrite(LED_2, HIGH);
    break;
  }
}

void ledsOff() {
  ledOn(NO_LED);
}


#define HOSTNAME "easy"
#define CONFIG_AP_SSID "easy"

const byte DNS_PORT = 53;
DNSServer dnsServer;

ESP8266WebServer webServer(80);

// Needs to store:
// {"u":[255],"d":[255],"n":[63]}
// So 6 per key + str len + 1 commas, 2 braces
// 6 * 2 + 255 + 63 + 1 + 2 rounded up to 384 :)
#define CONFIG_LEN 384
#define WEBHOOK_URL_LEN 255
#define DEVICE_NAME_LEN 63

char webhook_url[WEBHOOK_URL_LEN] = "";
char device_name[DEVICE_NAME_LEN] = HOSTNAME;

void webhook(int state) {
  HTTPClient http;
  http.begin(webhook_url);

  const char *payload;
  
  if(state == KEY_UP) {
    payload = "{\"key\":\"up\"}";
  } else {
    payload = "{\"key\":\"down\"}";
  }
  http.addHeader("Content-type", "application/json");
  http.POST((uint8_t *)payload, strlen(payload));
  http.end();
}

void keyUp() {
  Serial.println("Key Up");
  webhook(KEY_UP);
}

void keyDown() {
  Serial.println("Key Down");
  webhook(KEY_DOWN);
}

int saveFlag = false;
void saveCallback() {
  saveFlag = true;
}

void writeConfig () {  
  StaticJsonBuffer<CONFIG_LEN> buf;

  JsonObject &root = buf.createObject();
  root["w"] = webhook_url;
  root["n"] = device_name;

  Serial.print("Saving config... ");
  
  if (SPIFFS.begin()) {
    File configFile = SPIFFS.open("/config.json", "w+");
    Serial.println("\nWriting...");
    root.printTo(Serial);

    root.printTo(configFile);
    configFile.close();
    Serial.println("Done!");
  } else {
    Serial.println("Could not save config file");
  }
}

void readConfig() {
  Serial.print("Reading config file... ");
  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> json(new char[size]);
        configFile.readBytes(json.get(), size);

        StaticJsonBuffer<CONFIG_LEN> buf;
        JsonObject &root = buf.parseObject(json.get());
        
        if(root.success()) {
          strcpy(webhook_url, root["w"]);
          strcpy(device_name, root["n"]);
          
          Serial.println("Done!");
        } else {
          Serial.println("Unable to parse JSON");
        }
        configFile.close();
        
      } else {
        Serial.println("Unable to open config.json");
      }
    } else {
      Serial.println("config.json not found");
    }
  } else {
    Serial.println("Couldn't access file system");
  }
}

void wifiSetup() {
  WiFiManager wifiManager;
  
  WiFiManagerParameter webhook_url_parameter("webhook", "Webhook URL", webhook_url, WEBHOOK_URL_LEN);
  WiFiManagerParameter device_name_parameter("deviceName", "Device name", device_name, DEVICE_NAME_LEN);  
  
  wifiManager.setSaveConfigCallback(saveCallback);
  wifiManager.addParameter(&webhook_url_parameter);
  wifiManager.addParameter(&device_name_parameter);
  
  if(configMode) {
    Serial.println("Going in to config mode");
    wifiManager.startConfigPortal(CONFIG_AP_SSID);
  } else {  
    wifiManager.autoConnect(CONFIG_AP_SSID);
  }

  strcpy(webhook_url, webhook_url_parameter.getValue());
  strcpy(device_name, device_name_parameter.getValue());
  
  if(saveFlag) {
    writeConfig();
  }
    
  setNetworkName(device_name);
}

void setNetworkName(char *name) {
  wifi_station_set_hostname(name);
  
  if (!MDNS.begin(name)) {
    Serial.println("Couldn't set mDNS name");
    return;
  }

  Serial.print("Device is available at ");
  Serial.print(name);
  Serial.println(".local");
}

void getIndex() {
  File f = SPIFFS.open("/index.html", "r");
  webServer.setContentLength(f.size());
  webServer.streamFile(f, "text/html");
  f.close();
}

void getConfig() {
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");

  StaticJsonBuffer<CONFIG_LEN> buf;

  JsonObject &root = buf.createObject();
  root["webhook"] = webhook_url;
  root["deviceName"] = device_name;
  root["apConfigured"] = true;

  char json[CONFIG_LEN];
  root.printTo(json, sizeof(json));
  webServer.sendContent(json);

  Serial.println("JSON response sent");
}

void postSave() {
  Serial.println("Saving Configuration Settings");
  Serial.print("Sent ");
  Serial.println(webServer.arg("plain"));

  String text = webServer.arg("plain");
  
  DynamicJsonBuffer buf;
  JsonObject &root = buf.parseObject(text);
        
  if(root.success()) {
    strcpy(webhook_url, root["webhook"]);
    strcpy(device_name, root["deviceName"]);
    setNetworkName(device_name);
    writeConfig();
    
    Serial.println("Done!");
    webServer.send(200, "text/plain", "done");
  } else {
    Serial.println("Unable to parse JSON");
    webServer.send(500, "text/plain", "Error!");
  }
}

void webServerSetup() {
  webServer.on("/", getIndex);
  webServer.on("/config.json", getConfig);
  webServer.on("/save", postSave);
  webServer.begin();
}

void webServerLoop() {
  webServer.handleClient();
}

void setup() {
  Serial.begin(115200);
  ledSetup();
  ledOn(RED_LED);
  keySetup();
  readConfig();

  // If the button is held down on power up, go into config mode
  if(buttonState == KEY_DOWN) {
    configMode = true;
  }

  wifiSetup();
  webServerSetup();
  ledOn(GREEN_LED);
}


void loop() {
  keyLoop();
  webServerLoop();
}
