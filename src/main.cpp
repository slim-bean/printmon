#include <Arduino.h>
#include <SensirionI2CSen5x.h>
#include <Wire.h>
#include <ArduinoOTA.h>
#include <GrafanaLoki.h>
#include <PrometheusArduino.h>
#include "config.h"
#include "certificates.h"

// For webserver
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// The used commands use up to 48 bytes. On some Arduino's the default buffer
// space is not large enough
#define MAXBUF_REQUIREMENT 48

#if (defined(I2C_BUFFER_LENGTH) &&                 \
     (I2C_BUFFER_LENGTH >= MAXBUF_REQUIREMENT)) || \
    (defined(BUFFER_LENGTH) && BUFFER_LENGTH >= MAXBUF_REQUIREMENT)
#define USE_PRODUCT_INFO
#endif

// Create a transport and client object for sending our data.
PromLokiTransport transport;
LokiClient loki(transport);
PromClient prom(transport);

SensirionI2CSen5x sen5x;

AsyncWebServer server(80);

// Report sensor
#define S_LENGTH 150
LokiStream lokiStream(1, S_LENGTH, "{job=\"printmon\",type=\"sensor\"}");
LokiStreams streams(1);

TimeSeries pm1(1, "pm1", "{job=\"printmon\"}");
TimeSeries pm2_5(1, "pm2_5", "{job=\"printmon\"}");
TimeSeries pm4(1, "pm4", "{job=\"printmon\"}");
TimeSeries pm10(1, "pm10", "{job=\"printmon\"}");
TimeSeries voc(1, "voc", "{job=\"printmon\"}");
TimeSeries temp(1, "temp", "{job=\"printmon\"}");
TimeSeries humidity(1, "humidity", "{job=\"printmon\"}");
WriteRequest series(7);

