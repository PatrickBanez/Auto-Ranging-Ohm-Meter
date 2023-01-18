// Created by: Patrick Banez and Gabe Hearon
// Date: 11/15/2022
//
// This code is for an auto-ranging ohm meter. The resistances measure can range
// from 1 ohm to 1.5M ohm. A 3-1/2 digit resistance value is displayed on an I2C LCD display.
// A running average of the resistance is used as the value for the unknown resistor.
// 
// Steps:
//  1. Take differential voltages of unknown resistor and known resistance:
//    a. if voltage of unknown resistor is less than more than 75% VDD,
//        add a resistor in parallel to the known resistance and remeasure the voltage.
//    b. adjust the gain of the ADC to the appropriate value based on the voltage of the
//        unknown resistor's voltage measurement.
//            2/3x gain @  +/- 6.144V   1 bit = 0.1875mV (default)
//            1x gain   @  +/- 4.096V   1 bit = 0.125mV
//            2x gain   @  +/- 2.048V   1 bit = 0.0625mV
//            4x gain   @  +/- 1.024V   1 bit = 0.03125mV
//            8x gain   @  +/- 0.512V   1 bit = 0.015625mV
//            16x gain  @  +/- 0.256V   1 bit = 0.0078125mV
//  2. Calculate current through circuit using the known resistance and its voltage.
//  3. Use the calculated current to calculate the resistance of the unknown resistor.

#include <Adafruit_ADS1X15.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

#define res_100k 2  // 100 kOhm resistor MOSFET to digital pin 2
#define res_10k 3   // 10 kOhm resistor MOSFET to digital pin 3
#define res_1k 4    // 1 kOhm resistor MOSFET to digital pin 4
#define res_220 5   // 220 Ohm resistor MOSFET to digital pin 5

Adafruit_ADS1115 ads; // ADS1115, 16-bit ADC
LiquidCrystal_I2C lcd(0x27, 16, 2);
  
volatile double unknownResistance, knownResistance;              // resistance values
volatile double unknownVoltage, knownVoltage, referenceVoltage;  // voltage values
const uint32_t RESISTOR_1M = 990090;                             // 1M ohm resistor
const float LEAD_RESISTANCE = 0.08;                              // resistance of test leads
const int SAMPLE_RATE = 1;                                       // 1 ms (1000 Hz) sample rate
volatile int lastSample;                                         // time of last sample update
const int DISPLAY_RATE = 100;                                    // 100 ms (10 Hz) display rate
volatile int lastDisplay;                                        // time of last display update
volatile double current;                                         // current value (A)


void setup()
{
  Serial.begin(9600);
  
  // LCD setup
  lcd.init();
  lcd.backlight();

  // set MOSFET pins
  pinMode(res_100k, OUTPUT);
  digitalWrite(res_100k, LOW);
  pinMode(res_10k, OUTPUT);
  digitalWrite(res_10k, LOW);
  pinMode(res_1k, OUTPUT);
  digitalWrite(res_1k, LOW);
  pinMode(res_220, OUTPUT);
  digitalWrite(res_220, LOW);
  
  if (!ads.begin()) {
    lcd.print("ADC Fail");
    while (1);
  }

  knownResistance = RESISTOR_1M;
  lastSample = 0;
  lastDisplay = 0;
}

// Read voltages, calculate current, calculate the unknown resistance, and
// display the calculated unknown resistance on the LCD at the SAMPLE_RATE.
void loop()
{
  int currentTime = millis();
  if ((currentTime - lastSample) >= SAMPLE_RATE)  // sampling
  {
    lastSample = currentTime;
    readVoltages();
    calculateCurrent();
    calculateResistance();
  }
  if ((currentTime - lastDisplay) >= DISPLAY_RATE)  // displaying
  {
    lastDisplay = currentTime;
    displayMeasurement();
  }
}

// Send the calculated resistor value to the LCD for display
void displayMeasurement()
{
  lcd.clear();
  if (unknownResistance < 0 || unknownResistance >= (1.5 * pow(10, 6)))  // no resistor or resistor >= 1.5 Mohm
  {
    unknownResistance = 0;  // reset unknown resistance
    lcd.setCursor(7,0);
    lcd.print("OL");
    lcd.setCursor(0,1);
    lcd.print("Range: 0 - 1.5M");
    return;
  }
  lcd.setCursor(0,0);
  double resistance = unknownResistance - LEAD_RESISTANCE;
  if ((resistance >= 5.0*pow(10,5))) resistance *= 1.025; // adjust higher end reading by 2.5%
  lcd.print(String(resistance) + " ohms");
}

