// #include <SoftwareSerial.h>
#include <IridiumSBD.h> // Click here to get the library: http://librarymanager/All#IridiumSBDI2C
#include <Wire.h>       //Needed for I2C communication

#define IridiumWire Wire
#define MINUTE_VALUE 60000
#define HOUR_VALUE 60
#define REPORT_PERIOD 4

// Declare the IridiumSBD object using default I2C address
IridiumSBD modem(IridiumWire);

// Set the variables for the sensor reading
static const int blueLED = 6;
static const int redLED = 7;
static const int greenLED = 8;
static const int yellowLED = 9;

int rangevalue[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
long pulse;
int modE;
int arraysize = 7;
bool measuring;

// Set the variables for timing
int minuteCount = 0;
int hourCount = 0;
unsigned long beginningOfTime;
unsigned long currentTime;

// Variables for sending the message
String lvl_message;
int levels[REPORT_PERIOD] = {0};
int txMsgLen;
int j = 0;
char satMessage[50] = {0};

void setup()
{
    // Set the initial time that the gauge was turned on
    beginningOfTime = millis();
    // Step for Serial Debugging only
    // Serial.begin(9600);
    // while (!Serial)
    ; // Wait to connect serial port. For native USB port only || Remove once live

    // Confirgure the pins used
    pinMode(blueLED, OUTPUT);
    pinMode(redLED, OUTPUT);
    pinMode(greenLED, OUTPUT);
    pinMode(yellowLED, OUTPUT);

    /*
     * Initiating the satellite modem
     *
     * Below we initiate the satellite modem. We first tell the arduino to
     * communicate with it via I2C, we then charge the supercapacitors.
     * Finally, we turn on the modem and set the power profile to be the default, then turn the system off.
     */

    // Start the I2C wire port connected to the satellite modem
    Wire.begin();
    Wire.setClock(400000); // Set I2C clock speed to 400kHz

    while (!modem.checkSuperCapCharger())
    {
        Serial.println(F("Waiting for supercapacitors to charge..."));
        digitalWrite(redLED, HIGH);
        delay(5000);
        digitalWrite(redLED, LOW);
    }
    Serial.println(F("Supercapacitors charged!"));

    // Enable power for the 9603N
    Serial.println(F("Enabling 9603N power..."));
    modem.enable9603Npower(true);
    // Set the modem power profile to be the default power profile
    Serial.println(F("Starting modem..."));
    modem.setPowerProfile(IridiumSBD::DEFAULT_POWER_PROFILE); // Set the default power source for the modem
    // Disable 9603N power
    Serial.println(F("Disabling 9603N power..."));
    modem.enable9603Npower(false);
    // Turn off the modem
    Serial.println(F("Putting modem to sleep and starting the loop"));
    modem.sleep();
    Serial.println(F("Setup complete!"));
}

void sendSatelliteMessage()
{
    int signalQuality = -1;
    int err;

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
    err = modem.sendSBDText(satMessage);
    if (err != ISBD_SUCCESS)
    {
        Serial.print(F("sendSBDText failed: error "));
        Serial.println(err);
        if (err == ISBD_SENDRECEIVE_TIMEOUT)
            Serial.println(F("Message Sending Failed"));
    }

    else
    {
        Serial.println(F("Satellite message sent!"));
    }

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

void loop()
{
    digitalWrite(blueLED, HIGH);
    // Set current time the loop starts
    currentTime = millis();

    // Prevent Millis Rollover
    if ((currentTime - beginningOfTime) < 0)
    {
        beginningOfTime = currentTime;
    }

    // It has been 1 minute, update the minute count and check if a reading is required
    if ((currentTime - beginningOfTime) >= MINUTE_VALUE)
    {
        // Counts up to 1hr
        minuteCount++;
        beginningOfTime = millis();

        // Check if a measurement is required
        if (measuring)
        {
            digitalWrite(greenLED, HIGH);
            // Gather 11 samples for rangeValue array over 55 Seconds
            for (int i = 0; i < arraysize; i++)
            {
                pulse = 10;
                rangevalue[i] = pulse / 58;
            }
            // We have 11 samples report the median to the levels array and shut off the sensor
            isort(rangevalue, arraysize);
            modE = mode(rangevalue, arraysize);
            levels[hourCount] = modE;
            Serial.println((String) "The mode at " + (hourCount) + " is " + levels[hourCount]);
            measuring = false;
            digitalWrite(greenLED, LOW);
        }
    }

    // An hour has passed reset the minute count and request the sensor to start reading again, also increment the hour reading
    if (minuteCount >= HOUR_VALUE)
    {
        minuteCount = 0;
        hourCount++;
        measuring = true;
    }

    // 8 Hours has passed, create the levels array and send it to the satelite. Reset the hours count to 0 to start the 8 hour cycle again.
    if (hourCount >= REPORT_PERIOD)
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
        // Send the message to the satellite
        Serial.println((String) "The message being sent to the satellite is " + satMessage);
        digitalWrite(yellowLED, HIGH);
        delay(5000);
        digitalWrite(yellowLED, LOW);
        // sendSatelliteMessage(); // Disabled for testing
        hourCount = 0;
    }
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