void printModuleVersions()
{
  uint16_t error;
  char errorMessage[256];

  unsigned char productName[32];
  uint8_t productNameSize = 32;

  error = sen5x.getProductName(productName, productNameSize);

  if (error)
  {
    Serial.print("Error trying to execute getProductName(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  else
  {
    Serial.print("ProductName:");
    Serial.println((char *)productName);
  }

  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  bool firmwareDebug;
  uint8_t hardwareMajor;
  uint8_t hardwareMinor;
  uint8_t protocolMajor;
  uint8_t protocolMinor;

  error = sen5x.getVersion(firmwareMajor, firmwareMinor, firmwareDebug,
                           hardwareMajor, hardwareMinor, protocolMajor,
                           protocolMinor);
  if (error)
  {
    Serial.print("Error trying to execute getVersion(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  else
  {
    Serial.print("Firmware: ");
    Serial.print(firmwareMajor);
    Serial.print(".");
    Serial.print(firmwareMinor);
    Serial.print(", ");

    Serial.print("Hardware: ");
    Serial.print(hardwareMajor);
    Serial.print(".");
    Serial.println(hardwareMinor);
  }
}

void printSerialNumber()
{
  uint16_t error;
  char errorMessage[256];
  unsigned char serialNumber[32];
  uint8_t serialNumberSize = 32;

  error = sen5x.getSerialNumber(serialNumber, serialNumberSize);
  if (error)
  {
    Serial.print("Error trying to execute getSerialNumber(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  else
  {
    Serial.print("SerialNumber:");
    Serial.println((char *)serialNumber);
  }
}

void setup()
{
  Serial.begin(115200);
  // Wait 5s for serial connection or continue without it
  // some boards like the esp32 will run whether or not the
  // serial port is connected, others like the MKR boards will wait
  // for ever if you don't break the loop.
  uint8_t serialTimeout = 0;
  while (!Serial && serialTimeout < 50)
  {
    delay(100);
    serialTimeout++;
  }

  // Setup Fan PWM port
  ledcSetup(0, 25000, 8);
  ledcAttachPin(9, 0);
  ledcWrite(0, 0);
  pinMode(10, OUTPUT);
  digitalWrite(10, LOW);

  transport.setWifiSsid(WIFI_SSID);
  transport.setWifiPass(WIFI_PASS);
  transport.setNtpServer(NTP);
  transport.setUseTls(true);
  transport.setCerts(lokiCert, strlen(lokiCert));
  transport.setDebug(Serial); // Remove this line to disable debug logging of the transport layer.
  if (!transport.begin())
  {
    Serial.println(transport.errmsg);
    while (true)
    {
    };
  }

  // Configure the client
  loki.setUrl(URL);
  loki.setPath(PATH);
  loki.setPort(PORT);
  loki.setDebug(Serial); // Remove this line to disable debug logging of the loki.
  if (!loki.begin())
  {
    Serial.println(loki.errmsg);
    while (true)
    {
    };
  }

  // Configure the client
  prom.setUrl(PROM_URL);
  prom.setPath((char *)PROM_PATH);
  prom.setPort(PORT);
  prom.setDebug(Serial); // Remove this line to disable debug logging of the client.
  if (!prom.begin())
  {
    Serial.println(prom.errmsg);
    while (true)
    {
    };
  }

  ArduinoOTA.setPassword(OTA_PASS);

  ArduinoOTA
      .onStart([]()
               {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
      .onEnd([]()
             { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
                  { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error)
               {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

  ArduinoOTA.begin();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "Hello, world"); });
  // Send a GET request to <IP>/fan?speed=<speed>
  server.on("/fan", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        String speed;
        if (request->hasParam("speed")) {
            speed = request->getParam("speed")->value();
        } else {
            speed = "0";
        }
        long speedNum = speed.toInt();
        ledcWrite(0, speedNum);
        if (speedNum == 0) {
          digitalWrite(10, LOW);
        } else {
          digitalWrite(10, HIGH);
        }
        request->send(200, "text/plain", "Speed set to: " + speed); });
  server.begin();

  Wire.begin();
  Wire.setClock(500000L);

  sen5x.begin(Wire);

  uint16_t error;
  char errorMessage[256];
  error = sen5x.deviceReset();
  if (error)
  {
    Serial.print("Error trying to execute deviceReset(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

// Print SEN55 module information if i2c buffers are large enough
#ifdef USE_PRODUCT_INFO
  printSerialNumber();
  printModuleVersions();
#endif

  // Start Measurement
  error = sen5x.startMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute startMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  streams.addStream(lokiStream);
  streams.setDebug(Serial);

  series.addTimeSeries(pm1);
  series.addTimeSeries(pm2_5);
  series.addTimeSeries(pm4);
  series.addTimeSeries(pm10);
  series.addTimeSeries(voc);
  series.addTimeSeries(temp);
  series.addTimeSeries(humidity);
  series.setDebug(Serial);
}

void loop()
{
  ArduinoOTA.handle();

  uint16_t error;
  char errorMessage[256];
  char lokiMsg[S_LENGTH] = {'\0'};

  // Read Measurement
  float massConcentrationPm1p0;
  float massConcentrationPm2p5;
  float massConcentrationPm4p0;
  float massConcentrationPm10p0;
  float ambientHumidity;
  float ambientTemperature;
  float vocIndex;
  float noxIndex;

  // We get a fair number of CRC errors, not sure why but try a few times to get a reading.
  for (uint8_t i = 0; i <= 5; i++)
  {
    error = sen5x.readMeasuredValues(
        massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0,
        massConcentrationPm10p0, ambientHumidity, ambientTemperature, vocIndex,
        noxIndex);

    if (error)
    {
      Serial.print("Error trying to execute readMeasuredValues(): ");
      errorToString(error, errorMessage, 256);
      Serial.println(errorMessage);
      delay(1);
    }
    else
    {
      break;
    }
  }

  Serial.print("MassConcentrationPm1p0:");
  Serial.print(massConcentrationPm1p0);
  Serial.print("\t");
  Serial.print("MassConcentrationPm2p5:");
  Serial.print(massConcentrationPm2p5);
  Serial.print("\t");
  Serial.print("MassConcentrationPm4p0:");
  Serial.print(massConcentrationPm4p0);
  Serial.print("\t");
  Serial.print("MassConcentrationPm10p0:");
  Serial.print(massConcentrationPm10p0);
  Serial.print("\t");
  Serial.print("AmbientHumidity:");
  if (isnan(ambientHumidity))
  {
    Serial.print("n/a");
  }
  else
  {
    Serial.print(ambientHumidity);
  }
  Serial.print("\t");
  Serial.print("AmbientTemperature:");
  if (isnan(ambientTemperature))
  {
    Serial.print("n/a");
  }
  else
  {
    Serial.print(ambientTemperature);
  }
  Serial.print("\t");
  Serial.print("VocIndex:");
  if (isnan(vocIndex))
  {
    Serial.print("n/a");
  }
  else
  {
    Serial.print(vocIndex);
  }
  Serial.print("\t");
  Serial.print("NoxIndex:");
  if (isnan(noxIndex))
  {
    Serial.println("n/a");
  }
  else
  {
    Serial.println(noxIndex);
  }

  snprintf(lokiMsg, S_LENGTH, "msg=sen54 pm1=%.2f pm2_5=%.2f pm4=%.2f pm10=%.2f voc=%.2f hum=%.2f temp=%.2f rssi=%d", massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0, massConcentrationPm10p0, vocIndex, ambientHumidity, ambientTemperature, WiFi.RSSI());
  if (!lokiStream.addEntry(loki.getTimeNanos(), lokiMsg, strlen(lokiMsg)))
  {
    Serial.println(lokiStream.errmsg);
  }

  int64_t ptime = transport.getTimeMillis();
  if (!pm1.addSample(ptime, massConcentrationPm1p0))
  {
    Serial.println(pm1.errmsg);
  }
  if (!pm2_5.addSample(ptime, massConcentrationPm2p5))
  {
    Serial.println(pm2_5.errmsg);
  }
  if (!pm4.addSample(ptime, massConcentrationPm4p0))
  {
    Serial.println(pm4.errmsg);
  }
  if (!pm10.addSample(ptime, massConcentrationPm10p0))
  {
    Serial.println(pm10.errmsg);
  }
  if (!voc.addSample(ptime, vocIndex))
  {
    Serial.println(voc.errmsg);
  }
  if (!temp.addSample(ptime, ambientTemperature))
  {
    Serial.println(temp.errmsg);
  }
  if (!humidity.addSample(ptime, ambientHumidity))
  {
    Serial.println(humidity.errmsg);
  }

  // Send the message, we build in a few retries as well.
  uint64_t start = millis();
  for (uint8_t i = 0; i <= 5; i++)
  {
    PromClient::SendResult res = prom.send(series);
    if (!res == PromClient::SendResult::SUCCESS)
    {
      Serial.println(prom.errmsg);
      delay(250);
    }
    else
    {
      // Batches are not automatically reset so that additional retry logic could be implemented by the library user.
      // Reset batches after a succesful send.
      pm1.resetSamples();
      pm2_5.resetSamples();
      pm4.resetSamples();
      pm10.resetSamples();
      voc.resetSamples();
      temp.resetSamples();
      humidity.resetSamples();
      uint32_t diff = millis() - start;
      Serial.print("Prom send succesful in ");
      Serial.print(diff);
      Serial.println("ms");
      break;
    }
  }

  // Send the message, we build in a few retries as well.
  uint64_t lokiStart = millis();
  for (uint8_t i = 0; i <= 5; i++)
  {
    LokiClient::SendResult res = loki.send(streams);
    if (res != LokiClient::SendResult::SUCCESS)
    {
      // Failed to send
      Serial.println(loki.errmsg);
      delay(250);
    }
    else
    {
      lokiStream.resetEntries();
      uint32_t diff = millis() - lokiStart;
      Serial.print("Loki send succesful in ");
      Serial.print(diff);
      Serial.println("ms");
      break;
    }
  }
  uint64_t delayms = 1000 - (millis() - start);
  // If the delay is longer than 5000ms we likely timed out and the send took longer than 5s so just send right away.
  if (delayms > 1000)
  {
    delayms = 0;
  }
  Serial.print("Sleeping ");
  Serial.print(delayms);
  Serial.println("ms");
  delay(delayms);
}
/*
MassConcentrationPm1p0:4.10     MassConcentrationPm2p5:4.30     MassConcentrationPm4p0:4.30     MassConcentrationPm10p0:4.30    AmbientHumidity:50.05   AmbientTemperature:24.72        VocIndex:13.00  NoxIndex:n/a
Begin serialization: Free Heap: 205872
Bytes used for serialization: 387
After serialization: Free Heap: 205872
After Compression Init: Free Heap: 205344
Required buffer size for compression: 483
Compressed Len: 177
After Compression: Free Heap: 205872
Sending To Prometheus
Connection already open
Sent, waiting for response
Prom Send Succeeded
Server: nginx/1.14.1
Date: Mon, 16 May 2022 12:26:46 GMT
Content-Length: 0
Connection: keep-alive

Prom send succesful in 251ms
Begin serialization: Free Heap: 205872
Bytes used for serialization: 141
After serialization: Free Heap: 205872
After Compression Init: Free Heap: 205344
Required buffer size for compression: 196
Compressed Len: 143
After Compression: Free Heap: 205872
Sending To Loki
Connection already open
Sent, waiting for response
Loki Send Succeeded
Server: nginx/1.14.1
Date: Mon, 16 May 2022 12:26:47 GMT
Connection: keep-alive

Loki send succesful in 250ms
Sleeping 498ms
*/