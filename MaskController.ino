#include <Adafruit_BMP280.h>
#include <Adafruit_LSM6DS33.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_APDS9960.h>
#include <PDM.h>
#include <bluefruit.h>
#include <BLEService.h>
#include <BLEClientService.h>
// #include <AutoPID.h>

// Global Variables

// Set this higher to automatically stop advertising after a time
#define ADV_TIMEOUT 0  // seconds. 
#define DEBUG 1

#define FANPIN 6
#define BATPIN A6

#define OUTPUT_MIN 0
#define OUTPUT_MAX 255
// #define Kp 0.33
// #define Ki 0.0
// #define Kd 0.0

// The following code is for setting a name based on the actual device MAC address
// Where to go looking in memory for the MAC
typedef volatile uint32_t REG32;
#define pREG32 (REG32 *)
#define MAC_ADDRESS_HIGH  (*(pREG32 (0x100000a8)))
#define MAC_ADDRESS_LOW   (*(pREG32 (0x100000a4)))

BLEUart bleUart;
Adafruit_BMP280 bmp280;      // temperautre, barometric pressure
Adafruit_LSM6DS33 lsm6ds33;  // accelerometer, gyroscope
Adafruit_SHT31 sht30;        // humidity
Adafruit_APDS9960 apds9960;  // light, color

//double setPoint = 40.0;
float fanSpeedPercent = 0.99;
float temperature, pressure, altitude, humidity;
float accel_x, accel_y, accel_z;
float gyro_x, gyro_y, gyro_z;
int vBattery = 0;
uint8_t proximity;
uint16_t r, g, b, a;
int32_t mic;

// Null-terminated string must be 1 longer than you set it, for the null
char ble_name[15] = "SmartMaskXXXX\n";
// AutoPID PID(&humidity, &setPoint, &fanSpeed, OUTPUT_MIN, OUTPUT_MAX, Kp, Ki, Kd);

extern PDMClass PDM;
short sampleBuffer[256];   // buffer to read samples into, each sample is 16-bits
volatile int samplesRead;  // number of samples read

/*****************************************************************/
// Functions
void SensorSetup(void);
void FanSetup(void);
void BluetoothSetup(void);

void SensorProcess(void);
void FanProcess(void);
void BluetoothProcess(void);
void BatteryProcess(void);

void startAdv(void);
int32_t getPDMwave(int32_t samples);
void onPDMdata(void);
void byte_to_str(char* buff, uint8_t val);
char nibble_to_hex(uint8_t nibble);
void adv_stop_callback(void);
void SerialPrint(void);
void BTPrint(void);

/*****************************************************************/
// Main program
void setup(void) {
    // Start serial monitor when debug mode is on
    // NOTE: Under debug mode, open serial monitor once
    // to activate bluetooth.
    if (DEBUG) Serial.begin(115200);
    SensorsSetup();
    FanSetup();
    BluetoothSetup();
}

void loop(void) {
    SensorProcess();
    FanProcess();
    BluetoothProcess();
    BatteryProcess();
    BTPrint();
//    if (DEBUG) SerialPrint();

    delay(2000);
}

/*****************************************************************/
// Main functions implementations.

void SensorsSetup(void) {
    // initialize the sensors
    apds9960.begin();
    apds9960.enableProximity(true);
    apds9960.enableColor(true);
    bmp280.begin();
    lsm6ds33.begin_I2C();
    sht30.begin();
    PDM.onReceive(onPDMdata);
    PDM.begin(1, 16000);
}

void FanSetup(void) {
    pinMode(FANPIN, OUTPUT);  // sets the pin as output

    // if humanity is more than n% below or above setpoint,
    // OUTPUT will be set to min or max respectively
    // PID.setBangBang(20);

    // set PID update interval to n ms
    // PID.setTimeStep(500);
}

void BluetoothSetup(void) {
    Bluefruit.begin();
    Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

    // Replace the XXXX with the lowest two bytes of the MAC Address
    // The commented lines show you how to get the WHOLE MAC address
    //  uint32_t addr_high = ((MAC_ADDRESS_HIGH) & 0x0000ffff) | 0x0000c000;
    uint32_t addr_low  = MAC_ADDRESS_LOW;

    // Fill in the XXXX in ble_name
    byte_to_str(&ble_name[9], (addr_low >> 8) & 0xFF);
    byte_to_str(&ble_name[11], addr_low & 0xFF);
    // Set the name we just made
    Bluefruit.setName(ble_name);

    bleUart.begin();

    // start advertising
    startAdv();
}

void startAdv(void) {
    // Advertising packet
    Bluefruit.Advertising.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleUart);

    // Tell the BLE device we want to send our name in a ScanResponse if asked.
    Bluefruit.ScanResponse.addName();

    /* Start Advertising
    * - Enable auto advertising if disconnected
    * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
    * - Timeout for fast mode is 30 seconds
    * - Start(timeout) with timeout = 0 will advertise forever (until connected)
    *
    * For recommended advertising interval
    * https://developer.apple.com/library/content/qa/qa1931/_index.html
    */
    Bluefruit.Advertising.setStopCallback(adv_stop_callback);
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in units of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);  // number of seconds in fast mode
    // Stop advertising entirely after ADV_TIMEOUT seconds
    Bluefruit.Advertising.start(ADV_TIMEOUT);
}

