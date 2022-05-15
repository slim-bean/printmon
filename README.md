

`'PinStatus' does not name a type` Remove the WiFiNINA library, [found this issue](https://github.com/arduino-libraries/WiFiNINA/issues/184)

Dependency Graph
|-- <Sensirion Core> 0.5.3
|   |-- <Wire> 1.0.1
|-- <Sensirion I2C SEN5X> 0.2.0
|   |-- <Wire> 1.0.1
|   |-- <Sensirion Core> 0.5.3
|   |   |-- <Wire> 1.0.1
|-- <GrafanaLoki> 0.2.2
|   |-- <PromLokiTransport> 0.2.2
|   |   |-- <WiFi> 1.0
|   |   |-- <ArduinoBearSSL> 1.7.2
|   |-- <WiFi> 1.0
|   |-- <ArduinoBearSSL> 1.7.2
|   |-- <SnappyProto> 0.1.2
|   |-- <ArduinoHttpClient> 0.4.0
|-- <ArduinoOTA> 1.0
|   |-- <Update> 1.0
|   |-- <WiFi> 1.0
|   |-- <ESPmDNS> 1.0
|   |   |-- <WiFi> 1.0
|-- <WiFi> 1.0
|-- <Wire> 1.0.1