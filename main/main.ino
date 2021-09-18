#include <SoftwareSerial.h>

#define MINUTE_VALUE 60
#define HOUR_VALUE 29
#define REPORT_PERIOD 8
//Set the variables for the sensor
static const int sensorPin = 9;
long range;

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

long runSum;
int samples;
bool measuring;

void setup()
{
    beginningOfTime = millis();
    Serial.begin(9600);
    while (!Serial)
    {
        ; // Wait to connect serial port. For native USB port only || Remove once live
    }
    //Confirgure the pins used
    pinMode(sensorPin, OUTPUT);
    digitalWrite(sensorPin, LOW);
}

void loop()
{
    currentTime = millis();
    if ((currentTime - beginningOfTime) < 0)
    {
        beginningOfTime = currentTime;
    }

    if ((currentTime - beginningOfTime) > MINUTE_VALUE)
    { /*1 min*/
        minuteCount++;
        beginningOfTime = millis();

        //other things to do on a 1 minute interval
        if (measuring)
        {
            // Take a 5 minute river gauge average
            runSum = (analogRead(A0) * 2) + runSum;
            Serial.println((String) "Run sum is " + runSum);
            samples++;

            //We have 5 samples report the average to the levels array and shut off the sensor
            if (samples >= 10)
            {
                levels[hourCount] = runSum / samples;
                runSum = 0;
                samples = 0;
                measuring = false;
                digitalWrite(sensorPin, LOW);
            }
        }
    }

    if (minuteCount > HOUR_VALUE)
    {
        minuteCount = 0;
        hourCount++;
        measuring = true;
        Serial.println((String) "New reading at hour " + hourCount);
        digitalWrite(sensorPin, HIGH);
        delay(5000);
    }

    if (hourCount > REPORT_PERIOD - 1)
    {
        lvl_message = "";
        for (j = 0; j < REPORT_PERIOD; j++)
        {
            lvl_message.concat(levels[j]);
            if (j < (REPORT_PERIOD - 1))
            {
                lvl_message.concat(",");
            }
            else
            {
                lvl_message.concat("\0");
            }
        }

        txMsgLen = lvl_message.length() + 1;
        lvl_message.toCharArray(txBuffer, txMsgLen);
        Serial.println((String) "txBuffer is " + txBuffer);
        Serial.println((String) "txMsgLen is " + txMsgLen);
        hourCount = 0;
    }
}
