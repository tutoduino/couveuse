// Egg incubator
// https://tutoduino.fr/
// Copyleft 2023

#include <ESP8266WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// The file credentials.h contains SSID and password
#include "credentials.h"

// OLED SSD1306
#define LINE_TEMP1 00
#define LINE_TEMP2 10
#define LINE_TEMP_MEAN 20
#define LINE_HUM 30
#define LINE_TEMP_HEAT 40
#define LINE_RELAY 50
#define OLED_RESET -1          // No reset
#define I2C_ADDRESS_OLED 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Internal LED of NodeMCU is D4
#define LED_INTERNE_NODEMCU D4

// Internal FLASH button is D3
#define INTERNAL_FLASH_BUTTON D3

// DHT11 sensor
#define TEMP_SENSOR_1_TYPE DHT11
#define TEMP_SENSOR_1_PIN D10
DHT dht(TEMP_SENSOR_1_PIN, TEMP_SENSOR_1_TYPE);

// DHT22 sensor
#define TEMP_SENSOR_2_TYPE DHT22
#define TEMP_SENSOR_2_PIN D6
DHT dht2(TEMP_SENSOR_2_PIN, TEMP_SENSOR_2_TYPE);

// Relay
#define RELAY_PIN D7

// Data sensor signal is D5
#define ONE_WIRE_BUS D5

// Heater and temperature
#define TARGET_TEMPERATURE 37.7
#define MAX_TEMPERATURE_HEATER 55
#define MIN_TEMPERATURE_HEATER 52
float targetHeaterTemp;
float previousMeanTemp;
unsigned long myTime;

// Setup a oneWire instance to communicate with any OneWire device
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// WIFI parameters
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWD;

// Redis client
#define REDIS_ADDR "192.168.1.21"
#define REDIS_PORT 6379
#define REDIS_PASSWORD REDIS_PASSWD
WiFiClient redisConn;
bool isRedisOk;

// WIFI setup
//
void setup_wifi() {
  // Connexion au wifi en mode station
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  display.setCursor(0, 10);
  display.println("WiFi ok");
  display.display();
}

// OLED screen setup
//
void setupOled() {
  display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS_OLED);
  display.display();
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Tutoduino"));
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print(F("Apprendre"));
  display.setCursor(0, 30);
  display.print(F("l'electronique"));
  display.setCursor(0, 40);
  display.print(F("avec un Arduino"));
  display.display();
  delay(200);
  display.clearDisplay();
}

// Redis client setup
//
void setupRedis() {
  isRedisOk = false;
  if (!redisConn.connect(REDIS_ADDR, REDIS_PORT)) {
    //Serial.println("Failed to connect to the Redis server!");
    return;
  }

  redisConn.printf("AUTH %s\n", REDIS_PASSWORD);
  while (!redisConn.available()) {
    delay(10);
  }
  String st = redisConn.readStringUntil('\n');

  if (st == "+OK\r") {
    display.setCursor(0, 20);
    display.println("Redis OK");
    display.display();
    //Serial.println("Connected to the Redis server!");
  } else {
    //Serial.printf("Failed to authenticate to the Redis server! Errno: %s\n", st);
    return;
  }
  display.setCursor(0, 20);
  display.println("Redis ok");
  display.display();
  isRedisOk = true;
}

// Check if Redis client is connected
//
void checkRedis() {
  display.setCursor(64, LINE_RELAY);
  if (isRedisOk) {
    if (!redisConn.connected()) {
      isRedisOk = false;
    }
  } else {
    display.println("Redis KO");
  }
}

void sendHeaterToRedis(bool heater) {
  if (isRedisOk) {
    redisConn.printf("TS.ADD heater * %d\n", heater);
    redisConn.readStringUntil('\n');
  }
}

void sendErrorStatusToRedis(const char* status) {
  if (isRedisOk) {
    redisConn.printf("SET error \"%s\"\n", status);
    redisConn.readStringUntil('\n');
  }
}

void sendTemp1ToRedis(float temp1) {
  if (isRedisOk) {
    redisConn.printf("TS.ADD temp1 * %f\n", temp1);
    redisConn.readStringUntil('\n');
  }
}


void sendTemp2ToRedis(float temp2) {
  if (isRedisOk) {
    redisConn.printf("TS.ADD temp2 * %f\n", temp2);
    redisConn.readStringUntil('\n');
  }
}

void sendHumToRedis(float hum) {
  if (isRedisOk) {
    redisConn.printf("TS.ADD hum * %f\n", hum);
    redisConn.readStringUntil('\n');
  }
}

void sendTempHeaterToRedis(float tempHeater) {
  if (isRedisOk) {
    redisConn.printf("TS.ADD tempHeater * %f\n", tempHeater);
    redisConn.readStringUntil('\n');
  }
}

bool getTemp(DHT &sensor, float *temp) {
  *temp = sensor.readTemperature();
  if (isnan(*temp)) {
    return false;
  }
  return true;
}

bool getHum(DHT &sensor, float *hum) {
  *hum = sensor.readHumidity();
  if (isnan(*hum)) {
    return false;
  }
  return true;
}

bool getTemp(DallasTemperature &sensor, float *temp) {
  sensor.requestTemperatures(); // Send the command to get temperatures
  *temp = sensors.getTempCByIndex(0);

  if (*temp == DEVICE_DISCONNECTED_C) {
    return false;;
  }
  return true;
}

