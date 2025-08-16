#include <SoftwareSerial.h>

// Set the variables for the sensor reading
static const int sensorReadPin = 7;
long pulse;

// Variables for sending the message

void setup()
{
    Serial.println((String) "The setup is running");
    Serial.begin(9600);
    while (!Serial)
        pinMode(sensorReadPin, INPUT);
    pulseIn(sensorReadPin, LOW);
}

void loop()
{
    pulse = pulseIn(sensorReadPin, HIGH);
    Serial.println((String) "Reading is " + (pulse / 58));
    delay(1000);
    pulse = pulseIn(sensorReadPin, LOW);
}
