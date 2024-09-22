#include <Wire.h>
#include "ds3231.h"
#include <IridiumSBD.h>
#include <avr/sleep.h>
// 2024,09,20,12,07,25
//** Set Arduino Values **//
boolean initialSetup = true;
boolean sendSatMessage = false;
boolean resendRequired;

// RTC Wakeup Pin
#define wakePin 2 // when low, makes 328P wake up, must be an interrupt pin (2 or 3 on ATMEGA328P)

// Sonic Sensor Pin
#define sonicSensor 7

// Satellite Modem Pin
#define modemStandIn 8

// Declare the IridiumSBD object using default I2C address
#define IridiumWire Wire
IridiumSBD modem(IridiumWire);

// Set the Value for Reporting Period
#define REPORT_PERIOD 12

// Sensor readings
long pulse;                         // The raw reading from the sensor
int currentReading;                 // The current reading from the sensor
int rangevalue[] = {0, 0, 0, 0, 0}; // Temp array to hold the readings
int modE;                           // The mode of the readings
int arraysize = 5;                  // The levels array for an individual hour

// DS3231 alarm time
uint8_t wake_HOUR;
uint8_t wake_MINUTE;
uint8_t wake_SECOND;
#define BUFF_MAX 256

struct ts t;

// Variables for sending the message
String lvl_message;
int levels[REPORT_PERIOD] = {0};
int txMsgLen;
int j = 0;
char satMessage[50] = {0};

// Standard setup( ) function
void setup()
{
  Serial.begin(115200);
  pinMode(modemStandIn, OUTPUT);
  pinMode(sonicSensor, INPUT);
  digitalWrite(modemStandIn, LOW);
  // Clear the current alarm (puts DS3231 INT high)
  Wire.begin();
  Wire.setClock(400000);
  DS3231_init(DS3231_CONTROL_INTCN);
  DS3231_clear_a1f();
}

void loop()
{
  // Get the current time
  // Check to see if the sonic sensor is working is high or low

  DS3231_get(&t);
  // If the the current hour is not the hour in which a signal is to be sent run this code
  // Print Current Time to Serial Monitor t.hour, t.min, t.sec
  Serial.println((String) "Current Time: " + t.hour + ":" + t.min + ":" + t.sec);
  // print the resending flag
  Serial.println((String) "Resend Flag: " + resendRequired);
  sendSatMessage = (t.min == 8 || t.min == 20 || resendRequired);
  // print the sendSatMessage flag
  Serial.println((String) "Send Message Flag: " + sendSatMessage);
  Serial.println((String) "Initial Setup Flag: " + initialSetup);
  if (initialSetup)
  {
    Serial.println((String) "Taking First Reading");
    currentReading = takeAReading();
    arrangeLevelsArray(currentReading);
    createLevelMessage();
    Serial.println((String) "Sending First Message...");
    sendSatelliteMessage(satMessage, resendRequired);
    initialSetup = false;
    goToSleep();
  }

  else if (sendSatMessage)
  {
    // This code should only run at 9am or 9pm and if a message needs to be resent
    if (resendRequired)
    {
      Serial.println((String) "!XX! Resending a Message...");
      Serial.println((String) "!XX! The message that needs to be resent to the satellite is " + satMessage);
      sendSatelliteMessage(satMessage, resendRequired);
      resendRequired = false;
      Serial.println((String) "Resend Flag: " + resendRequired);
      clearLevelsArray();
      currentReading = takeAReading();
      arrangeLevelsArray(currentReading);
      Serial.println("Current levels array after resending the message:");
      for (int i = 0; i < REPORT_PERIOD; i++)
      {
        Serial.print(levels[i]);
        if (i < REPORT_PERIOD - 1)
        {
          Serial.print(", ");
        }
      }
      Serial.println();
      goToSleep();
    }

    else
    {
      Serial.println((String) "Taking a Reading");
      currentReading = takeAReading();
      arrangeLevelsArray(currentReading);
      createLevelMessage();
      Serial.println((String) "Sending a Message...");
      sendSatelliteMessage(satMessage, resendRequired);
      // Clear the levels array if the message is sent
      if (!resendRequired)
      {
        clearLevelsArray();
      }
      goToSleep();
    }
  }
  else
  {
    // Take a reading
    currentReading = takeAReading();
    arrangeLevelsArray(currentReading);
    Serial.println("Current levels array after Normal Reading:");
    for (int i = 0; i < REPORT_PERIOD; i++)
    {
      Serial.print(levels[i]);
      if (i < REPORT_PERIOD - 1)
      {
        Serial.print(", ");
      }
    }
    Serial.println();
    goToSleep();
  }
}

// Reading and sending functions.

int takeAReading()
{
  // Take 5 readings over 25 seconds
  for (int i = 0; i < arraysize; i++)
  {
    pulse = t.min;
    rangevalue[i] = pulse;
    Serial.println((String) "Reading is " + pulse);
  }
  // We have 5 samples report the median to the levels array
  isort(rangevalue, arraysize);
  modE = mode(rangevalue, arraysize);

  // We have 5 samples report the median to the levels array
  isort(rangevalue, arraysize);
  modE = mode(rangevalue, arraysize);
  return modE;
}