// Calculate the currents through the unknown resistor by calculating the current in the known
// resistor(s).
// Ohm's Law: V=I/R
void calculateCurrent()
{
  current = knownVoltage/knownResistance;
}

// Calculate the unknown resistor value using the current calculation and unknownVoltage
// measurement.
// Ohm's Law: V=I/R
void calculateResistance()
{
  double calculatedResistance = unknownVoltage/current;
  unknownResistance = (unknownResistance + calculatedResistance) / 2;
}

// Read the reference voltage, known voltage (circuit resistors), and unknown voltage (resistor being tested).
// Reset the known resistance to the 1 Mohm plus miscellaneous resistances before conducting voltage
// measurements. To get an accurate reading for the unknown voltage, the drop across the known voltage should
// not be more than 75% of the reference voltage -- this 75% is arbitrary and can be changed. Add resistors in
// parallel to decrease the voltage drop of the known resistance to below 75% of the reference voltage.
void readVoltages()
{
  static const uint32_t RESISTANCE_VALUES[] = {    220   ,   1000    ,   9997   ,   100078    };  // known resistance values
  static byte openSwitches = 4; // number of MOSFETs open
  static const byte NUMBER_OF_SWITCHES = 4; // max number of MOSFETs
  static const byte SWITCHES[] = {   res_220    ,   res_1k    ,   res_10k   ,   res_100k    }; // MOSFET pins

  // reset known resistance by removing parallel resistors
  while (openSwitches < NUMBER_OF_SWITCHES)
  {
    digitalWrite(SWITCHES[openSwitches++], LOW);
  }
  knownResistance = RESISTOR_1M;
  
  int unknownADC, knownADC, referenceADC;
  
  // read reference voltage
  referenceADC = ads.readADC_Differential_0_3();
  referenceVoltage = ads.computeVolts(referenceADC);
  
  // read known voltage
  knownADC = ads.readADC_Differential_2_3();
  knownVoltage = ads.computeVolts(knownADC);

  // lessen the voltage drop across the known resistor by adding parallel resistors
  while (knownVoltage > (referenceVoltage * 0.75) && openSwitches > 0)
  {
    digitalWrite(SWITCHES[--openSwitches], HIGH);
    knownResistance = 1 / ((pow(knownResistance, -1) + pow(RESISTANCE_VALUES[openSwitches], -1)));
    knownADC = ads.readADC_Differential_2_3();
    knownVoltage = ads.computeVolts(knownADC);
    referenceADC = ads.readADC_Differential_0_3();
    referenceVoltage = ads.computeVolts(referenceADC);
  }
  
  // read unknown voltage
  unknownADC = ads.readADC_Differential_0_1();
  unknownVoltage = ads.computeVolts(unknownADC);
  
  // ADC gain adjustment
  if (unknownVoltage > -0.256 && unknownVoltage < 0.256)  // 16x gain
  {
    ads.setGain(GAIN_SIXTEEN);
  }
  else if (unknownVoltage > -0.512 && unknownVoltage < 0.512) // 8x gain
  {
    ads.setGain(GAIN_EIGHT);
  }
  else if (unknownVoltage > -0.124 && unknownVoltage < 1.024) // 4x gain
  {
    ads.setGain(GAIN_FOUR);
  }
  else if (unknownVoltage > -2.048 && unknownVoltage < 2.048) // 2x gain
  {
    ads.setGain(GAIN_TWO);
  }
  else if (unknownVoltage > -4.096 && unknownVoltage < 4.096) // 1x gain
  {
    ads.setGain(GAIN_ONE);
  }
  else  // (2/3)x gain
  {
    ads.setGain(GAIN_TWOTHIRDS);
  }
  
  // read unknown voltage with gain adjusted
  unknownADC = ads.readADC_Differential_0_1();
  unknownVoltage = ads.computeVolts(unknownADC);

  ads.setGain(GAIN_TWOTHIRDS);  // reset gain to default
}
