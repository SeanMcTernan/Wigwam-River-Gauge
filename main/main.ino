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

        // Display current RTC time before satellite sync
        Serial.println(F("RTC time before satellite sync:"));
        displayCurrentSystemTime();

        // Get satellite time before entering main loop
        if (getSatelliteTime())
        {
            Serial.println(F("Satellite time retrieved successfully."));
            Serial.println(F("RTC time after satellite sync:"));
            displayCurrentSystemTime();
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

    // Clear any stale data from serial buffers before starting
    while (Serial.available() > 0)
        Serial.read();

    err = modem.begin();
    if (err != ISBD_SUCCESS)
    {
        Serial.print(F("Begin failed: error "));
        Serial.println(err);
        if (err == ISBD_NO_MODEM_DETECTED)
            Serial.println(F("No modem detected: check wiring."));
        return false;
    }

    // Allow modem to fully initialize before time requests
    Serial.println(F("Modem initialized. Waiting 3 seconds for stabilization..."));
    delay(3000);

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

// Function to set RTC from UTC satellite time with MDT conversion
bool setRTCFromUTC(const struct tm &utc_time)
{
    struct ts rtc_time;

    // Convert struct tm to local variables for manipulation
    int year = utc_time.tm_year + 1900;
    int month = utc_time.tm_mon + 1; // tm_mon is 0-11, we need 1-12
    int day = utc_time.tm_mday;
    int hour = utc_time.tm_hour;
    int minute = utc_time.tm_min;
    int second = utc_time.tm_sec;

    // Show the UTC time before conversion
    char utc_buf[32];
    sprintf(utc_buf, "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    Serial.print(F("UTC time before MDT conversion: "));
    Serial.println(utc_buf);

    // Apply Mountain Daylight Time offset (UTC-6)
    hour -= 6;

    // Handle hour underflow (date rollback)
    if (hour < 0)
    {
        hour += 24;
        day--;

        // Handle day underflow (month rollback)
        if (day < 1)
        {
            month--;

            // Handle month underflow (year rollback)
            if (month < 1)
            {
                month = 12; // December
                year--;
            }

            // Set day to last day of previous month
            int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

            // Check for leap year
            if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
            {
                day = 29; // February in leap year
            }
            else
            {
                day = days_in_month[month - 1];
            }
        }
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
    rtc_time.isdst = 1; // Set to 1 since we're converting to MDT (daylight saving time)
    rtc_time.year_s = year % 100;

    // Set the RTC
    DS3231_set(rtc_time);

    // Confirm the setting
    char confirmation[32];
    sprintf(confirmation, "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    Serial.print(F("RTC time set to (MDT): "));
    Serial.println(confirmation);

    // Read back the RTC time to verify it was set correctly
    struct ts verify_time;
    DS3231_get(&verify_time);
    char verify_buf[32];
    sprintf(verify_buf, "%04d-%02d-%02d %02d:%02d:%02d",
            verify_time.year, verify_time.mon, verify_time.mday,
            verify_time.hour, verify_time.min, verify_time.sec);
    Serial.print(F("RTC verification read: "));
    Serial.println(verify_buf);

    return true;
}

// Function to display current system time for comparison
void displayCurrentSystemTime()
{
    struct ts current_time;
    DS3231_get(&current_time);

    char time_buf[32];
    sprintf(time_buf, "%04d-%02d-%02d %02d:%02d:%02d",
            current_time.year, current_time.mon, current_time.mday,
            current_time.hour, current_time.min, current_time.sec);
    Serial.print(F("Current RTC time (MDT): "));
    Serial.println(time_buf);
}

// Function to get satellite time with retry loop
bool getSatelliteTime()
{
    struct tm t;
    int err;
    int maxRetries = 3; // Reduced for testing - get 3 readings as requested
    int retryCount = 0;
    bool timeRetrieved = false;

    Serial.println(F("Attempting to get Iridium satellite time..."));

    // Clear any stale data before time requests (like getTime.ino does)
    while (Serial.available() > 0)
        Serial.read();

    // Force modem to acquire fresh network time by clearing any cached time
    Serial.println(F("Clearing modem buffers and forcing fresh time acquisition..."));
    modem.clearBuffers(ISBD_CLEAR_MO | ISBD_CLEAR_MT);
    delay(2000);

    while (retryCount < maxRetries)
    {
        // Add delay before each time request to ensure modem is ready
        if (retryCount > 0)
        {
            Serial.println(F("Waiting 10 seconds before retry..."));
            delay(10000);
        }
        else
        {
            // Even on first attempt, wait a bit for network acquisition
            Serial.println(F("Waiting 5 seconds for network acquisition..."));
            delay(5000);
        }

        Serial.print(F("Time request attempt #"));
        Serial.println(retryCount + 1);
        err = modem.getSystemTime(t);

        if (err == ISBD_SUCCESS)
        {
            // Display the UTC time received from satellite
            char buf[32];
            sprintf(buf, "%d-%02d-%02d %02d:%02d:%02d",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
            Serial.print(F("Iridium satellite time (UTC) - Reading #"));
            Serial.print(retryCount + 1);
            Serial.print(F(": "));
            Serial.println(buf);

            // Validate that the time is reasonable (not obviously stale)
            // Check if year is reasonable
            if (t.tm_year + 1900 < 2024 || t.tm_year + 1900 > 2030)
            {
                Serial.println(F("Warning: Received time appears invalid (bad year)"));
                retryCount++;
                continue;
            }

            // Store the time for potential use, but don't set RTC until we have the final reading
            timeRetrieved = true;

            // If this is the final reading (3rd attempt), set the RTC
            if (retryCount == maxRetries - 1)
            {
                Serial.println(F("Using final time reading to set RTC..."));
                setRTCFromUTC(t);
                return true;
            }
        }
        else if (err == ISBD_NO_NETWORK)
        {
            Serial.print(F("No network detected on attempt #"));
            Serial.print(retryCount + 1);
            Serial.println(F(". Waiting 15 seconds for network acquisition..."));
            delay(15000);
        }
        else
        {
            Serial.print(F("Unexpected error getting time on attempt #"));
            Serial.print(retryCount + 1);
            Serial.print(F(": "));
            Serial.println(err);
        }

        retryCount++;
    }

    if (timeRetrieved)
    {
        Serial.println(F("At least one time reading was successful, but failed to complete all 3 readings."));
        return false;
    }
    else
    {
        Serial.println(F("Failed to get satellite time after maximum retries."));
        return false;
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
