#include <SoftwareSerial.h>

#define MINUTE_VALUE 10
#define HOUR_VALUE 60
#define REPORT_PERIOD 12
//Set the variables for the sensor reading
static const int sensorReadPin = 7;
int rangevalue[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
long pulse;
int modE;
int arraysize = 11;
bool measuring;

//Set the variables for timing
int minuteCount = 0;
int hourCount = 0;
unsigned long beginningOfTime;
unsigned long currentTime;

//VARs for sending the message
String lvl_message;
int levels[REPORT_PERIOD] = {0};
int txMsgLen;
int j, i = 0;
bool beginOnce;
char txBuffer[50] = {0};
char rxBuffer[50] = {0};

void setup()
{
    //Set the initial time that the gauge was turned on
    beginningOfTime = millis();
    //Step for Serial Debugging only
    Serial.begin(9600);
    while (!Serial)
    {
        ; // Wait to connect serial port. For native USB port only || Remove once live
    }
    //Confirgure the pins used
    pinMode(sensorReadPin, INPUT);
    pulseIn(sensorReadPin, LOW);
}

void loop()
{
    //Set current time the loop starts
    currentTime = millis();

    //Prevent Millis Rollover
    if ((currentTime - beginningOfTime) < 0)
    {
        beginningOfTime = currentTime;
    }

    //It has been 1 minute, update the minute count and check if a reading is required
    if ((currentTime - beginningOfTime) > MINUTE_VALUE)
    {
        //Counts up to 1hr
        minuteCount++;
        beginningOfTime = millis();

        //Check if a measurement is required
        if (measuring)
        {
            // Gather 11 samples for rangeValue array over 55 Seconds
            for (int i = 0; i < arraysize; i++)
            {
                pulse = pulseIn(sensorReadPin, HIGH);
                rangevalue[i] = pulse / 58;
                delay(10);
            }

            //We have 11 samples report the median to the levels array and shut off the sensor
            isort(rangevalue, arraysize);
            modE = mode(rangevalue, arraysize);
            levels[hourCount] = modE;
            Serial.println((String) "The mode at " + (hourCount) + " is " + levels[hourCount]);
            measuring = false;
            pulseIn(sensorReadPin, LOW);
        }
    }

    //An hour has passed reset the minute count and request the sensor to start reading again, also increment the hour reading
    if (minuteCount > HOUR_VALUE)
    {
        minuteCount = 0;
        hourCount++;
        measuring = true;
    }

    //8 Hours has passed, create the levels array and send it to the satelite. Reset the hours count to 0 to start the 8 hour cycle again.
    if (hourCount > REPORT_PERIOD - 1)
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
        lvl_message.toCharArray(txBuffer, txMsgLen);
        Serial.println(txBuffer);
        hourCount = 0;
    }
}

//Sorting function

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

//Mode function, returning the mode or median.

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
        { //If the dataset has 2 or more modes.
            bimodal = 1;
        }
        if (mode == 0 || bimodal == 1)
        { //Return the median if there is no mode.
            mode = x[(n / 2)];
        }
        return mode;
    }
}