void setupTemp() {
  targetHeaterTemp = 50.0;
  myTime = millis();
  getTemp(dht2, &previousMeanTemp);
}

void computeTemp(float meanTemp, float *currentTarget) {
  // compute every 8 minutes
  if ((millis() - myTime) >= 480000) {
    myTime = millis();
    if (abs(previousMeanTemp - meanTemp) < 0.2) {
      // we have a stable mean temperature since 8 minutes
      // We increase heater temperature with value to reach target temperature
      *currentTarget += (TARGET_TEMPERATURE - meanTemp);

    }
    else {
      // we are not yet stable, do not change the currentTarget temperature
    }
    previousMeanTemp = meanTemp;
  }
}


//
// Setup
//
void setup() {
  setupOled();
  setup_wifi();
  setupRedis();
  dht.begin();
  dht2.begin();
  sensors.begin();
  pinMode(LED_INTERNE_NODEMCU, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(INTERNAL_FLASH_BUTTON, INPUT_PULLUP);
  setupTemp();
}

//
// Loop
//
void loop() {
  int button;
  float temp1, temp2, tempHeater, tempMean, hum;
  bool errorTemp1 = false, errorTemp2 = false, errorTempHeater = false;
  bool isHeating, heatingRequired;
  bool errorRaised = false;

  display.clearDisplay();

  button = digitalRead(INTERNAL_FLASH_BUTTON);

  checkRedis();

  display.setCursor(0, LINE_TEMP1);
  if (!getTemp(dht, &temp1)) {
    display.println("Temp1 disconnected");
    errorTemp1 = true;
    sendErrorStatusToRedis("Sensor 1 disconnected");
    errorRaised = true;
  } else {
    display.println("Temp1 = " + String(temp1));
    sendTemp1ToRedis(temp1);
  }

  display.setCursor(0, LINE_TEMP2);
  if (!getTemp(dht2, &temp2)) {
    display.println("Temp2 disconnected");
    errorTemp2 = true;
    sendErrorStatusToRedis("Sensor 2 disconnected");
    errorRaised = true;

  } else {
    display.println("Temp2 = " + String(temp2));
    sendTemp2ToRedis(temp2);
  }

  if (errorTemp1 && errorTemp2) {
    // Both temperature sensors disconnected
    digitalWrite(RELAY_PIN, HIGH); // Heater if OFF when relay pin is HIGH
    display.setCursor(0, LINE_RELAY);
    display.println("Relay OFF");
    sendHeaterToRedis(false);
    display.display();
    return;
  }

  display.setCursor(0, LINE_HUM);
  if (!getHum(dht2, &hum)) {
    display.println("Temp2 disconnected");
    errorTemp2 = true;
    sendErrorStatusToRedis("Sensor 2 disconnected");
    errorRaised = true;

  } else {
    display.println("Hum = " + String(hum));
    sendHumToRedis(hum);
  }

  // compute average temperature
  if (errorTemp1) {
    tempMean = temp2;
  } else if (errorTemp2) {
    tempMean = temp1;
  } else {
    tempMean = (temp1 + temp2) / 2;
  }

  display.setCursor(0, LINE_TEMP_MEAN);
  display.println("MeanTemp = " + String(tempMean));
  computeTemp(temp2, &targetHeaterTemp);
  display.setCursor(0, LINE_RELAY);
  display.print("Heat target = ");
  display.println(targetHeaterTemp);

  display.setCursor(0, LINE_TEMP_HEAT);
  if (!getTemp(sensors, &tempHeater)) {
    display.println("TempHeat disconnected");
    errorTempHeater = true;
  } else {
    display.println("Heat temp = " + String(tempHeater));
    sendTempHeaterToRedis(tempHeater);
  }

  if (errorTempHeater) {
    // Heater temperature sensor disconnected
    digitalWrite(LED_INTERNE_NODEMCU, HIGH);
    digitalWrite(RELAY_PIN, HIGH);
    sendHeaterToRedis(false);
    sendErrorStatusToRedis("Heater temp disconnected");
    errorRaised = true;
    display.display();
    return;
  }

  // Check if heating is required to reach the target temperature
  if (tempHeater < targetHeaterTemp) {
    heatingRequired = true;
  } else {
    heatingRequired = false;
  }

  // Ensure that heater temperature never exceed maximum
  if (heatingRequired) {
    // Turn on the heater
    digitalWrite(RELAY_PIN, LOW); // Heater ON when RELAY PIN is LOW
    digitalWrite(LED_INTERNE_NODEMCU, LOW);

    if (digitalRead(RELAY_PIN) == LOW) {
      digitalWrite(LED_INTERNE_NODEMCU, LOW);
      sendHeaterToRedis(true);
    } else {
      digitalWrite(LED_INTERNE_NODEMCU, HIGH);
      sendHeaterToRedis(false);
    }
  } else {
    // Turn off the heater
    digitalWrite(RELAY_PIN, HIGH); // Heater OFF when RELAY PIN is HIGH
    digitalWrite(LED_INTERNE_NODEMCU, HIGH);
    sendHeaterToRedis(false);
  }

  if (!errorRaised) {
    sendErrorStatusToRedis("");
  }

  display.display();

  delay(1000);
}
