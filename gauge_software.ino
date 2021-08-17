#include <SoftwareSerial.h>

#define txPin 11 //define pins used for software serial for sonar
#define rxPin 10

SoftwareSerial sonarSerial(rxPin, txPin, true); //define serial port for recieving data, output from maxSonar is inverted requiring true to be set.

boolean stringComplete = false;
boolean stringComplete2 = false;
boolean zeroCalculated = false;

void setup()
{
  Serial.begin(9600);
  while (!Serial)
  {
    ; // Wait to connect serial port. For native USB port only
  }
  calculateZero();
  while (!zeroCalculated)
  {
    ; // Wait to for base level to be calculated before initializing the loop
  }
}

void loop()
{
  int range = serialValueRead2();
  if (stringComplete2)
  {
    stringComplete2 = false;                      //reset sringComplete ready for next reading
    Serial.println((String) "Range is " + range); // Line for Debugging
    delay(5000);                                  //delay for debugging
  }
}

int serialValueRead()
{
  int result;
  char inData[4]; //char array to read data into
  int index = 0;
  sonarSerial.flush(); // Clear cache ready for next reading
  sonarSerial.begin(9600);
  while (stringComplete == false)
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

      index = 0;             // Reset index ready for next reading
      stringComplete = true; // Set completion of read to true
      sonarSerial.end();
      result = atoi(inData); // Changes string data into an integer for use
    }
  }

  return result;
}

int serialValueRead2()
{
  int result;
  char inData[4]; //char array to read data into
  int index = 0;
  sonarSerial.flush(); // Clear cache ready for next reading
  sonarSerial.begin(9600);
  while (stringComplete2 == false)
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

      index = 0;              // Reset index ready for next reading
      stringComplete2 = true; // Set completion of read to true
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
    firstReading[index] = serialValueRead();
    Serial.println(firstReading[index]);
    index++;
  }
  result = firstReading[1];
  return result;
}

int calculateZero() //Calculate the base river level with 4 readings over the course of 1 minute
{
  int result;
  result = 0;
  int average;
  int readings[4];
  int readingIndex = 0;
  while (readingIndex < 4)
  {
    readings[readingIndex] = serialValueRead();
    if (stringComplete)
    {
      stringComplete = false;
      Serial.println((String) "Reading at index " + readingIndex + " is " + readings[readingIndex]); // Line for Debugging
      readingIndex++;
      if (readingIndex != 4)
      {
        delay(3000);
      }
    }
  }

  unsigned int index;

  for (index = 0; index < sizeof(readings) / sizeof(readings[0]); index++) // Add all values in the array
  {
    average += readings[index];
  }
  average = average / 4;

  Serial.println((String) "The average level reading is " + average); // Line for Debugging
  zeroCalculated = true;
  return average;
}
