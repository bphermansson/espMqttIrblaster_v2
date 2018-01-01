# espMqttIrblaster_v2
Esp-based multi device for sending and transmitting IR, sending RF, measure temperature, light and more. 

Work in progress, not finished!

To transmit a RF-code to for example a Nexa switch:
Start by using an Arduino and a RF-receiver. 
Use ReceiveDemo_Simple from the rc-switch library to receive RF-codes. Press a button on the remote and note the codes received, both for off and on. Also note the length?
It can be like '20' and '21'.
These codes can then be used in a mqtt message:

mosquitto_pub -h 192.168.1.79 -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/irsender' -m '{t:rf,c:20,b:32}'

't:rf' tells the device to send by radio.
'c:nn' is the code.
'b:nn' is the length 
'c' and 'b' is retrieved from ReceiveDemo_Simple. 

mosquitto_pub -h 192.168.1.79 -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/irsender' -m '{t:ir,p:NEC,c:20,b:32}'

Here 'p' is the protocol used by the ir remote.
Find the ir values by listening to the blasters Mqtt-stream:
mosquitto_sub -h 192.168.1.79 -v -u 'emonpi' -P 'emonpimqtt2016' -t 'espMqttIrblaster/#'




