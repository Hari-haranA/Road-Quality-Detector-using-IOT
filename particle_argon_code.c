#include <MPU6050.h>
#include "NMEAGPS.h"

#define EEPROM_ADDR 0x50



// fill these values in with the network address of the computer running the Python script

byte[] PYTHON_SERVER = {192, 168, 1, 18};


// Causes the Particle device to start the code immediately rather than waiting for a WiFi connection

SYSTEM_MODE(MANUAL);


// holds the data for each sensor reading that will be stored in EEPROM

typedef struct {
    int32_t t;
    int32_t lat;
    int32_t lng;
    float accz;
} sensorReading_t;



// local variables used for reading data from the MPU6050 and GPS

MPU6050 mpu;
NMEAGPS gps;
gps_fix fix;
sensorReading_t reading;

// variables used to maintain a rolling mean of the sensor data used as a baseline for detecting spikes in the acceleration data

#define ROLLING_WINDOW 50
#define ACCZ_THRESHOLD 0.17 // 0.17G's

int32_t avgAZ;
uint32_t lastAvgAZ;
uint32_t avgAZSamples;
uint16_t numStoredRecords;
int32_t circleBuf[ROLLING_WINDOW];
uint32_t circleBufIndex;
int32_t circleBufSum;

bool detectedBump;
float detectedAccZ;


// these LED status modes can be activated to display when the device is running or when it detected a bump

LEDStatus statusNormal(0x000000, LED_PATTERN_SOLID, LED_PRIORITY_NORMAL);
LEDStatus statusDetected(RGB_COLOR_GREEN, LED_PATTERN_SOLID, LED_PRIORITY_IMPORTANT);



// I2C functions for writing data buffers to the EEPROM module

void writeEEPROM(uint16_t addr, uint8_t data ) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(addr >> 8);   // MSB
    Wire.write(addr & 0xff); // LSB
    Wire.write(data);
    Wire.endTransmission();
    delay(5);
}

void writeEEPROM(uint16_t addr, uint8_t* buffer, uint8_t length ) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(addr >> 8);   // MSB
    Wire.write(addr & 0xff); // LSB
    Wire.write(buffer, length);
    Wire.endTransmission();
    delay(5);
}
 
uint8_t readEEPROM(uint16_t addr) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(addr >> 8);   // MSB
    Wire.write(addr & 0xff); // LSB
    Wire.endTransmission();
    
    Wire.requestFrom(EEPROM_ADDR, 1);
    if (Wire.available()) 
        return Wire.read();
    return 0xff;
}

uint8_t readEEPROM(uint16_t addr, uint8_t* buffer, uint8_t length) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(addr >> 8);   // MSB
    Wire.write(addr & 0xff); // LSB
    Wire.endTransmission();
    
    uint8_t read = 0;
    Wire.requestFrom(EEPROM_ADDR, length);
    while(Wire.available()) {
        *buffer++ = Wire.read();
        read++;
    }
    return read;
}



// the first 2 bytes in the EEPROM represent how many records there are in the remainder of the EEPROM

uint16_t getNumStoredRecords() {
    uint16_t num;
    readEEPROM(0, (uint8_t*)&num, 2);
    return num;
}

void setNumStoredRecords(uint16_t num) {
    writeEEPROM(0, (uint8_t*)&num, 2);
}


// this function will attempt a connection to the device's known WiFi network
// if it connects successfully, it will attempt to communicate with the Python script to upload its records to the Firebase Database

void tryConnectAndUpload() {
    
    // try to connect to the WiFi network
    
    Serial.println("Connecting...");
    WiFi.on();
    WiFi.connect();
    
    long t0 = millis();
    while(!WiFi.ready() && millis() - t0 < 30000); // wait for a connection or timeout after 30s
    if(!WiFi.ready()) {
        Serial.println("TIMEOUT");
    } else {
        TCPClient client;
        Serial.println("Connecting to batcher");
        
        // try to connect the the Python server based on the address at the top of this code
        if(client.connect(PYTHON_SERVER, 5000)) {
            
            client.print(System.deviceID());                                        // send the device ID to Python
            client.write((uint8_t*)&numStoredRecords, 2);                           // send the number of records to Python
            
            // send the binary data for the records to Python
            for(uint16_t i = 0; i < numStoredRecords; i++) {
                readEEPROM((1 + i) * 16, (uint8_t*)&reading, 16); // read each record from the EEPROM module into the 'reading' variable which is a sensorReading_t struct
                NeoGPS::time_t t = (NeoGPS::clock_t)reading.t;
                String data = String::format("{\"t\": \"%d-%02d-%02dT%02d:%02d:%02dZ\", \"lat\": %f, \"lng\": %f, \"accz\": %f}", t.year+2000, t.month, t.date, t.hours, t.minutes, t.seconds, reading.lat / 1.0e7, reading.lng / 1.0e7, reading.accz);
                Serial.println(data);
                
                // send the raw byte data
                client.write((uint8_t*)&reading, 16);
            }
        } else {
            Serial.println("ERROR"); // the device was unable to connect to the Python server
        }
        numStoredRecords = 0;
        setNumStoredRecords(numStoredRecords);
        Serial.println("Cleared all records");
    }

    // turn WiFi off to conserve battery power
    WiFi.off();
    Serial.println("WiFi off");
}



