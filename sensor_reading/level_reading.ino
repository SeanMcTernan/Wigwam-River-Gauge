#include <SoftwareSerial.h>

#define txPin 11 //define pins used for software serial for sonar
#define rxPin 10

SoftwareSerial sonarSerial(rxPin, txPin, true); //define serial port for recieving data, output from maxSonar is inverted requiring true to be set.
boolean loopStringComplete = false;

void setup()
{
  Serial.begin(9600);
  while (!Serial)
  {
    ; // Wait to connect serial port. For native USB port only
  }
}

void loop()
{
  int range = loopSerialValueRead();
  if (loopStringComplete)
  {
    loopStringComplete = false;                   //reset sringComplete ready for next reading
    Serial.println((String) "Range is " + range); // Line for Debugging
    delay(5000);                                  //delay for debugging
  }
}

int loopSerialValueRead()
{
  int result;
  char inData[4]; //char array to read data into
  int index = 0;
  sonarSerial.flush(); // Clear cache ready for next reading
  sonarSerial.begin(9600);
  while (loopStringComplete == false)
  {

    if (sonarSerial.available())
    {
      char rByte = sonarSerial.read(); //read serial input for "R" to mark start of data
      if (rByte == 'R')
      {
        while (index < 4) //read next three character for range from sensor
        {
          if (sonarSerial.available())
          {
            inData[index] = sonarSerial.read();
            index++; // Increment where to write next
          }
        }
        inData[index] = 0x00; //add a padding byte at end for atoi() function
      }

      rByte = 0; //reset the rByte ready for next reading

      index = 0;                 // Reset index ready for next reading
      loopStringComplete = true; // Set completion of read to true
      sonarSerial.end();
      result = atoi(inData); // Changes string data into an integer for use
    }
  }

  return result;
}

int calculateAverage()
{
  int result;
  int firstReading[4];
  int index = 0;

  while (index < 4)
  {
    firstReading[index] = loopSerialValueRead();
    Serial.println(firstReading[index]);
    index++;
  }
  result = firstReading[1];
  return result;
}
