#define ED_WIFI_SSID "XXX"
#define ED_WIFI_PASS "YYY"

#define ED_MQTT_URI "mqtts://raspi00:8883"
#define ED_MQTT_USERNAME "WWW"
#define ED_MQTT_PASSWORD "PPP!"

/*template for the wifi credential which need to be loaded at boot. Actual values to be defined in ED_wifi.h
Specify in sequence SSID, password, C [connectable] or U [unconnectable]
e.g.
{"Edolis", "YYYYY", "C"},
*/
#define ED_WIFI_CREDENTIALS   {"XXX", "YYYY", "C"}, {"XXX1", "YYYY1", "C"}