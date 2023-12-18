#include <Adafruit_AHTX0.h>
#include <Arduino.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <arduino_secrets.h>

// Sensor information
String sensorID = "";
const char *sensorName = "sensor-zum-1";
const char *macAddress = "30:AE:A4:0E:8C:8C";

// WiFi information
const char *ssid = SECRET_SSID;
const char *pass = SECRET_PASS;

// Server information
String authToken = "";
const char *password = SECRET_PASS_SERVER;
const char serverName[] = "sensor.irealworlds.com";
const int port = 80;

// Sensor information & hardware setup
const int analogPin = 39; // AQI sensor
Adafruit_AHTX0 aht;       // temperature and humidity sensor

WiFiClient wifi;
int status = WL_IDLE_STATUS;

bool ensureConnection(HttpClient &client) {
  if (!client.connected()) {
    Serial.println("Error: Could not connect to server");
    return false;
  }
  return true;
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("");

  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
}

void setupSensor() {
  analogSetAttenuation(ADC_11db);

  if (!aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    while (1)
      delay(10);
  }
  Serial.println("AHT10 or AHT20 found");
}

void login() {
  HttpClient client(wifi, serverName, port);

  Serial.println("Making POST request to login");

  DynamicJsonDocument loginJson(1024);
  loginJson["macAddress"] = macAddress;
  loginJson["password"] = password;

  String postData;
  serializeJson(loginJson, postData);
  Serial.println(postData);

  client.post("/auth/sessions", "application/json", postData);

  if (client.responseStatusCode() == 201) {
    Serial.println("Login successful");
    const char *authTokenJSON = client.responseBody().c_str();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, authTokenJSON);

    sensorID = doc["subject"].as<String>();

    authToken = doc["accessToken"].as<String>();
    Serial.print("Sensor ID(login): ");
    Serial.println(sensorID);
    Serial.print("Auth token: ");
    Serial.println(authToken);
  } else {
    Serial.println("Login failed");
    Serial.println(client.responseStatusCode());
    Serial.println(client.responseBody());
  }
  client.stop(); // Close connection
}

void registerSensor() {
  HttpClient client(wifi, serverName, port);

  Serial.println("Registering sensor");

  DynamicJsonDocument registerJson(1024);
  registerJson["macAddress"] = macAddress;
  registerJson["name"] = sensorName;
  registerJson["password"] = password;

  String postData;
  serializeJson(registerJson, postData);
  Serial.println(postData);

  if (client.post("/sensors", "application/json", postData) != 201) {
    Serial.println("Register failed");
    Serial.println(client.responseStatusCode());
    Serial.println("Trying to login");

    login();
  } else {
    Serial.println("Register successful");

    const char *sensorIDJSON = client.responseBody().c_str();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, sensorIDJSON);
    sensorID = doc["resourceIdentifier"].as<String>();

    Serial.print("Sensor ID(register): ");
    Serial.println(sensorID);
  }
  client.stop(); // Close connection
}

void sendSensorReadings(int aqiIndex, float humidity_f, float temp_f,
                        String sensorID) {
  Serial.println(sensorID);
  if (authToken == nullptr || authToken.isEmpty()) {
    Serial.println("Error: Auth token not set");
    return;
  }

  HttpClient client(wifi, serverName, port);

  // Ensure sensorID is not empty before building the URL
  String url = "/sensors/" + sensorID + "/readings";
  const char *url_c_str = url.c_str();
  Serial.print("\t In sendSensorReadings:");
  Serial.println(url_c_str);

  if (strstr(url_c_str, "//readings") != nullptr) {
    Serial.println("Error: Invalid sensor ID");
    return;
  }

  // Serial.println("\t AuthToken verificare: " + authToken);
  const char *authToken_c_str = authToken.c_str();
  const char *bearerHeader_c_str = String("Bearer " + authToken).c_str();
  Serial.print("\t AuthToken verificare:");
  Serial.println(bearerHeader_c_str);

  // Build post data
  DynamicJsonDocument readingJson(1024);
  readingJson["airQuality"] = aqiIndex;
  readingJson["humidity"] = humidity_f;
  readingJson["temperature"] = temp_f;
  String postData;
  serializeJson(readingJson, postData);

  // Send the request
  client.beginRequest();
  client.post(url_c_str);
  client.sendHeader("Content-Type", "application/json");
  client.sendHeader("Content-Length", postData.length());
  client.sendHeader("Authorization", bearerHeader_c_str);
  client.beginBody();
  client.print(postData);
  client.endRequest();

  // Print the result
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);

  // If authorization fails, try to log in again
  if (statusCode == 401) {
    login();
  }

  client.stop(); // Close connection
}
void setup() {
  Serial.begin(115200);
  delay(2500);

  setupWiFi();
  setupSensor();

  Serial.println("Registering sensor");
  registerSensor();

  Serial.println("Setup done! ✅");
  Serial.println("--------------------");
}

void loop() {
  int aqiIndex = analogRead(analogPin);
  Serial.println(aqiIndex);

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  float humidity_f = humidity.relative_humidity;
  float temp_f = temp.temperature;

  Serial.println("\t In loop " + String(sensorID));
  sendSensorReadings(aqiIndex, humidity_f, temp_f, sensorID);

  Serial.println("Wait 2.5 seconds");
  delay(2500);
}
