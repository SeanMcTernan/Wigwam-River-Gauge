
#include <Wire.h>
#include "ds3231.h"
#include <avr/sleep.h>

//** Set Arduino Values **//

// RTC Wakeup Pin
#define wakePin 3 // when low, makes 328P wake up, must be an interrupt pin (2 or 3 on ATMEGA328P)
// Sonic Sensor Pin
#define sonicSensor 7
// Satellite Modem Pin
#define redLED 12
// Set the minute value to check agianst
#define MINUTE_VALUE 60000
// Set the report period
#define REPORT_PERIOD 8

//** Set Variables **//

// The number of readings to take before sending the data
int hoursCount = 0;
// The raw reading from the sensor
long pulse;
// Temp array to hold the readings
int rangevalue[] = {0, 0, 0, 0, 0};
// The mode of the readings
int modE;
// The levels array for an individual hour.
int arraysize = 5;
// Time value to keep track during the loop
unsigned long startTime;
unsigned long elapsedTime;
// Levels array to send to the satellite
int levels[REPORT_PERIOD] = {0};
// a boolean to determine if a measurement is neededed.
bool measuring;

// DS3231 alarm time
uint8_t wake_HOUR;
uint8_t wake_MINUTE;
uint8_t wake_SECOND;
#define BUFF_MAX 256

struct ts t;

// Standard setup( ) function
void setup()
{
  Serial.begin(9600);

  pinMode(sonicSensor, INPUT);
  digitalWrite(sonicSensor, LOW);

  pinMode(redLED, OUTPUT);
  digitalWrite(redLED, LOW);

  // Clear the current alarm (puts DS3231 INT high)
  Wire.begin();
  DS3231_init(DS3231_CONTROL_INTCN);
  DS3231_clear_a1f();

  Serial.println("Setup completed.");
}

void loop()
{

  if (hoursCount != 7)
  {
    // Start tracking time for function execution
    startTime = millis();
    // Take a reading
    takeAReading();
    // Increment the hours count
    hoursCount++;
    elapsedTime = millis() - startTime;
    // Go to sleep
    goToSleep(elapsedTime / 1000);
  }

  else
  {
    startTime = millis();
    Serial.println((String) "Taking Final Reading " + hoursCount);
    Serial.println((String) "Sending sat signal");
    digitalWrite(redLED, HIGH);
    delay(8000);
    digitalWrite(redLED, LOW);
    hoursCount = 0;
    elapsedTime = millis() - startTime;
    goToSleep(elapsedTime / 1000);
  }
}
void goToSleep(int elapsedTime)
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

void setNextAlarm(void)
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

  wake_SECOND = t.sec;
  wake_MINUTE = t.min;
  wake_HOUR = t.hour;

  // Add a some seconds to current time. If overflow increment minutes etc.
  wake_SECOND = wake_SECOND + 10;
  if (wake_SECOND > 59)
  {
    wake_MINUTE++;
    wake_SECOND = wake_SECOND - 60;

    if (wake_MINUTE > 59)
    {
      wake_HOUR++;
      wake_MINUTE -= 60;
    }
  }

  // Set the alarm time (but not yet activated)
  DS3231_set_a1(wake_SECOND, wake_MINUTE, wake_HOUR, 0, flags);

  // Turn the alarm on
  DS3231_set_creg(DS3231_CONTROL_INTCN | DS3231_CONTROL_A1IE);
}

// Sorting function
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

void takeAReading()
{
  // Take 5 readings over 25 seconds
  for (int i = 0; i < arraysize; i++)
  {
    pulse = pulseIn(sonicSensor, HIGH);
    rangevalue[i] = pulse / 58;
    Serial.println((String) "Reading is " + (pulse / 58));
    delay(1000);
  }
  // We have 5 samples report the median to the levels array
  isort(rangevalue, arraysize);
  modE = mode(rangevalue, arraysize);
  levels[hoursCount] = modE;
  Serial.println((String) "The mode at " + (hoursCount) + " is " + levels[hoursCount]);
  // Shut off the sensor
  pulseIn(sonicSensor, LOW);
}