// ENTRY POINT OF THE PROGRAM

void setup() {
    
    // begin communication with Serial and I2C
    Wire.begin();
    Serial.begin(115200);
    Serial1.begin(9600);            // start Serial connection to NMEA GPS module


    // initialize the MPU6050 module
    Serial.println("Initializing MPU6050");
    mpu.initialize();
    delay(500);
    Serial.println(mpu.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");
    
    
    
    // check how many records are stored in EEPROM
    numStoredRecords = getNumStoredRecords();
    Serial.print("Stored records: ");
    Serial.println(numStoredRecords);
    if(numStoredRecords > 0) {
        tryConnectAndUpload();                                  // try to upload the records if there are any
    }
    
    statusNormal.setActive(true);              // turn off the status led by setting statusNormal to active
    lastAvgAZ = millis();
}

void loop() {
    
    // try to read data from the GPS module
    while(gps.available(Serial1)) {
        fix = gps.read();
        
        // if a bump was detected since the last reading, save the time and location to the EEPROM module
        if(detectedBump) {
            reading.t = (time_t)fix.dateTime;
            reading.lat = fix.latitudeL();
            reading.lng = fix.longitudeL();
            reading.accz = detectedAccZ;
            
            // reset detection variables for the rolling mean comparison
            detectedBump = false;
            detectedAccZ = 0;
            
            Serial.print(reading.t); Serial.print(' ');
            Serial.print(reading.lat / 1e7); Serial.print(' ');
            Serial.print(reading.lng / 1e7); Serial.print(' ');
            Serial.print(reading.accz); Serial.print('\n');
            
            // write the buffer to EEPROM and increase the counter for the number of records
            writeEEPROM((1 + numStoredRecords) * 16, (uint8_t*)&reading, 16);
            setNumStoredRecords(++numStoredRecords);
            
            // flash the LED to signal a bump was stored
            statusDetected.setActive(true);
            delay(200);
            statusDetected.setActive(false);
        }
    }
    
    // add the current acceleration to the total to get an average acceleration every 20ms
    avgAZ += mpu.getAccelerationZ();
    avgAZSamples++;
    
    // use the average acceleration every 20ms
    if(millis() - lastAvgAZ >= 20) {
        lastAvgAZ = millis();
        avgAZ /= avgAZSamples;
        
        // if the circle buffer is full, compare the current average acceleration with the current rolling mean of the data
        if(circleBufIndex >= ROLLING_WINDOW) {
            float relAmpAccZ = abs(avgAZ * ROLLING_WINDOW - circleBufSum) / 16384.0 / ROLLING_WINDOW;         // computationally cheap way to compute the rolling mean of the acceleration data
            
            // if the amplitude of the current acceleration is beyond a threshold away from the rolling mean, a bump was detected 
            if(relAmpAccZ > ACCZ_THRESHOLD && relAmpAccZ > detectedAccZ) {
                detectedAccZ = relAmpAccZ;
                detectedBump = true;
            }
            
            // subtracts the last element of the circular buffer from the current sum of the data to keep computation speed fast
            circleBufSum -= circleBuf[circleBufIndex % ROLLING_WINDOW];
        }
        
        // append the average Z axis acceleration to the circle buffer for maintaining a rolling window of the baseline acceleration data
        circleBuf[circleBufIndex++ % ROLLING_WINDOW] = avgAZ;
        circleBufSum += avgAZ;

        // reset the average acceleration data for every 20ms
        avgAZ = 0;
        avgAZSamples = 0;
    }
    
}





