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

* Usage
* 
* - Connect to subject espMqttIrblaster/sensors with a Mqtt-client
* The device then sends temp/light data like this
* {"temp":23.0625,"light":39}
* - Press a button on an Ir remote, you see a Mqtt-mess with the Ir-code, type and more
* espMqttIrblaster/irrec {"Protocol":"IR","type":"NEC","code":"5EA1D827","bits":32}
* 
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
#include <RCSwitch.h> // RF transmitter

//#include "RemoteDebug.h" // Debug via Telnet, https://github.com/JoaoLopesF/RemoteDebug
//RemoteDebug Debug;

RCSwitch mySwitch = RCSwitch();

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
//struct irdec {
//  String dectype;
//};

// For state machine
long lastMsg = 0;
String temp, hum, curHour, curMinute;


// Mqtt
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_server = "192.168.1.79";

// Mqtt topics
const char* mqtt_sub_topic = "espMqttIrblaster/irsender";
const char* mqtt_pub_topic = "espMqttIrblaster/sensors";
const char* mqtt_irpub_topic = "espMqttIrblaster/irrec";
const char* mqtt_status_topic = "espMqttIrblaster/status";

void setup() {
  // Wifi settings
  char ssid[]="";
  const char* password = "..........";
  
  Serial.begin(115200);
  //Serial.println("Booting");

  StaticJsonBuffer<200> jsonBuffer;

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
    /*
    Serial.print ("Best network is ");
    Serial.println(ssid);
    Serial.println ("Connecting...");
    */
  } 

  // Fixed ssid for testing
  strcpy(ssid, "NETGEAR83");
  
  WiFi.mode(WIFI_STA);
  // Connect to strongest network

  if (WiFi.status() != WL_CONNECTED) {  // FIX FOR USING 2.3.0 CORE (only .begin if not connected)
    // https://github.com/esp8266/Arduino/issues/2186
    WiFi.begin(ssid, password);
  }

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    //Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  WiFi.hostname("espMqttIrBlaster");

  // Mqtt
  //Serial.println("Mqtt");
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // What to do when a Mqtt message arrives
  if (!client.connected()) {
      reconnect();
  }

  // Subscribe to topic
  boolean rc = client.subscribe(mqtt_sub_topic);

  //localip = WiFi.localIP().toString();
  //Serial.print("IP address: ");
  //Serial.println(localip);

  IPAddress ip = WiFi.localIP();
  char buf[16];
  sprintf(buf, "IP:%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3] );

  /*char mess[150];
  String sMess;
  sMess = "Hello from espMqttIrblaster_v2"; 
  sMess.toCharArray(mess, 50);
  client.publish(mqtt_status_topic, mess);  // Wants a char*/
  client.publish(mqtt_status_topic, "espMqttIrblaster_v2");
  client.publish(mqtt_status_topic, buf);
  client.publish(mqtt_status_topic, ssid);
  
  // Initialize irsender
  //Serial.println("IR");
  irsend.begin();
  irrecv.enableIRIn(); // Start the receiver

  // RF receiver
  //pinMode(D5, OUTPUT);
  mySwitch.enableTransmit(D5);

  // Test RF transmitter
/*
  #define CODE_ButtonOn 1052693  
  #define CODE_ButtonOff 1052692  

  delay(500);
  Serial.print("Send code ");
  Serial.println(CODE_ButtonOn);
  mySwitch.send(CODE_ButtonOff, 24);
  delay(2000);
  mySwitch.send(CODE_ButtonOn, 24);
*/

  // DS18B20
  sensors.begin();

  ArduinoOTA.onStart([]() {
    //Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  //Serial.println("Ready");


  if (MDNS.begin("espMqttIrblaster")) {
    //Serial.println("MDNS responder started");
  }

/*  Debug.begin("Telnet_HostName"); 
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.println("Setup done");
  DEBUG_I("This is a information");
*/

}
void loop() {
  //Debug.handle();

  ArduinoOTA.handle();
  
  if (irrecv.decode(&results)) {
    // print() & println() can't handle printing long longs. (uint64_t)
    /*Serial.print("IR rec:");
    serialPrintUint64(results.value, HEX);
    Serial.println("");
    */
    // Decode and send via Mqtt
    dump(&results);

    // Send results via Mqtt
    /*char msg[40];
    sprintf (msg, "{\"Irrec\":%d}", results.value);
    client.publish(mqtt_pub_topic, msg);  
    */
    irrecv.resume();  // Receive the next value
  }
  delay(100);

  // "State machine", check sensor and send values when 2 minutes have passed.
  long now = millis();
  if (now - lastMsg > 120000) {  // Every 2 minutes
  //if (now - lastMsg > 10000) {  // Every 10 seconds
    //DEBUG_I("* Going to read sensors\n");
    lastMsg = now;

    sensors.requestTemperatures(); // Send the command to get temperatures
    float temp = sensors.getTempCByIndex(0);
    //Serial.print("Temp: ");
    //Serial.println(sensors.getTempCByIndex(0)); 

    // Get light level
    int ll = lightlevel(lightsensor); 
    //Serial.print("Light: ");
    //Serial.println(ll);
    
    //For Json output
    StaticJsonBuffer<50> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    char msg[50];
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
    //Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("EspMqttIrblaster", "emonpi", "emonpimqtt2016")) {
      //Serial.println("connected");
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
  /*
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  */
  
  char msg[200]="";
  
  // Extract payload
  String stringPayload = "";
  for (int i = 0; i < length; i++) {
    //Serial.print((char)payload[i]);
    stringPayload += (char)payload[i];
  }
  //Serial.println(stringPayload);

  /* Send a message like this:
  * mosquitto_pub -h <Mqtt server ip> -u '<username>' -P '<password>' -t 'espMqttIrblaster/irsender' 
  * -m '{type:ir|rf,code:20,bits:32}'
  * 
  * mosquitto_pub -h 192.168.1.79 -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/irsender' -m '{t:ir,c:0x5EA1D827,b:32}'
  * mosquitto_pub -h 192.168.1.79 -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/irsender' -m '{t:rf,c:20,b:32}'
  * 
  * 
  * Nexa C3:
  * mosquitto_pub -h 192.168.1.79 -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/irsender' -m '{t:rf,c:1052693,b:24}'
  */
  
  // Decode json
  const size_t bufferSize = JSON_OBJECT_SIZE(3) + 20;
  DynamicJsonBuffer jsonBuffer(bufferSize);
  JsonObject& rootRec = jsonBuffer.parseObject(stringPayload);
  if (!rootRec.success()) {
      //Serial.println("parseObject() failed");
      client.publish(mqtt_pub_topic, "Mqtt parse error");  
      return;
  }
  else {
      client.publish(mqtt_pub_topic, "Mqtt parsed ok");  
  }

  // Get Json values
  String type = rootRec["t"];
  //Serial.println(type);
  unsigned long code = rootRec["c"];
  String scode = rootRec["c"];
  //Serial.println(code);
  String bits = rootRec["b"];
  int ibits = rootRec["b"];
  //Serial.println(bits);
  
  if (type=="rf") {
    //Serial.println("Send rf");
    char cstatus[100];
    sprintf(cstatus,"Send rf = %s", code);
    
    //cstatus.toCharArray(msg,100);
    client.publish(mqtt_status_topic, "Send rf");
    client.publish(mqtt_status_topic, msg);
    mySwitch.send(code, ibits);
    // Debug:
    // mySwitch.send(1052692, 24);
    
  }
  else if (type=="ir") {
    String protocol = rootRec["p"];
    //Serial.println(protocol);
    
    //int iBits = bits.toInt();
    scode.toCharArray(msg,50);
    client.publish(mqtt_status_topic, msg);

    // It works to send the dec equivalent of 0xE0E040BF (=3772793023)
    
    //int iCode = code.toInt();
    if (protocol=="NEC") {
      irsend.sendNEC(code, ibits);
      
    }
    else if (protocol=="SONY") {
      //irsend.sendSony(iCode, iBits);
    }
    else if (protocol=="RC5") {
      //irsend.sendRC5(iCode, iBits);
    }
    else if (protocol=="RC6") {
      //irsend.sendRC6(iCode, iBits);
    }
    else if (protocol=="LG") {
      //irsend.sendLG(iCode, iBits);
      
    }
    else if (protocol=="JVC") {
      
    }
    else {
      client.publish(mqtt_status_topic, "Ir type unknown");  
    }
    //irsend.sendSAMSUNG(iCode, 32);
  }
  else {
    client.publish(mqtt_pub_topic, "Mqtt message error");  
  }
  
  //irsend.sendSAMSUNG(uPayload, 32);
  /*irsend.sendSAMSUNG(551489775, 32);
  irsend.sendLG(551489775, 28);
  irsend.sendLG(0x20DF10EF, 28);
  */

}

void dump(decode_results *results) {
  // Dumps out the decode_results structure.
  // Call this after IRrecv::decode()
  uint16_t count = results->rawlen;

  // Mqtt buffer
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  root["Protocol"] = "IR";
  
  if (results->decode_type == UNKNOWN) {
    //Serial.print("Unknown encoding: ");
    root["type"] = "Unknown";
  } else if (results->decode_type == NEC) {
    //Serial.print("Decoded NEC: ");
    root["type"] = "NEC";
  } else if (results->decode_type == SONY) {
    //Serial.print("Decoded SONY: ");
    root["type"] = "SONY";
  } else if (results->decode_type == RC5) {
    //Serial.print("Decoded RC5: ");
    root["type"] = "RC5";
  } else if (results->decode_type == RC5X) {    // Not supported by ir-sender? https://github.com/markszabo/IRremoteESP8266/blob/master/src/IRsend.cpp
    //Serial.print("Decoded RC5X: ");
    root["type"] = "RC5X";
  } else if (results->decode_type == RC6) {
    //Serial.print("Decoded RC6: ");
    root["type"] = "RC6";
  } else if (results->decode_type == RCMM) {
    //Serial.print("Decoded RCMM: ");
    root["type"] = "RCMM";
  } else if (results->decode_type == PANASONIC) {
    //Serial.print("Decoded PANASONIC - Address: ");
    //Serial.print(results->address, HEX);
    //Serial.print(" Value: ");
    root["type"] = "PANASONIC";
  } else if (results->decode_type == LG) {
    //Serial.print("Decoded LG: ");
    root["type"] = "LG";
  } else if (results->decode_type == JVC) {
    //Serial.print("Decoded JVC: ");
    root["type"] = "JVC";
  } else if (results->decode_type == AIWA_RC_T501) {
    //Serial.print("Decoded AIWA RC T501: ");
    root["type"] = "AIWA";
  } else if (results->decode_type == WHYNTER) {
    //Serial.print("Decoded Whynter: ");
    root["type"] = "Whynter";
  }
  //serialPrintUint64(results->value, 16);
  root["code"] = uint64ToString(results->value, 16);
  /*
  Serial.print(" (");
  Serial.print(results->bits, DEC);
  Serial.println(" bits)");
  */
  root["bits"] = results->bits;

  char msg[100];
  root.printTo((char*)msg, root.measureLength() + 1);
  client.publish(mqtt_irpub_topic, msg);  

  /*
  Serial.print("Raw (");
  Serial.print(count, DEC);
  Serial.print("): ");
  
  
  for (uint16_t i = 1; i < count; i++) {
    if (i % 100 == 0)
      yield();  // Preemptive yield every 100th entry to feed the WDT.
    if (i & 1) {
      Serial.print(results->rawbuf[i] * RAWTICK, DEC);
    } else {
      Serial.write('-');
      Serial.print((uint32_t) results->rawbuf[i] * RAWTICK, DEC);
    }
    Serial.print(" ");
  }
  Serial.println();
  */
}


int lightlevel(int adPin)                       // Measures volts at adPin
{                                            
 return int(analogRead(adPin));
}  
/*
String uint64ToString(uint64_t input, uint8_t base) {
  String result = "";
  // prevent issues if called with base <= 1
  if (base < 2) base = 10;
  // Check we have a base that we can actually print.
  // i.e. [0-9A-Z] == 36
  if (base > 36) base = 10;

  do {
    char c = input % base;
    input /= base;

    if (c < 10)
      c +='0';
    else
      c += 'A' - 10;
    result = c + result;
  } while (input);
return result;
}
*/