// Determine the position in the levels array based on the current hour
void arrangeLevelsArray(int currentLevel)
{
  int hourPosition = (t.min - 8 + 24) % 24; // Adjust the hour to be between 0 and 23
  hourPosition = (hourPosition + 11) % 12;  // Wrap around to the last position in the 12-hour array
  levels[hourPosition] = currentLevel;
  Serial.println((String) "The mode at " + ((hourPosition + 1) % 12) + " is " + levels[hourPosition]);
}

void clearLevelsArray()
{
  for (int i = 0; i < REPORT_PERIOD; i++)
  {
    levels[i] = 0;
  }
}

void createLevelMessage()
{
  lvl_message = "[";
  for (j = 0; j < REPORT_PERIOD; j++)
  {
    lvl_message.concat(levels[j]);
    if (j < (REPORT_PERIOD - 1))
    {
      lvl_message.concat(",");
    }
    else
    {
      lvl_message.concat("]");
    }
  }

  txMsgLen = lvl_message.length() + 1;
  lvl_message.toCharArray(satMessage, txMsgLen);
}

// void sendSatelliteMessage(const String &message, bool &resendRequired)
// {
//   int signalQuality = -1;
//   int err;

//   // Check that the Qwiic Iridium is attached
//   if (!modem.isConnected())
//   {
//     Serial.println(F("Qwiic Iridium is not connected! Please check wiring. Freezing."));
//     while (1)
//       ;
//   }

//   // Enable the supercapacitor charger
//   Serial.println(F("Enabling the supercapacitor charger..."));
//   modem.enableSuperCapCharger(true);

//   // Wait for the supercapacitor charger PGOOD signal to go high
//   while (!modem.checkSuperCapCharger())
//     ;
//   Serial.println(F("Supercapacitors charged!"));

//   // Enable power for the 9603N
//   Serial.println(F("Enabling 9603N power..."));
//   modem.enable9603Npower(true);

//   // Begin satellite modem operation
//   Serial.println(F("Starting modem..."));
//   err = modem.begin();
//   if (err != ISBD_SUCCESS)
//   {
//     Serial.print(F("Begin failed: error "));
//     Serial.println(err);
//     if (err == ISBD_NO_MODEM_DETECTED)
//       Serial.println(F("No modem detected: check wiring."));
//     return;
//   }

//   // Send the message
//   Serial.println(F("Trying to send the message.  This might take several minutes."));
//   Serial.println((String) "The message being sent to the satellite is " + satMessage);
//   err = modem.sendSBDText(satMessage);
//   if (err != ISBD_SUCCESS)
//   {
//     resendRequired = true;
//     Serial.println((String) "Resend flag value set");
//     Serial.print(F("sendSBDText failed: error "));
//     Serial.println(err);
//     if (err == ISBD_SENDRECEIVE_TIMEOUT)
//       Serial.println(F("Message Sending Failed"));
//   }

//   else
//   {
//     Serial.println(F("Satellite message sent!"));
//   }

//   // Clear the Mobile Originated message buffer
//   Serial.println(F("Clearing the MO buffer."));
//   err = modem.clearBuffers(ISBD_CLEAR_MO); // Clear MO buffer
//   if (err != ISBD_SUCCESS)
//   {
//     Serial.print(F("clearBuffers failed: error "));
//     Serial.println(err);
//   }

//   // Power down the modem
//   Serial.println(F("Putting the 9603N to sleep."));
//   err = modem.sleep();
//   if (err != ISBD_SUCCESS)
//   {
//     Serial.print(F("sleep failed: error "));
//     Serial.println(err);
//   }

//   // Disable 9603N power
//   Serial.println(F("Disabling 9603N power..."));
//   modem.enable9603Npower(false);

//   // Disable the supercapacitor charger
//   Serial.println(F("Disabling the supercapacitor charger..."));
//   modem.enableSuperCapCharger(false);

//   Serial.println(F("Message Send Function Complete"));
// }

// Sorting Functions
void isort(int *a, int n)
{
  //  *a is an array pointer function
  for (int i = 1; i < n; ++i)
  {
    int j = a[i];
    int k;
    for (k = i - 1; (k >= 0) && (j < a[k]); k--)
    {
      a[k + 1] = a[k];
    }
    a[k + 1] = j;
  }
}

// Mode function, returning the mode or median.
int mode(int *x, int n)
{
  int i = 0;
  int count = 0;
  int maxCount = 0;
  int mode = 0;
  int bimodal;
  int prevCount = 0;

  while (i < (n - 1))
  {
    prevCount = count;
    count = 0;
    while (x[i] == x[i + 1])
    {
      count++;
      i++;
    }
    if (count > prevCount & count > maxCount)
    {
      mode = x[i];
      maxCount = count;
      bimodal = 0;
    }

    if (count == 0)
    {
      i++;
    }

    if (count == maxCount)
    { // If the dataset has 2 or more modes.
      bimodal = 1;
    }
    if (mode == 0 || bimodal == 1)
    { // Return the median if there is no mode.
      mode = x[(n / 2)];
    }
    return mode;
  }
}

