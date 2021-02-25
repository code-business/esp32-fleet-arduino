#include "WiFi.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Arduino_JSON.h>

char AWS_CERT_CA[] = R"(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)";

char AWS_CERT_CRT[1250];
char AWS_CERT_PRIVATE[1800];

// The name of the device. This MUST match up with the name defined in the AWS console
#define DEVICE_NAME "ESP_With_Dynamic_Certs"

// The MQTTT endpoint for the device (unique for each AWS account but shared amongst devices within the account)
#define AWS_IOT_ENDPOINT "a2vy8psrlkjjpu-ats.iot.us-east-1.amazonaws.com"

// The MQTT topic that this device should publish to
#define AWS_IOT_TOPIC "$aws/things/" DEVICE_NAME "/shadow/update"
//#define AWS_IOT_TOPIC "testmqtt"

// How many times we should attempt to connect to AWS
#define AWS_MAX_RECONNECT_TRIES 50

// API Url to fetch certs from
#define API_URL "https://qhzfktym1b.execute-api.us-east-1.amazonaws.com/default/test_fleet_provisioning"

// How many times we should attempt to connect to Wifi
#define WIFI_MAX_RECONNECT_TRIES 5

// Wifi credentials
const char *WIFI_SSID = "BrandWhiz";
const char *WIFI_PASSWORD = "rajiv123";

int hasCerts = 1;
int isWifiConnected = 0;
int wifiConnRetries = 0;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(1024);

String httpGETRequest()
{
  HTTPClient http;

  http.begin(API_URL);

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  String payload = "";
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
  if (httpResponseCode != 200)
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    return "";
  }

  payload = http.getString();
  http.end();
  return payload;
}

void readCertsFromFile()
{
  File crtFile = SPIFFS.open("/crtFile.txt", "r");

  if (!crtFile)
  {
    Serial.println("Failed to open file for reading");
  }
  else
  {
    String fileCon = crtFile.readString();
    if (fileCon.length() == 0)
    {
      Serial.println("=============== Getting Certificates from API ===============");
      getCertsFromAPI();
      return;
    }

    JSONVar certObj = JSON.parse(fileCon);

    strcpy(AWS_CERT_CRT, certObj["certificatePem"]);
    strcpy(AWS_CERT_PRIVATE, certObj["privateKey"]);
  }

  crtFile.close();
}

void connectToAWS()
{
  if (strlen(AWS_CERT_CRT) == 0 || strlen(AWS_CERT_PRIVATE) == 0)
  {
    Serial.println("=============== No certs found ===============");
    hasCerts = 0;
    return;
  }
  else
  {
    hasCerts = 1;

    // Configure WiFiClientSecure to use the AWS certificates we generated
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // Connect to the MQTT broker on the AWS endpoint we defined earlier
    client.begin(AWS_IOT_ENDPOINT, 8883, net);

    // Try to connect to AWS and count how many times we retried.
    int retries = 0;
    Serial.println("=============== Connecting to AWS IOT ===============");

    while (!client.connect(DEVICE_NAME) && retries < AWS_MAX_RECONNECT_TRIES)
    {
      Serial.print(".");
      delay(1000);
      retries++;
    }

    // Make sure that we did indeed successfully connect to the MQTT broker
    // If not we just end the function and wait for the next loop.
    if (!client.connected())
    {
      Serial.println("\n=============== Connection Timeout! ===============");
      return;
    }

    // If we land here, we have successfully connected to AWS!
    // And we can subscribe to topics and send messages.
    Serial.println("\n=============== Connected ===============");
  }
}

void writeToFile(JSONVar certObj)
{

  File crtFile = SPIFFS.open("/crtFile.txt", "w");
  if (!crtFile)
  {
    Serial.println("There was an error opening the file for writing");
    return;
  }

  int bytesWritten = crtFile.print(certObj);

  if (bytesWritten > 0)
  {
    Serial.println("=============== File was written ===============");
    Serial.println(bytesWritten);

    // assign certificates to variables
    strcpy(AWS_CERT_CRT, certObj["certificatePem"]);
    strcpy(AWS_CERT_PRIVATE, certObj["privateKey"]);
  }
  else
  {
    Serial.println("File write failed");
  }
}

void getCertsFromAPI()
{
  int retries = 0;
  String data = "";
  while (data == "" && retries < 10)
  {
    data = httpGETRequest();
    retries++;
    delay(2000);
  }

  if (data == "")
  {
    Serial.println("=============== API is failing ===============");
    return;
  }

  JSONVar certObj = JSON.parse(data);
  writeToFile(certObj);
}

void connectToWiFi()
{
  if (wifiConnRetries <= WIFI_MAX_RECONNECT_TRIES)
  {

    wifiConnRetries++;
    Serial.println("\n=============== Connecting WiFi ===============");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Only try 15 times to connect to the WiFi
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15)
    {
      delay(1000);
      Serial.print(".");
      retries++;
    }

    // If we still couldn't connect to the WiFi, go to deep sleep for a minute and try again.
    if (WiFi.status() != WL_CONNECTED)
    {
      delay(2000);
      Serial.println("\n=============== Retrying WiFi Connection ===============");
      connectToWiFi();
    }
    else
    {
      isWifiConnected = 1;
      Serial.println("\n=============== WiFi Connected ===============");
    }
  }
  else
  {
    Serial.println("\n=============== Please enter proper wifi creds ===============");
  }
}

void sendJsonToAWS()
{
  Serial.println("=============== Publishing to AWS ===============");
  char jsonBuffer[512];
  String RandNum = String(random(0, 20));

  RandNum.toCharArray(jsonBuffer, 512);

  // Publish the message to AWS
  client.publish(AWS_IOT_TOPIC, jsonBuffer);
}

void setup()
{
  Serial.begin(115200);

  connectToWiFi();
  if (isWifiConnected == 1)
  { // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    {
      Serial.println("An Error has occurred while mounting SPIFFS");
      return;
    }
    // for formmating uncomment
    // bool formatted = SPIFFS.format();
    // if (formatted)
    // {
    //   Serial.println("SPIFFS formatted successfully");
    // }
    // else
    // {
    //   Serial.println("Error formatting");
    // }
    readCertsFromFile();
    connectToAWS();
  }
}

void loop()
{
  // put your main code here, to run repeatedly:
  if (isWifiConnected && hasCerts)
  {
    sendJsonToAWS();
    client.loop();
    delay(5000);
  }
}
