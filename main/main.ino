#include <Wire.h>
#include "ds3231.h"
#include <IridiumSBD.h>
#include <avr/sleep.h>
#include <time.h>
// 2024,10,20,17,57,50
//** Set Arduino Values **//
boolean initialSetup = true;
boolean sendSatMessage = false;
boolean resendRequired;

// RTC Wakeup Pin
#define wakePin 3 // when low, makes 328P wake up, must be an interrupt pin (2 or 3 on ATMEGA328P)

// Sonic Sensor Pin
#define sonicSensor 7

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

// Function to initialize the Iridium modem
bool initializeIridiumModem()
{
    int err;

    // Check that the Qwiic Iridium is attached
    if (!modem.isConnected())
    {
        Serial.println(F("Qwiic Iridium is not connected! Please check wiring."));
        return false;
    }

    // Enable the supercapacitor charger
    Serial.println(F("Enabling the supercapacitor charger..."));
    modem.enableSuperCapCharger(true);

    // Wait for the supercapacitor charger PGOOD signal to go high
    while (!modem.checkSuperCapCharger())
    {
        Serial.println(F("Waiting for supercapacitors to charge..."));
        delay(1000);
    }
    Serial.println(F("Supercapacitors charged!"));

    // Enable power for the 9603N
    Serial.println(F("Enabling 9603N power..."));
    modem.enable9603Npower(true);

    // Begin satellite modem operation
    Serial.println(F("Starting modem..."));
    modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE); // Assume 'USB' power (slow recharge)
    err = modem.begin();
    if (err != ISBD_SUCCESS)
    {
        Serial.print(F("Begin failed: error "));
        Serial.println(err);
        if (err == ISBD_NO_MODEM_DETECTED)
            Serial.println(F("No modem detected: check wiring."));
        return false;
    }

    return true;
}

// Helper function to calculate day of week (0=Sunday, 1=Monday, etc.)
uint8_t calculateDayOfWeek(int year, int month, int day)
{
    // Zeller's congruence algorithm
    if (month < 3)
    {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + ((13 * (month + 1)) / 5) + k + (k / 4) + (j / 4) - 2 * j) % 7;
    return (h + 5) % 7 + 1; // Convert to 1-7 range (1=Sunday)
}

// Helper function to calculate day of year
uint16_t calculateDayOfYear(int year, int month, int day)
{
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Check for leap year
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
    {
        days_in_month[1] = 29;
    }

    int dayOfYear = day;
    for (int i = 0; i < month - 1; i++)
    {
        dayOfYear += days_in_month[i];
    }

    return dayOfYear;
}

