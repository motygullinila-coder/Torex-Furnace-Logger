#include <Adafruit_MAX31855.h>

/**

  MAX-31855 - ESP32_DEV_KIT: 
  GND       - GND
  VIN       - 3v3
  CLK       - GPIO_25
  CS        - GPIO_17
  DO        - GPIO_19
**/

#define PIN_CLK 25
#define PIN_CS 17
#define PIN_DO 19
Adafruit_MAX31855 thermocouple(PIN_CLK, PIN_CS, PIN_DO);

#define RX_PIN 4
#define TX_PIN 16
#define BAUD_RATE 115200

float temp = 0.0;

void initial_trans_serial_port() {
  Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(500);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  initial_trans_serial_port();
}

void loop() {
  temp = thermocouple.readCelsius();
  Serial1.println(String(temp));

  Serial.print("Temp: ");
  Serial.println(temp);

  delay(1000);
  // delay(1000);
}