// Sleep functionality below

void goToSleep()
{
  setNextAlarm();

  // Disable the ADC (Analog to digital converter, pins A0 [14] to A5 [19])
  static byte prevADCSRA = ADCSRA;
  ADCSRA = 0;

  /* Set the type of sleep mode we want. Can be one of (in order of power saving):
   SLEEP_MODE_IDLE (Timer 0 will wake up every millisecond to keep millis running)
   SLEEP_MODE_ADC
   SLEEP_MODE_PWR_SAVE (TIMER 2 keeps running)
   SLEEP_MODE_EXT_STANDBY
   SLEEP_MODE_STANDBY (Oscillator keeps running, makes for faster wake-up)
   SLEEP_MODE_PWR_DOWN (Deep sleep)
   */
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  // Turn of Brown Out Detection (low voltage)
  // Thanks to Nick Gammon for how to do this (temporarily) in software rather than
  // permanently using an avrdude command line.
  //
  // Note: Microchip state: BODS and BODSE only available for picoPower devices ATmega48PA/88PA/168PA/328P
  //
  // BODS must be set to one and BODSE must be set to zero within four clock cycles. This sets
  // the MCU Control Register (MCUCR)
  MCUCR = bit(BODS) | bit(BODSE);

  // The BODS bit is automatically cleared after three clock cycles so we better get on with it
  MCUCR = bit(BODS);

  // Ensure we can wake up again by first disabling interupts (temporarily) so
  // the wakeISR does not run before we are asleep and then prevent interrupts,
  // and then defining the ISR (Interrupt Service Routine) to run when poked awake
  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(wakePin), sleepISR, LOW);

  // Send a message just to show we are about to sleep
  Serial.println("Good night!");
  Serial.flush();

  // Allow interrupts now
  interrupts();

  // And enter sleep mode as set above
  sleep_cpu();

  // --------------------------------------------------------
  // µController is now asleep until woken up by an interrupt
  // --------------------------------------------------------

  // Wakes up at this point when wakePin is brought LOW - interrupt routine is run first
  Serial.println("I'm awake!");

  // Clear existing alarm so int pin goes high again
  DS3231_clear_a1f();

  // Re-enable ADC if it was previously running
  ADCSRA = prevADCSRA;
}

void sleepISR()
{
  // Prevent sleep mode, so we don't enter it again, except deliberately, by code
  sleep_disable();

  // Detach the interrupt that brought us out of sleep
  detachInterrupt(digitalPinToInterrupt(wakePin));

  // Now we continue running the main Loop() just after we went to sleep
}

// Set the next alarm
void setNextAlarm()
{
  // flags define what calendar component to be checked against the current time in order
  // to trigger the alarm - see datasheet
  // A1M1 (seconds) (0 to enable, 1 to disable)
  // A1M2 (minutes) (0 to enable, 1 to disable)
  // A1M3 (hour)    (0 to enable, 1 to disable)
  // A1M4 (day)     (0 to enable, 1 to disable)
  // DY/DT          (dayofweek == 1/dayofmonth == 0)
  uint8_t flags[5] = {0, 0, 0, 1, 1};
  // get current time so we can calc the next alarm
  DS3231_get(&t);
  // set the values for the next alarm - wake up on the hour every hour
  wake_HOUR = t.hour;
  wake_MINUTE = t.min + 1;
  wake_SECOND = 0;

  Serial.println((String) "Current Time: " + t.hour + ":" + t.min + ":" + t.sec);
  // Set the alarm time (but not yet activated)
  Serial.println("Setting alarm for " + (String)wake_HOUR + ":" + (String)wake_MINUTE + ":" + (String)wake_SECOND);

  DS3231_set_a1(wake_SECOND, wake_MINUTE, wake_HOUR, 0, flags);
  // Turn the alarm on
  DS3231_set_creg(DS3231_CONTROL_INTCN | DS3231_CONTROL_A1IE);
}

void sendSatelliteMessage(const String &message, bool &resendRequired)
{
  // Send the message
  Serial.println(F("Trying to send the message.  This might take several minutes."));
  digitalWrite(modemStandIn, HIGH);
  Serial.println((String) "The message being sent to the satellite is " + message);
  delay(5000);
  digitalWrite(modemStandIn, LOW);
  if (digitalRead(sonicSensor) == LOW)
  {
    resendRequired = true;
    Serial.println((String) "Resend Flag: " + resendRequired);
    Serial.print(F("sendSBDText failed: error "));
    Serial.println(F("Message Sending Failed"));
  }
  else
  {
    Serial.println(F("Satellite message sent!"));
  }
  Serial.println(F("Putting the 9603N to sleep."));
  Serial.println(F("Disabling 9603N power..."));
  Serial.println(F("Disabling the supercapacitor charger..."));
  Serial.println(F("Message Send Function Complete"));
}