// Helper function to set RTC time from string
// Supports formats: "YYYY-MM-DD HH:MM:SS", "YYYY,MM,DD,HH,MM,SS", "MM/DD/YYYY HH:MM:SS"
bool setRTCFromString(const String &timeString)
{
    struct ts rtc_time;
    int year, month, day, hour, minute, second;

    // Try different parsing formats
    bool parsed = false;

    // Format 1: "YYYY-MM-DD HH:MM:SS"
    if (timeString.indexOf('-') > 0 && timeString.indexOf(':') > 0)
    {
        if (sscanf(timeString.c_str(), "%d-%d-%d %d:%d:%d",
                   &year, &month, &day, &hour, &minute, &second) == 6)
        {
            parsed = true;
        }
    }
    // Format 2: "YYYY,MM,DD,HH,MM,SS"
    else if (timeString.indexOf(',') > 0)
    {
        if (sscanf(timeString.c_str(), "%d,%d,%d,%d,%d,%d",
                   &year, &month, &day, &hour, &minute, &second) == 6)
        {
            parsed = true;
        }
    }
    // Format 3: "MM/DD/YYYY HH:MM:SS"
    else if (timeString.indexOf('/') > 0 && timeString.indexOf(':') > 0)
    {
        if (sscanf(timeString.c_str(), "%d/%d/%d %d:%d:%d",
                   &month, &day, &year, &hour, &minute, &second) == 6)
        {
            parsed = true;
        }
    }

    if (!parsed)
    {
        Serial.println(F("Error: Invalid time string format"));
        Serial.println(F("Supported formats:"));
        Serial.println(F("  YYYY-MM-DD HH:MM:SS"));
        Serial.println(F("  YYYY,MM,DD,HH,MM,SS"));
        Serial.println(F("  MM/DD/YYYY HH:MM:SS"));
        return false;
    }

    // Validate ranges
    if (year < 2000 || year > 2099 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59)
    {
        Serial.println(F("Error: Time values out of valid range"));
        return false;
    }

    // Populate the ts structure
    rtc_time.year = year;
    rtc_time.mon = month;
    rtc_time.mday = day;
    rtc_time.hour = hour;
    rtc_time.min = minute;
    rtc_time.sec = second;
    rtc_time.wday = calculateDayOfWeek(year, month, day);
    rtc_time.yday = calculateDayOfYear(year, month, day);
    rtc_time.isdst = 0; // Assume standard time unless specified
    rtc_time.year_s = year % 100;

    // Set the RTC
    DS3231_set(rtc_time);

    // Confirm the setting
    char confirmation[32];
    sprintf(confirmation, "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    Serial.print(F("RTC time set to: "));
    Serial.println(confirmation);

    return true;
}

// Function to get satellite time with retry loop
bool getSatelliteTime()
{
    struct tm t;
    int err;
    int maxRetries = 20;
    int retryCount = 0;

    Serial.println(F("Attempting to get Iridium satellite time..."));

    while (retryCount < maxRetries)
    {
        err = modem.getSystemTime(t);

        if (err == ISBD_SUCCESS)
        {
            String satTime = String(t.tm_year + 1900) + "-" + String(t.tm_mon + 1) + "-" + String(t.tm_mday) + " " + String(t.tm_hour) + ":" + String(t.tm_min) + ":" + String(t.tm_sec);
            setRTCFromString(satTime);
            char buf[32];
            sprintf(buf, "%d-%02d-%02d %02d:%02d:%02d",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
            Serial.print(F("Iridium satellite time: "));
            Serial.println(buf);
            return true;
        }
        else if (err == ISBD_NO_NETWORK)
        {
            retryCount++;
            Serial.print(F("No network detected. Retry "));
            Serial.print(retryCount);
            Serial.print(F("/"));
            Serial.print(maxRetries);
            Serial.println(F(". Waiting 10 seconds..."));
            delay(10000);
        }
        else
        {
            Serial.print(F("Unexpected error getting time: "));
            Serial.println(err);
            retryCount++;
            delay(5000);
        }
    }

    Serial.println(F("Failed to get satellite time after maximum retries."));
    return false;
}

// Standard setup( ) function
void setup()
{
    Serial.begin(115200);
    while (!Serial)
        ; // Wait for serial port to connect

    Serial.println(F("Wigwam River Gauge Starting Up..."));

    // Set the sonic sensor as an input
    pinMode(sonicSensor, INPUT);
    digitalWrite(sonicSensor, LOW);

    // Initialize I2C and DS3231 RTC
    Wire.begin();
    Wire.setClock(400000);
    DS3231_init(DS3231_CONTROL_INTCN);
    DS3231_clear_a1f();

    // Initialize Iridium modem and get satellite time
    if (initializeIridiumModem())
    {
        Serial.println(F("Iridium modem initialized successfully."));

        // Get satellite time before entering main loop
        if (getSatelliteTime())
        {
            Serial.println(F("Satellite time retrieved successfully."));
        }
        else
        {
            Serial.println(F("Warning: Could not retrieve satellite time."));
        }

        // Power down the modem after getting time
        Serial.println(F("Putting modem to sleep after time sync..."));
        modem.sleep();
        modem.enable9603Npower(false);
        modem.enableSuperCapCharger(false);
    }
    else
    {
        Serial.println(F("Warning: Iridium modem initialization failed."));
    }
    Serial.println(F("Setup complete. Entering main loop..."));
}

void loop()
{
    // Get the current time
    DS3231_get(&t);
    // If the the current hour is not the hour in which a signal is to be sent run this code
    // Print Current Time to Serial Monitor t.hour, t.min, t.sec
    Serial.println((String) "Current Time: " + t.hour + ":" + t.min + ":" + t.sec);
    // print the resending flag
    Serial.println((String) "Resend Flag: " + resendRequired);
    sendSatMessage = (t.hour == 8 || t.hour == 20 || resendRequired);
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
        // remove comments before merging to main
        // sendSatelliteMessage(satMessage, resendRequired);
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
            // remove comments before merging to main
            // sendSatelliteMessage(satMessage, resendRequired);
            resendRequired = false;
            Serial.println((String) "Resend Flag: " + resendRequired);
            clearLevelsArray();
            currentReading = takeAReading();
            arrangeLevelsArray(currentReading);
            // Serial below strictly for debugging purposes
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
            // remove comments before merging to main
            // sendSatelliteMessage(satMessage, resendRequired);
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
        // Serial below strictly for debugging purposes
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

int takeAReading()
{
    // Take 5 readings over 25 seconds
    for (int i = 0; i < arraysize; i++)
    {
        pulse = pulseIn(sonicSensor, HIGH);
        rangevalue[i] = pulse / 58;
        Serial.println((String) "Reading is " + (pulse / 58));
        // Wait 5 seconds before taking the next reading -- For testing purposes value is at .5 seconds
        delay(5000);
    }
    // We have 5 samples report the median to the levels array
    isort(rangevalue, arraysize);
    modE = mode(rangevalue, arraysize);
    // Shut off the sensor
    pulseIn(sonicSensor, LOW);
    return modE;
}

void arrangeLevelsArray(int currentLevel)
{
    int hourPosition = (t.hour - 8 + 24) % 24; // Adjust the hour to be between 0 and 23
    hourPosition = (hourPosition + 11) % 12;   // Wrap around to the last position in the 12-hour array
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
    wake_HOUR = ((t.hour + 1) % 24);
    wake_MINUTE = 0;
    wake_SECOND = 0;

    Serial.println((String) "Current Time: " + t.hour + ":" + t.min + ":" + t.sec);
    // Set the alarm time (but not yet activated)
    Serial.println("Setting alarm for " + (String)wake_HOUR + ":" + (String)wake_MINUTE + ":" + (String)wake_SECOND);

    DS3231_set_a1(wake_SECOND, wake_MINUTE, wake_HOUR, 0, flags);
    // Turn the alarm on
    DS3231_set_creg(DS3231_CONTROL_INTCN | DS3231_CONTROL_A1IE);
}

// Sorting function

void sendSatelliteMessage(const String &message, bool &resendRequired)
{
    int signalQuality = -1;
    int err;

    // Check that the Qwiic Iridium is attached
    if (!modem.isConnected())
    {
        Serial.println(F("Qwiic Iridium is not connected! Please check wiring. Freezing."));
        while (1)
            ;
    }

    // Enable the supercapacitor charger
    Serial.println(F("Enabling the supercapacitor charger..."));
    modem.enableSuperCapCharger(true);

    // Wait for the supercapacitor charger PGOOD signal to go high
    while (!modem.checkSuperCapCharger())
        ;
    Serial.println(F("Supercapacitors charged!"));

    // Enable power for the 9603N
    Serial.println(F("Enabling 9603N power..."));
    modem.enable9603Npower(true);

    // Begin satellite modem operation
    Serial.println(F("Starting modem..."));
    err = modem.begin();
    if (err != ISBD_SUCCESS)
    {
        Serial.print(F("Begin failed: error "));
        Serial.println(err);
        if (err == ISBD_NO_MODEM_DETECTED)
            Serial.println(F("No modem detected: check wiring."));
        return;
    }

    // Send the message
    Serial.println(F("Trying to send the message.  This might take several minutes."));
    Serial.println((String) "The message being sent to the satellite is " + satMessage);
    // remove comments before merging to main
    // err = modem.sendSBDText(satMessage);
    // if (err != ISBD_SUCCESS)
    // {
    //     resendRequired = true;
    //     Serial.println((String) "Resend flag value set");
    //     Serial.print(F("sendSBDText failed: error "));
    //     Serial.println(err);
    //     if (err == ISBD_SENDRECEIVE_TIMEOUT)
    //         Serial.println(F("Message Sending Failed"));
    // }

    // else
    // {
    //     Serial.println(F("Satellite message sent!"));
    // }

    // Clear the Mobile Originated message buffer
    Serial.println(F("Clearing the MO buffer."));
    err = modem.clearBuffers(ISBD_CLEAR_MO); // Clear MO buffer
    if (err != ISBD_SUCCESS)
    {
        Serial.print(F("clearBuffers failed: error "));
        Serial.println(err);
    }

    // Power down the modem
    Serial.println(F("Putting the 9603N to sleep."));
    err = modem.sleep();
    if (err != ISBD_SUCCESS)
    {
        Serial.print(F("sleep failed: error "));
        Serial.println(err);
    }

    // Disable 9603N power
    Serial.println(F("Disabling 9603N power..."));
    modem.enable9603Npower(false);

    // Disable the supercapacitor charger
    Serial.println(F("Disabling the supercapacitor charger..."));
    modem.enableSuperCapCharger(false);

    Serial.println(F("Message Send Function Complete"));
}
