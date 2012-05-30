#include <PString.h>
#include <SoftwareSerial.h>
#include <TinyGPS.h>
#include <string.h>

/*****************************************
 * Error codes
 */
const int ERROR_GPS_UNAVAIL  = 0;
const int ERROR_GPS_STALE    = 1;
const int ERROR_SIM_UNAVAIL  = 2;
const int ERROR_GPRS_FAIL    = 3;
const int ERROR_GPRS_UNKNOWN = 4;
const boolean DEBUG         = true;

/******************************************
 * Pin Definitions
 */

// UART MODE
int GPS_TX = 0;
int GPS_RX = 1;

// DLINE mode
//int GPS_TX = 2;
//int GPS_RX = 3;

int GPRS_TX = 2;
int GPRS_RX = 3;

int LED_STATUS = 13;
int LED_ERROR = 12;

/*****************************************
 * Buffers
 */
const int BUFFSIZE = 90;
char at_buffer[BUFFSIZE];
char buffidx;
char incoming_char = 0;
char buffer[60];

/*****************************************
 * Registers
 */
boolean firstTimeInLoop = true;
boolean GPRS_registered = false;
boolean GPRS_AT_ready   = false;

/*****************************************
 * Misc variables
 */
 
const int POLL_TIME = 1000; // The amount of time in milliseconds to poll for gps data;
const int PRECISION = 9; // Precision of lat/long coordinates
const int DIAG_DELAY = 2000; // time to pause before replaying diagnostic codes
const int SEND_DELAY = 10000; // The time to wait before sending the next coordinates

TinyGPS gps;
SoftwareSerial cell(GPRS_TX, GPRS_RX);
PString myStr(buffer, sizeof(buffer));

