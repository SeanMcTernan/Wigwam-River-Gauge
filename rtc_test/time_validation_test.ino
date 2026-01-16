#include <Wire.h>
#include "ds3231.h"
#include <time.h>

struct ts t;

void setup() {
    Serial.begin(115200);
    while (!Serial);
    
    Serial.println(F("Time Validation Test Starting..."));
    
    // Initialize I2C and DS3231 RTC
    Wire.begin();
    Wire.setClock(400000);
    DS3231_init(DS3231_CONTROL_INTCN);
    DS3231_clear_a1f();
    
    // Test MDT conversion with known UTC times
    testMDTConversion();
    
    // Display current RTC time
    displayCurrentTime();
}

void loop() {
    // Display time every 10 seconds
    displayCurrentTime();
    delay(10000);
}

void testMDTConversion() {
    Serial.println(F("\n=== Testing MDT Conversion ==="));
    
    // Test case 1: Normal conversion (no date rollback)
    struct tm test_time1;
    test_time1.tm_year = 124;  // 2024
    test_time1.tm_mon = 9;     // October (0-based)
    test_time1.tm_mday = 20;
    test_time1.tm_hour = 18;   // 6 PM UTC
    test_time1.tm_min = 30;
    test_time1.tm_sec = 0;
    
    Serial.println(F("Test 1: Normal conversion (18:30 UTC -> 12:30 MDT)"));
    setRTCFromUTC(test_time1);
    
    delay(2000);
    
    // Test case 2: Date rollback (early morning UTC)
    struct tm test_time2;
    test_time2.tm_year = 124;  // 2024
    test_time2.tm_mon = 9;     // October (0-based)
    test_time2.tm_mday = 21;
    test_time2.tm_hour = 3;    // 3 AM UTC
    test_time2.tm_min = 15;
    test_time2.tm_sec = 0;
    
    Serial.println(F("\nTest 2: Date rollback (03:15 UTC -> 21:15 MDT previous day)"));
    setRTCFromUTC(test_time2);
}

// Helper function to calculate day of week (0=Sunday, 1=Monday, etc.)
uint8_t calculateDayOfWeek(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + ((13 * (month + 1)) / 5) + k + (k / 4) + (j / 4) - 2 * j) % 7;
    return (h + 5) % 7 + 1; // Convert to 1-7 range (1=Sunday)
}

// Helper function to calculate day of year
uint16_t calculateDayOfYear(int year, int month, int day) {
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29;
    }
    
    int dayOfYear = day;
    for (int i = 0; i < month - 1; i++) {
        dayOfYear += days_in_month[i];
    }
    
    return dayOfYear;
}

// Function to set RTC from UTC satellite time with MDT conversion
bool setRTCFromUTC(const struct tm &utc_time) {
    struct ts rtc_time;
    
    int year = utc_time.tm_year + 1900;
    int month = utc_time.tm_mon + 1;
    int day = utc_time.tm_mday;
    int hour = utc_time.tm_hour;
    int minute = utc_time.tm_min;
    int second = utc_time.tm_sec;
    
    char utc_buf[32];
    sprintf(utc_buf, "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    Serial.print(F("UTC time before MDT conversion: "));
    Serial.println(utc_buf);
    
    // Apply Mountain Daylight Time offset (UTC-6)
    hour -= 6;
    
    // Handle hour underflow (date rollback)
    if (hour < 0) {
        hour += 24;
        day--;
        
        if (day < 1) {
            month--;
            
            if (month < 1) {
                month = 12;
                year--;
            }
            
            int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            
            if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
                day = 29;
            } else {
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
    rtc_time.isdst = 1;
    rtc_time.year_s = year % 100;
    
    // Set the RTC
    DS3231_set(rtc_time);
    
    char confirmation[32];
    sprintf(confirmation, "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    Serial.print(F("RTC time set to (MDT): "));
    Serial.println(confirmation);
    
    // Verify the setting
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

void displayCurrentTime() {
    DS3231_get(&t);
    
    char time_buf[32];
    sprintf(time_buf, "%04d-%02d-%02d %02d:%02d:%02d",
            t.year, t.mon, t.mday, t.hour, t.min, t.sec);
    Serial.print(F("Current RTC time (MDT): "));
    Serial.println(time_buf);
}
