// AWS IOT
#include "certs.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>

// ntp
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "WiFi.h"
#include <OneWire.h>

OneWire ds(25);

// Wifi credentials
const char *WIFI_SSID = "alans";
const char *WIFI_PASSWORD = "life2004";

// The name of the device. This MUST match up with the name defined in the AWS console
#define DEVICE_NAME "esp32"

// The MQTTT endpoint for the device (unique for each AWS account but shared amongst devices within the account)
#define AWS_IOT_ENDPOINT "a2scn5mpg6n5wv-ats.iot.eu-central-1.amazonaws.com"

// The MQTT topic that this device should publish to
#define AWS_IOT_TOPIC "/temp"

// How many times we should attempt to connect to AWS
#define AWS_MAX_RECONNECT_TRIES 50

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// deep sleep
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 15       /* Time ESP32 will go to sleep (in seconds) */

RTC_DATA_ATTR int bootCount = 0;

void connectToWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Only try 15 times to connect to the WiFi
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 15)
  {
    delay(500);
    Serial.print(".");
    retries++;
  }

  // If we still couldn't connect to the WiFi, go to deep sleep for a minute and try again.
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("Unable to connect");
    //esp_sleep_enable_timer_wakeup(1 * 60L * 1000000L);
    //esp_deep_sleep_start();
  }
}

void connectToAWS()
{
  // Configure WiFiClientSecure to use the AWS certificates we generated
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  // Try to connect to AWS and count how many times we retried.
  int retries = 0;
  Serial.print("Connecting to AWS IOT");

  while (!client.connect(DEVICE_NAME) && retries < AWS_MAX_RECONNECT_TRIES)
  {
    Serial.print(".");
    delay(100);
    retries++;
  }

  // Make sure that we did indeed successfully connect to the MQTT broker
  // If not we just end the function and wait for the next loop.
  if (!client.connected())
  {
    Serial.println(" Timeout!");
    return;
  }

  // If we land here, we have successfully connected to AWS!
  // And we can subscribe to topics and send messages.
  Serial.println("Connected!");
}

float getTemperature()
{
  int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;

  byte i;

  byte present = 0;

  byte data[12];

  byte addr[8];

  ds.reset_search();

  if (!ds.search(addr))
  {

    Serial.print("No more addresses.\n"); //  "Адресов больше нет.\n")

    ds.reset_search();

    return 0;
  }

  Serial.print("R=");

  for (i = 0; i < 8; i++)
  {

    Serial.print(addr[i], HEX);

    Serial.print(" ");
  }

  if (OneWire::crc8(addr, 7) != addr[7])
  {

    Serial.print("CRC is not valid!\n"); //  "CRC не корректен!\n")

    return 0;
  }

  if (addr[0] == 0x10)
  {

    Serial.print("Device is a DS18S20 family device.\n"); //  "Устройство принадлежит семейству DS18S20.\n")
  }

  else if (addr[0] == 0x28)
  {

    Serial.print("Device is a DS18B20 family device.\n"); //  "Устройство принадлежит семейству DS18B20.\n")
  }

  else
  {

    Serial.print("Device family is not recognized: 0x"); //  "Семейство устройства не распознано.\n")

    Serial.println(addr[0], HEX);

    return 0;
  }

  ds.reset();

  ds.select(addr);

  ds.write(0x44, 1); // запускаем конверсию и включаем паразитное питание

  delay(1000); // 750 миллисекунд может хватить, а может и нет

  // здесь можно использовать ds.depower(), но об этом позаботится сброс

  present = ds.reset();

  ds.select(addr);

  ds.write(0xBE); // считываем scratchpad-память

  Serial.print("P=");

  Serial.print(present, HEX);

  Serial.print(" ");

  for (i = 0; i < 9; i++)
  { // нам нужно 9 байтов

    data[i] = ds.read();

    Serial.print(data[i], HEX);

    Serial.print(" ");
  }

  Serial.print(" CRC=");

  Serial.print(OneWire::crc8(data, 8), HEX);

  Serial.println();

  LowByte = data[0];

  HighByte = data[1];

  TReading = (HighByte << 8) + LowByte;

  SignBit = TReading & 0x8000; // проверяем значение в самом старшем бите

  if (SignBit) // если значение отрицательное

  {

    TReading = (TReading ^ 0xffff) + 1;
  }

  Tc_100 = (6 * TReading) + TReading / 4; // умножаем на (100 * 0.0625) или 6.25

  Whole = Tc_100 / 100; // отделяем друг от друга целую и дробную порции

  Fract = Tc_100 % 100;

  if (SignBit) // если число отрицательное

  {

    Serial.print("-");
  }

  Serial.print(Whole);

  Serial.print(".");

  if (Fract < 10)

  {

    Serial.print("0");
  }

  Serial.print(Fract);

  Serial.print("\n");

  return Whole;
}

void sendJsonToAWS()
{
  timeClient.update();
  StaticJsonDocument<128> jsonDoc;
  JsonObject stateObj = jsonDoc.createNestedObject("state");
  JsonObject reportedObj = jsonDoc.createNestedObject("reported");

  // Write the temperature & humidity. Here you can use any C++ type (and you can refer to variables)
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  // float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  // float t = dht.readTemperature();
  reportedObj["temperature"] = getTemperature();
  //reportedObj["humidity"] = 65;
  reportedObj["wifi_strength"] = WiFi.RSSI();
  reportedObj["timestamp"] = timeClient.getEpochTime();

  // Create a nested object "location"
  JsonObject locationObj = reportedObj.createNestedObject("location");
  locationObj["name"] = "Melashchenko";

  Serial.println("Publishing message to AWS...");
  //serializeJson(doc, Serial);
  char jsonBuffer[512];
  serializeJson(jsonDoc, jsonBuffer);

  client.publish(AWS_IOT_TOPIC, jsonBuffer);
}

void setup()
{
  Serial.begin(9600);

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  connectToWiFi();
  connectToAWS();

  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(7200);

  /*
  First we configure the wake up source
  We set our ESP32 to wake up every 5 seconds
  */
  //esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  //Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +   " Seconds");
}

void loop()
{
  timeClient.update();
  sendJsonToAWS();
  //client.loop();
  delay(5000);
}