void setup() {
  Serial.println("Arduino powered GPS Tracker initializing");
    
  pinMode(LED_STATUS, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  
  Serial.begin(4800);
  cell.begin(9600);
  
  // Check LEDS
  blinkLed(LED_STATUS, 5, 100);
  blinkLed(LED_ERROR, 5, 100);

}

/*********************************************************
 * GPRS Functions
 */
 
/**
 * store the serial string in a buffer until we receive a newline
 */
static void readATString() {
  char c;
  buffidx = 0;
  
  while (1) {
    if (cell.available() > 0) {
      c = cell.read();
      if (c == -1) {
        at_buffer[buffidx] = '\0';
        return;
      }
      
      if (c == '\n') {
        continue;
      }
      
      if ((buffidx == BUFFSIZE - 1) || (c == '\r')) {
        at_buffer[buffidx] = '\0';
        return;
      }
      
      at_buffer[buffidx++] = c;
    } 
  }
}

/**
 * Send an AT command to the cell interface
 */
static void sendATCommand(const char* ATCom, int dly) {
  cell.println(ATCom);
  readATString();
  //ProcessATString();
  
  if (DEBUG) {
    Serial.print("COMMAND: ");
    Serial.print(ATCom);
    Serial.print(" RESPONSE: ");
    Serial.println(at_buffer);
  }
  delay(dly);
}

/*
 * Handle response codes
 */
static void ProcessATString() {
  
  if (DEBUG) {
    Serial.println(at_buffer);
  }
  
  if (strstr(at_buffer, "+SIND: 1") != 0) {
      GPRS_registered = false;
      GPRS_AT_ready = false;
  }
  
  if (strstr(at_buffer, "+SIND: 10,\"SM\",0,\"FD\",0,\"LD\",0,\"MC\",0,\"RC\",0,\"ME\",0") != 0) {
    error(ERROR_SIM_UNAVAIL);
  }
  if (strstr(at_buffer, "+SIND: 10,\"SM\",1,\"FD\",1,\"LD\",1,\"MC\",1,\"RC\",1,\"ME\",1") != 0) {
    Serial.println("SIM card Detected");
    successLED();
  }
  
  if (strstr(at_buffer, "+SIND: 8") != 0) {
    GPRS_registered = false;
    Serial.println("GPRS Network Not Available");
    error(ERROR_GPRS_FAIL);
  }
  
  if (strstr(at_buffer, "+SIND: 11") != 0) {
    GPRS_registered = true;
    Serial.println("GPRS Registered");
    successLED();
  }
  
  if (strstr(at_buffer, "+SIND: 4") != 0) {
     GPRS_AT_ready = true;
     Serial.println("GPRS AT Ready");
     successLED();
  }
  
  if (strstr(at_buffer, "+CME ERROR") != 0) {
    error(ERROR_GPRS_UNKNOWN);
  }
  
  if (strstr(at_buffer, "OK") != 0) {
     successLED();
  }
}

static void establishNetwork() {
  while (GPRS_registered == false || GPRS_AT_ready == false) {
    readATString();
    ProcessATString();
  }
  
  Serial.println("Setting up PDP Context");
  sendATCommand("AT+CGDCONT=1,\"IP\",\"wap.cingular\"", 1000);
    
  Serial.println("Configure APN");
  sendATCommand("AT+CGPCO=0,\"\",\"\", 1", 1000);
    
  Serial.println("Activate PDP Context");
  sendATCommand("AT+CGACT=1,1", 1000);
    
//  Serial.println("Configuring TCP connection to server");
//  sendATCommand("AT+SDATACONF=1,\"TCP\",\"174.129.39.158\",81", 1000);
//    
//  Serial.println("Starting TCP Connection");
//  sendATCommand("AT+SDATASTART=1,1", 1000);
//
//  Serial.println("Getting status");
//  sendATCommand("AT+SDATASTATUS=1", 3000);
  
  digitalWrite(LED_STATUS, HIGH);

}

static void sendData(const char* data) {
  Serial.println("Configuring TCP connection to server");
  sendATCommand("AT+SDATACONF=1,\"TCP\",\"0.0.0.0\",81", 1000);
    
  Serial.println("Starting TCP Connection");
  sendATCommand("AT+SDATASTART=1,1", 1000);

  Serial.println("Getting status");
  sendATCommand("AT+SDATASTATUS=1", 3000);
  
  Serial.println("Sending data");
  sendATCommand(data, 1000);
  
  Serial.println("Getting status");
  sendATCommand("AT+SDATASTATUS=1", 3000);
  myStr.begin();
  successLED();
  sendATCommand("AT+SDATASTART=1,0", 1000);
}

/*********************************************************
 * GPS Functions
 */
 
static void pollGPS(TinyGPS &gps) {
      unsigned long age;
      float lat, lng, speed;
      char course;
      
      // Acquire data from gps
      gps.f_get_position(&lat, &lng, &age);
      speed = gps.f_speed_mph();
      course = gps.course();
      
      if (lat == TinyGPS::GPS_INVALID_F_ANGLE) {
        error(ERROR_GPS_UNAVAIL);
      } 
      
      /*
       * create string of "speed,course,latitude,longitude" to send to server
       */
      myStr.print("AT+SDATATSEND=1,10,\"");
      myStr.print(speed);
      myStr.print(",");
      myStr.print(course);
      myStr.print(",");
      myStr.print(lat, DEC);
      myStr.print(",");
      myStr.print(lat, DEC);
      myStr.print("\"");
      
}

static bool gpsAvailable() {
  while (Serial.available())
  {
    if (gps.encode(Serial.read()))
      return true;
  }
  
  error(ERROR_GPS_UNAVAIL);
  return false;
}


/*********************************************************
 * Misc functions
 */
 
/**
 * Flash an LED X amount of times and write to console based on the error code given
 */
static void error(const int errorCode) {
  int flashTimes = 0;
  int i = 0;
  
  switch(errorCode) {
    case ERROR_GPS_UNAVAIL:
      flashTimes = 2;
      Serial.println("ERROR: GPS Unavailable");
      break;
    case ERROR_SIM_UNAVAIL:
      flashTimes = 3;
      Serial.println("ERROR: SIM Unavailable");
      break;
    case ERROR_GPRS_FAIL:
      flashTimes = 4;
      Serial.println("ERROR: Connection to network failed");
      break;
    case ERROR_GPRS_UNKNOWN:
      flashTimes = 5;
      Serial.println("ERROR: Unknown");
      break;
  }
  
  blinkLed(LED_ERROR, flashTimes, 500);

}

/** 
 * Blink a few times so we know all is right with the world
 */
static void successLED() {
  blinkLed(LED_STATUS, 5, 100);
}

/**
 * Blink LED
 */
static void blinkLed(int lPin, int flashTimes, int dly) {
  for (int i = 0; i < flashTimes; i++) {
    digitalWrite(lPin, HIGH); 
    delay(dly);
    digitalWrite(lPin, LOW);
    delay(dly);
  } 
  delay(DIAG_DELAY);
}


/*********************************************************
 * Main Loop
 */
void loop() {
  if (GPRS_registered == false || GPRS_AT_ready == false) {
    Serial.println("establishing");
    establishNetwork();
  }
  
  Serial.println("Connection established: acquiring GPS");
  

  if (gpsAvailable()) {
    Serial.println("acquired gps");
    // Retrieve GPS Data
    pollGPS(gps);
  
    // Send data to cell network
    sendData(myStr);
  }
  
  delay(SEND_DELAY);
}



