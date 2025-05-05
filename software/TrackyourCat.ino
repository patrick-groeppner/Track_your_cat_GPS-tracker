/* This program will write GPS NMEA data to a micro SD card every X minutes */

#include <Wire.h>
#include <LowPower.h>
#include <MCP7940.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>
//#include <TinyGPS++.h> /* used to decode the nmea messages into timestamps and geografical coordinates */


// The Arduino pins used
static const int GPS_ONOFFPin = 7;
#define GPS_RXPin A0
#define GPS_TXPin A2
#define GPS_SYSONPin A1
static const int ISRwakeUpPin = 2; // pin 2 is wake up pin = INT0
static const int chipSelect = 10;
static const int LEDyellow = 5;
static const int LEDred = 6;

boolean mfpPinTriggered = true; //for single loop after startup

// How often should the positions be written to the sd card after every x minutes?
const uint8_t writeIterations = 200; 

static const uint8_t GPSBaud = 4800;
static const uint32_t serial_baud = 115200;     //Set the baud rate for Serial I/O comm
const uint8_t  SPRINTF_BUFFER_SIZE = 32;  //Buffer size for sprintf()
const uint8_t  ALARM_INTERVAL = 20;       //Interval of X min

/* Enumeration of MCP7940 alarm types */
enum alarmTypes {
  matchSeconds,
  matchMinutes,
  matchHours,
  matchDayOfWeek,
  matchDayOfMonth,
  Unused1,
  Unused2,
  matchAll,
  Unknown
  };
  
MCP7940_Class MCP7940;
char inputBuffer[SPRINTF_BUFFER_SIZE];  //Buffer for sprintf() or sscanf()

// The GPS connection is attached with a software serial port
SoftwareSerial Gps_serial(GPS_RXPin, GPS_TXPin);


int inByte = 0; // incoming serial byte
byte pbyGpsBuffer[100];
int byBufferIndex = 0;


void wakeUp() {
    //handler for the pin interrupt.
    mfpPinTriggered = true;
    
    // Disable external pin interrupt on wake up pin: prevent multiple wakeups
    detachInterrupt(0);
}



/*********************** 
********SETUP***********
***********************/
void setup()
{ 
  
  // Open the debug serial port
  Serial.begin(serial_baud);
 
  // Open the GPS serial port  
  Gps_serial.begin(GPSBaud);

  pinMode(GPS_SYSONPin, INPUT);
  pinMode(GPS_ONOFFPin, OUTPUT);
  digitalWrite(GPS_ONOFFPin, LOW );
  delay(5);
  pinMode(ISRwakeUpPin, INPUT);

  //led pinmodes
  pinMode(LEDyellow, OUTPUT);
  pinMode(LEDred, OUTPUT);
  digitalWrite(LEDyellow, LOW);
  digitalWrite(LEDred, LOW);

  //To save some extra power it is recommended to set every unused pin to OUTPUT mode
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(A3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(3, OUTPUT);

  pinMode(chipSelect, OUTPUT); // chip select pin set to output
  
   //Waking up the the GPS chip
  if (digitalRead(GPS_SYSONPin) == LOW)
  {
    digitalWrite(GPS_ONOFFPin, HIGH); 
    delay(100); 
    digitalWrite(GPS_ONOFFPin, LOW);
    delay(1000);      
  }

  while (!MCP7940.begin())  //Loop until the RTC communications are established
  {
    Serial.println(F("Unable to find MCP7940. Checking again in 3s."));
    delay(3000);
  }
  
  Serial.println(F("MCP7940 initialized."));
  while (!MCP7940.deviceStatus())  // Turn oscillator on if necessary
  {
    Serial.println(F("Oscillator is off, turning it on."));
    bool deviceStatus = MCP7940.deviceStart();  // Start oscillator and return state
    if (!deviceStatus) {
      Serial.println(F("Oscillator did not start, trying again."));
      delay(1000);
    }  
  }
  
  Serial.println("Setting MCP7940M to date/time of library compile");
  MCP7940.adjust();  // Use the compilers date/time to set clock
  Serial.print("Date/Time set to ");
  DateTime now = MCP7940.now();  // get the current time
  
  // Use sprintf() to pretty print date/time with leading zeroes
  sprintf(inputBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  Serial.println(inputBuffer);
  
  MCP7940.setAlarm(0, matchAll, now, true);
  
  // see if the card is present and can be initialized:
  Serial.print("Initializing SD card...");
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    return;
  }
  Serial.println("card initialized.");

  digitalWrite(LEDred, HIGH); // indicate power is on
}




/********************** 
********LOOP***********
***********************/
void loop()
{
  static uint8_t secs;
  DateTime now = MCP7940.now();  // get the current time
  
  if (secs != now.second())      // Output if seconds have changed
  {
 // sprintf(inputBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
 // Serial.print(inputBuffer);
    secs = now.second();     // Set the counter for later comparision
    
    if (MCP7940.isAlarm(0))  // When alarm 0 is triggered
    {
    //Serial.print(" *Alarm1* resetting to go off next at ");
      now = now + TimeSpan(0, 0, ALARM_INTERVAL, 0);  // Add interval to current time
    //sprintf(inputBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
      Serial.print(inputBuffer);
      MCP7940.setAlarm(0, matchAll, now, true);  // Set alarm to go off in 20 min again
    }
    Serial.println();
  }
  
  // Allow wake up pin to trigger interrupt on low.
  attachInterrupt(0, wakeUp, LOW);
  
  if (mfpPinTriggered == true) {

  mfpPinTriggered = false;
  };
  
  
  // Enter power down state with ADC and BOD module disabled.
  // Wake up when wake up pin is low.
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF); 
 

  if (!SD.begin(chipSelect)) {
    return;
  }
  
  //Wakeup the GPS chip and wait for a fix
  if (digitalRead(GPS_SYSONPin) == LOW)
  {
  digitalWrite( GPS_ONOFFPin, HIGH ); 
  delay(100); 
  digitalWrite( GPS_ONOFFPin, LOW );
  while (digitalRead(GPS_SYSONPin) == LOW){
  delay(1000);
  };

for(int k=1; k <= writeIterations; k++) {
  byte byDataByte;
  if (Gps_serial.available())
  {
     byDataByte = Gps_serial.read();
    
     Serial.write(byDataByte);
     pbyGpsBuffer[byBufferIndex++] = byDataByte;
     
     if(byBufferIndex >= 100)
     {
       byBufferIndex = 0;       
       File dataFile = SD.open("gps.txt", FILE_WRITE);
    
       // if the file is available, then write to it:
       if (dataFile) {
        dataFile.write(pbyGpsBuffer, 100);  //use dataFile.println(pbyGpsBuffer) if it overwrites the data
        dataFile.close();
      }  
      // if the file isn't open, pop up an error:
      else {
        Serial.println("error opening gps.txt");
      }
     }
    }
  }

  //Turn off the GPS chip and then wait some seconds
  if (digitalRead(GPS_SYSONPin) == HIGH)
  {
  digitalWrite(GPS_ONOFFPin, HIGH); 
  delay(100); 
  digitalWrite(GPS_ONOFFPin, LOW);
  while (digitalRead(GPS_SYSONPin) == HIGH){
    delay(500);
  }
 }
}
}