void SensorProcess(void) {
    proximity = apds9960.readProximity();
    while (!apds9960.colorDataReady()) {
      delay(5);
    }
  
    apds9960.getColorData(&r, &g, &b, &a);
    temperature = bmp280.readTemperature();
    pressure = bmp280.readPressure();
    altitude = bmp280.readAltitude(1013.25);

    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;
    lsm6ds33.getEvent(&accel, &gyro, &temp);
    accel_x = accel.acceleration.x;
    accel_y = accel.acceleration.y;
    accel_z = accel.acceleration.z;
    gyro_x = gyro.gyro.x;
    gyro_y = gyro.gyro.y;
    gyro_z = gyro.gyro.z;

    humidity = sht30.readHumidity();

    samplesRead = 0;
    mic = getPDMwave(4000);
}

void FanProcess(void) {
//    PID.run();
    fanSpeedPercent = (0.0098 * humidity) + 0.0257;
    analogWrite(FANPIN, round(fanSpeedPercent * 255));
}

void BluetoothProcess(void) {
    if (Bluefruit.connected()) {
        BTPrint();
    }
}

void BatteryProcess(void) {
    vBattery = round((((analogRead(BATPIN) * 2 * 3.3) / 1024) / 4.2) * 100);
    if (vBattery == 100) --vBattery;
}

/*****************************************************************/
// Helper functions implementations.

int32_t getPDMwave(int32_t samples) {
    short minwave = 30000;
    short maxwave = -30000;

    while (samples > 0) {
        if (!samplesRead) {
            yield();
            continue;
        }
        for (int i = 0; i < samplesRead; i++) {
            minwave = min(sampleBuffer[i], minwave);
            maxwave = max(sampleBuffer[i], maxwave);
            samples--;
        }
        // clear the read count
        samplesRead = 0;
    }
    return maxwave - minwave;
}

void onPDMdata(void) {
    // query the number of bytes available
    int bytesAvailable = PDM.available();

    // read into the sample buffer
    PDM.read(sampleBuffer, bytesAvailable);

    // 16-bit, 2 bytes per sample
    samplesRead = bytesAvailable / 2;
}

// convert an 8-bit byte to a string of 2 hexadecimal characters
void byte_to_str(char* buff, uint8_t val) {
    buff[0] = nibble_to_hex(val >> 4);
    buff[1] = nibble_to_hex(val);
}

// convert a 4-bit nibble to a hexadecimal character
char nibble_to_hex(uint8_t nibble) {
    nibble &= 0xF;
    return nibble > 9 ? nibble - 10 + 'A' : nibble + '0';
}

// Callback invoked when advertising is stopped by timeout
void adv_stop_callback(void) {
    if (DEBUG) Serial.println("Advertising time passed, advertising will now stop.");
}

// Serial print
void SerialPrint(void) {
    Serial.print(temperature);
    Serial.print(",");
    Serial.print(pressure);
    Serial.print(",");
    Serial.print(humidity);
    Serial.print(",");
    Serial.print(altitude);
    Serial.print(",");
    Serial.print(accel_x);
    Serial.print(",");
    Serial.print(accel_y);
    Serial.print(",");
    Serial.print(accel_z);
    Serial.print(",");
    Serial.print(gyro_x);
    Serial.print(",");
    Serial.print(gyro_y);
    Serial.print(",");
    Serial.print(gyro_z);
    Serial.print(",");
    Serial.print(r);
    Serial.print(",");
    Serial.print(g);
    Serial.print(",");
    Serial.print(b);
    Serial.print(",");
    Serial.print(a);
    Serial.print(mic);
    Serial.print(",");
    Serial.print(vBattery);
    Serial.print(",");
    Serial.print(round(fanSpeedPercent * 255));
    Serial.println();
}

// Bluetooth Print
void BTPrint(void) {
    String tempBuff = String(temperature + 0.05);
    tempBuff[tempBuff.length() - 1] = ',';

    tempBuff += String(int(fmod(round(pressure), 100000) * 10));
    tempBuff[tempBuff.length() - 1] = ',';
    
    tempBuff += String(humidity + 0.05);
    tempBuff[tempBuff.length() - 1] = ',';

    tempBuff += (String(vBattery) + String(","));
    
    tempBuff += (String(round(fanSpeedPercent * 100)) + String("\n\r"));
 
    char buff[tempBuff.length() + 1] = {0};
    tempBuff.toCharArray(buff, tempBuff.length());

//    if (DEBUG) Serial.print(buff);
               
    bleUart.write(buff);
    bleUart.flush(); 
 
//    String data = String(temperature) + String(",") + String(pressure) + String(",");
//    bleUart.write(data.c_str());
//    bleUart.flush();
// 
//    data = String(humidity) + String(",") + String(altitude) + String(",");
//    bleUart.write(data.c_str());
//    bleUart.flush();
//
//    data = String(accel_x) + String(",") + String(accel_y) + String(",") + String(accel_z) + String(",");
//    bleUart.write(data.c_str());
//    bleUart.flush();
// 
//    data = String(gyro_x) + String(",") + String(gyro_y) + String(",") + String(gyro_z) + String(",");
//    bleUart.write(data.c_str());
//    bleUart.flush();
//
//    data = String(r) + String(",") + String(g) + String(",") + String(b) + String(",") + String(a) + String(",");
//    bleUart.write(data.c_str());
//    bleUart.flush();
//
//    data = String(mic) + String(",") + String(fanSpeed) + String("\n");
//    bleUart.write(data.c_str());
//    bleUart.flush();
}
