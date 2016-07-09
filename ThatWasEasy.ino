#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <FS.h>
#include <ArduinoJson.h>

#include "WiFiManager.h"

#define KEY_UP 0
#define KEY_DOWN 1
#define BUTTON 4

#define DEBOUNCE_DELAY 50

int debounce = 0;
int buttonState = KEY_UP;
int configMode = true;

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

void keyUp() {
  Serial.println("Key Up");
}

void keyDown() {
  Serial.println("Key Down");
}

#define HOSTNAME "easy"
#define CONFIG_AP_SSID "easy"

const byte DNS_PORT = 53;
DNSServer dnsServer;

ESP8266WebServer webServer(80);

// Needs to store:
// {"u":[255],"d":[255],"n":[63]}
// So 6 per key + str len + 2 commas, 2 braces
// 6 * 3 + 255 + 255 + 63 + 2 + 2 rounded up to 600 :)
#define CONFIG_LEN 600
#define UP_URL_LEN 255
#define DOWN_URL_LEN 255
#define DEVICE_NAME_LEN 63

char up_url[UP_URL_LEN] = "";
char down_url[DOWN_URL_LEN] = "";
char device_name[DEVICE_NAME_LEN] = HOSTNAME;

int saveFlag = false;
void saveCallback() {
  saveFlag = true;
}

void writeConfig () {
  Serial.print("Before");
  Serial.println(device_name);
  
  StaticJsonBuffer<CONFIG_LEN> buf;

  JsonObject &root = buf.createObject();
  root["u"] = up_url;
  root["d"] = down_url;
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
          strcpy(up_url, root["u"]);
          strcpy(down_url, root["d"]);
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

void wifiConfigureSetup() {
  WiFiManager wifiManager;
  
  WiFiManagerParameter up_url_parameter("up_url", "Key Up URL", up_url, UP_URL_LEN);
  WiFiManagerParameter down_url_parameter("down_url", "Key Down URL", down_url, DOWN_URL_LEN);
  WiFiManagerParameter device_name_parameter("device_name", "Device name", device_name, DEVICE_NAME_LEN);  
  
  wifiManager.setSaveConfigCallback(saveCallback);
  wifiManager.addParameter(&up_url_parameter);
  wifiManager.addParameter(&down_url_parameter);
  wifiManager.addParameter(&device_name_parameter);
  
  if(configMode) {
    Serial.println("Going in to config mode");
    wifiManager.startConfigPortal(CONFIG_AP_SSID);
  } else {  
    wifiManager.autoConnect(CONFIG_AP_SSID);
  }

  strcpy(up_url, up_url_parameter.getValue());
  strcpy(down_url, down_url_parameter.getValue());
  strcpy(device_name, device_name_parameter.getValue());

  Serial.print("Before");
  Serial.println(device_name);
  
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

void setup() {
  Serial.begin(115200);
  keySetup();
  readConfig();

  // If the button is held down on power up, go into config mode
  if(buttonState == KEY_DOWN) {
    configMode = true;
  }

  wifiConfigureSetup();
}


void loop() {
  keyLoop();
}
