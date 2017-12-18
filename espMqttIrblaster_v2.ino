/*
- Uses a Wemos D1 Mini.


* - Receives Mqtt messages and sends IR codes to the tv and other equipment.
* - Receives ir-codes and send them via Mqtt.
* - Measures room temperature
* - Measure light level with an IR-transistor 
*   and send the value via Mqtt.

* In progress - Receive Mqtt and send RF codes

Serial baud rate: 115200
 
Patrik Hermansson 2017


* Send Mqtt messages like this "Manufacturer code, ir code, code length"
* Manufacturer code are 1 for Samsung, 2 for LG, 3 for Yamaha
* 
* ir code 
* Find your remote at http://lirc.sourceforge.net/remotes/
* Note bits, pre_data_bits, pre_data and a code
* Example, Samsung BN59-00538A. 
* bits = 16, pre_data_bits = 16, pre_data = 0xE0E0, power on/off code = 0x40BF
* 
* Then the message to send is Manu code, pre_data+code, pre_data_bits+bits, longpress (0 or 1) =
* "1,E0E040BF,32,1"
* Longpress gives a longer transmission, sometimes needed to turn of equipment.
* 
* Example:
* mosquitto_pub -h 192.168.1.79 -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/irsender' -m '1,E0E040BF,32,1'
* 
* IR transmitter
* 330 ohm from D1 to BC547 base. Emittor to ground, collector to ir led cathod.
* Two IR leds in parallel, anode to +5V. 
* 
* IR Receiver on D2
* RF Transmitter on D5.
* 
* DS18B20 with 4k7 resistor on D3
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>  // Needed if you want to send IR commands.
#include <IRrecv.h>  // Needed if you want to receive IR commands.
#include <IRutils.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Define I/O:s
IRsend irsend(5); //an IR led is connected to Gpio5/D1
IRrecv irrecv(D2);
#define ONE_WIRE_BUS D3
#define lightsensor A0

OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// IR
decode_results results;

// For state machine
long lastMsg = 0;
String temp, hum, curHour, curMinute;
String localip;

//For Json output
StaticJsonBuffer<200> jsonBuffer;
JsonObject& root = jsonBuffer.createObject();
char msg[100];

char ssid[]="";
const char* password = "..........";

// Mqtt
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_server = "192.168.1.79";

// Mqtt topics
const char* mqtt_sub_topic = "espMqttIrblaster/irsender";
const char* mqtt_pub_topic = "espMqttIrblaster/sensors";
const char* mqtt_irpub_topic = "espMqttIrblaster/irrec";

void setup() {
  //char* bestWifi[15];
  Serial.begin(115200);
  Serial.println("Booting");

  // Setup wifi

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks(false,true);
  Serial.println("Scan done");
  Serial.print(n);
  Serial.println(" networks found");

  if (n == 0)
    Serial.println("no networks found");
  else
  {
    // Sort by RSSI, connect to the best network
    int minimum = WiFi.RSSI(0);
    int index = 0;
    uint8_t c;
    for (c = 1; c < n; c++) {
      int p = WiFi.RSSI(c);
      if (p > minimum) {
        minimum = p;
        index = c;
      }    
    }
    strcpy (ssid, WiFi.SSID(index).c_str());
    Serial.print ("Best network is ");
    Serial.println(ssid);
    Serial.println ("Connecting...");
  } 
  WiFi.mode(WIFI_STA);
  // Connect to strongest network
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  WiFi.hostname("espMqttIrBlaster");

  // Mqtt
  Serial.println("Mqtt");
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // What to do when a Mqtt message arrives
  if (!client.connected()) {
      reconnect();
  }

  // Subscribe to topic
  boolean rc = client.subscribe(mqtt_sub_topic);

  localip = WiFi.localIP().toString();
  Serial.print("IP address: ");
  Serial.println(localip);

  char mess[50];
  String sMess;
  sMess = "Hello from espMqttIrblaster_v2 @ " + localip; 
  sMess.toCharArray(mess, 50);
  client.publish(mqtt_pub_topic, mess);  // Wants a char

  // Initialize irsender
  Serial.println("IR");
  irsend.begin();
  irrecv.enableIRIn(); // Start the receiver

  // DS18B20
  sensors.begin();

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");


  if (MDNS.begin("espMqttIrblaster")) {
    Serial.println("MDNS responder started");
  }

}
void loop() {
  ArduinoOTA.handle();
  if (irrecv.decode(&results)) {
    // print() & println() can't handle printing long longs. (uint64_t)
    Serial.print("IR rec:");
    serialPrintUint64(results.value, HEX);
    Serial.println("");
    irrecv.resume();  // Receive the next value
  }
  delay(100);
     // client.publish(mqtt_pub_topic, "Check");  // Wants a char


  // "State machine", check sensor and send values when 2 minutes have passed.
  long now = millis();
  if (now - lastMsg > 120000) {  // Every 2 minutes
  //if (now - lastMsg > 10000) {  // Every 10 seconds
    lastMsg = now;

    sensors.requestTemperatures(); // Send the command to get temperatures
    float temp = sensors.getTempCByIndex(0);
    Serial.print("Temp: ");
    Serial.println(sensors.getTempCByIndex(0)); 

    // Get light level
    int ll = lightlevel(lightsensor); 
    Serial.print("Light: ");
    Serial.println(ll);
    

    root["temp"] = temp;
    root["light"] = ll;

    root.printTo((char*)msg, root.measureLength() + 1);
    client.publish(mqtt_pub_topic, msg);  // Wants a char
  }
  
    // Mqtt
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
}




void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("EspWasherWeb", "emonpi", "emonpimqtt2016")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("EspWasher","hello world from washer");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// When a Mqtt message has arrived
void callback(char* topic, byte* payload, unsigned int length) {
  char message[14] ="";
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  // Extract payload
  String stringPayload = "";
  for (int i = 0; i < length; i++) {
    //Serial.print((char)payload[i]);
    stringPayload += (char)payload[i];
  }
  Serial.println(stringPayload);

  uint64_t uPayload = 0;
  // Byte to uint64
  memcpy(&uPayload, payload, sizeof(uint64_t));
  //irsend.sendSAMSUNG(uPayload, 32);
  irsend.sendSAMSUNG(551489775, 32);
  irsend.sendLG(551489775, 28);
   irsend.sendLG(0x20DF10EF, 28);
  

}

int lightlevel(int adPin)                       // Measures volts at adPin
{                                            
 return int(analogRead(adPin));
}  
