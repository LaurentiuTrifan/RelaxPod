#include <WiFi.h>
#include "BluetoothSerial.h"
#include "SoftwareSerial.h"
//#include <RIMS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


# define Start_Byte 0x7E
# define Version_Byte 0xFF
# define Command_Length 0x06
# define End_Byte 0xEF
# define Acknowledge 0x00 //Returns info with command 0x41 [0x01: info, 0x00: no info]
# define ACTIVATED LOW
# define RED_LED 14
# define YELLOW_LED 34
# define GREEN_LED 12

# define BUTTON_PIN 23

// Check if Bluetooth is available
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Check Serial Port Profile
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif

BluetoothSerial SerialBT;
SoftwareSerial Mp3Serial(21, 22);
HTTPClient http;
const char* API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InV3dmtwZG56ZmVla3h3bXF2ZWpwIiwicm9sZSI6ImFub24iLCJpYXQiOjE3MzE5MTM2ODAsImV4cCI6MjA0NzQ4OTY4MH0.sdImu3hKEWANywrdXv1Fez6-0yqKplN43KO2-2VILCQ";

/*volatile unsigned char TimerFlag = 0; 
void TimerISR() {
   TimerFlag = 1;
}*/

enum RPod_States{RPOD_Idle, RPOD_Credentials, RPOD_SysOn, RPOD_SysOff, RPOD_Upload};

// mp3 variables >>>
static uint8_t songInList = 1;
static uint8_t totalSongs = 7;
static bool isPlaying = false;
static bool isPaused = false;
static bool firstSong = true;

// light variables >>>
const uint8_t echopin = 13;
const uint8_t sonnarTriggerpin = 15;
static uint16_t distance = 30;
static uint16_t readDistance = 30;
static uint8_t median[]  = {30, 30, 30, 30, 30, 30, 30, 30, 30, 30};
static uint8_t iterator = 0;
static uint16_t medianSum = 300;
static long iterations = 0;
//static bool lightSequence = 0;

// authentification global variables >>>
static bool validIDFormat = true;
static bool credentialsVerified = false;
static uint8_t idDigit = 0;
static uint8_t authentificationAttempts = 0;

// user variables >>>
static String userName;
static uint32_t userID;
static uint8_t lightValue = 35;
static uint8_t soundValue = 15;
static uint8_t melody = 1;

// functional variables >>>
static unsigned long startTime = 0;
static bool doorOpened = false;
static bool systemOff = false;
static bool dataUploaded = false;
static uint8_t messageRead = 0;  // for reading SerialBT
static String response;          // general variable for manipulation

RPod_States RPod;

void setup() {
  pinMode(echopin, INPUT);      // reads input signal from sonnar
  pinMode(sonnarTriggerpin, OUTPUT);  // trigger output signal for sonnar
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);  // output dimmable light
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressed, FALLING);

  Serial.begin(115200);

  WiFi.begin("LAPTOP_957UV57G 0621", "W795|6b9");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Wi-Fi connected!");

  Mp3Serial.begin(9600);
  SerialBT.begin("Relaxation Pod");  //Bluetooth device name
  Serial.printf("Relaxation Pod is running.\nYou can pair it with Bluetooth!\n");
  digitalWrite(GREEN_LED, HIGH);
}

// ----------------------  < MP3 functions section > ---------------------- 
void execute_CMD(byte CMD, byte Par1, byte Par2){  // Execute the command and parameters
  // Calculate the checksum (2 bytes)
  word checksum = -(Version_Byte + Command_Length + CMD + Acknowledge + Par1 + Par2);
  // Build the command line
  byte Command_line[10] = { Start_Byte, Version_Byte, Command_Length, CMD, Acknowledge,
  Par1, Par2, highByte(checksum), lowByte(checksum), End_Byte};
  //Send the command line to the module
  for (byte k=0; k<10; k++){
    Mp3Serial.write( Command_line[k]);
  }
}

void setVolume(int volume){
  execute_CMD(0x06, 0, volume); // Set the volume (0x00~0x30)
  delay(2);
}

void BTReadingMP3(){
  if(SerialBT.available()) {
    messageRead = SerialBT.read();
    switch(messageRead){

      case 49:{                   // Volume up     [Up]     ***if not working, try      execute_CMD(0x04, 0, 1);
        if(isPlaying){
          execute_CMD(0x04,0,0);
          if(soundValue > 0){
            soundValue--;
          }
        }
        break;
      }

      case 50:{                   // Volume down   [Down]      *** if not working, try      execute_CMD(0x05, 0, 1);
        if(isPlaying){
          execute_CMD(0x05,0,0);
          if(soundValue < 30){
            soundValue++;
          }
        }
        break;
      }

      case 76:{                   // Prevous song  [Left]
        if(isPlaying && songInList > 1){
          execute_CMD(0x02,0,1);
          songInList--;
        }
        else{
          execute_CMD(0x0D,0,1); // play command - starts the same song from begining
        }
        break;
      }

      case 82:{                   // Next song     [Right]
          if(isPlaying && (songInList < totalSongs)){
            execute_CMD(0x01,0,1);
            songInList++;
          }
        break;
      }

      case 84:{                   // Play          [Triangle]
        isPlaying = true;
        if(firstSong){
          execute_CMD(0x3F,0,0); //Initialization parameters.
          delay(5);
          execute_CMD(0x44,0,4); // classic setup equaliser
          setVolume(soundValue);     // Set the volume (0x00~0x30)
          delay(5);
          execute_CMD(0x11,0,1); // play command
          firstSong = false;
          break;
        }
        if(isPaused){
          isPaused = false;
        }
        execute_CMD(0x0D,0,1);
        break;
      }

      case 83:{                   // Pause         [Square]
        if(isPlaying){
          execute_CMD(0x0E,0,0);
          isPlaying = false;
          isPaused = true;
          break;
        }
        else if(isPaused){
          execute_CMD(0x0D,0,1);
          isPaused = false;
          isPlaying = true;
        }
        break;
      }

      case 88:{                   // Stop          [Cross]
        if(isPlaying){
          execute_CMD(0x16,0,0);
          isPlaying = false;
        }
        break;
      }

      case 67:{                   // Repeat        [Circle]
        if(isPlaying){
          execute_CMD(0x08,songInList,songInList);
        }
        break;
      }

      default:{
        break;
      }
    }
  }
}

void playFirst(){
  execute_CMD(0x3F, 0, 0);
  delay(500);
  setVolume(soundValue);
  delay(500);
  execute_CMD(0x0F,0,melody);
  delay(500);
}

void enableMusicPreferences(){
  if(melody != 0){
    playFirst();
  }
}
// ----------------------  </ MP3 functions section >  ---------------------- 
// ----------------------    ----------------------    ----------------------
// ----------------------  < light functions section >  ---------------------- 
void sonarSequence(){
  digitalWrite(sonnarTriggerpin, LOW);
  delayMicroseconds(2);
  u32_t initialTime = millis();
  digitalWrite(sonnarTriggerpin, HIGH);
  delayMicroseconds(10);
  digitalWrite(sonnarTriggerpin, LOW);
  readDistance = (pulseIn(echopin, HIGH)) * 0.01715;   // speed of sound: 343 m/s = 0.343 mm/microsec;  divided by 2 since the distance is measured 2 times

  if(readDistance < 90){
    if(readDistance > 80){
      readDistance = 80;
    }
    medianSum = medianSum - median[iterator];
    median[iterator] = readDistance;   // speed of sound: 343 m/s = 0.343 mm/microsec; it measures in cm; divided by 2 since the distance is measured twice: when sound is sent and when it returns
    medianSum = medianSum + median[iterator];
    distance = medianSum/10;
    lightValue = distance*80/255;
    iterator = (iterator + 1)%10;
  }
}

void enableLightPreference(){
  uint8_t d = (lightValue*80)/255;
  for(uint i = 0; i < 10; i++){
    median[i] = d;
  }
  medianSum = 10*d;
}

void light(){
  sonarSequence();
  analogWrite(YELLOW_LED, lightValue);
}
// ----------------------  </ light functions section >  ---------------------- 
// ----------------------  ----------------------------  ----------------------
// ----------------------  < Authentification functions > ---------------------- 
bool BTReadingID(){
  if(SerialBT.available()) {
    messageRead = SerialBT.read();
    Serial.println("Serial is available");
    if(messageRead == 65){
      Serial.println("first value is 65");
      while(SerialBT.available()) {
        Serial.println("Serial is still available");
        messageRead = SerialBT.read();
        Serial.println("idDigit: " + String(idDigit));
        uint8_t m = messageRead;
        Serial.println("messageRead: " + String(messageRead));
        //Serial.println("Authentification in process: messageRead = " + String(messageRead));
        if((idDigit < 8) && (messageRead > 47 && messageRead < 58)){
          idDigit++;
          userID = (userID * 10) + messageRead - 48;
          Serial.println("user id = " + String(userID));
          continue;
        }
        else{
          validIDFormat = false;
          idDigit++;
          continue;
        }
        delay(5);
      }
      if(validIDFormat && (idDigit == 8)){
        idDigit = 0;
        Serial.println("Success");
        return true;
      }
      else{
        userID = 0;
        idDigit = 0;
        validIDFormat = true;
        notifyUser(String("Authentification failed."));
        if(++authentificationAttempts > 3){
          notifyUser(String("Maximum 3 authentification attempts.\nDisconnecting..."));
          SerialBT.end();
          delay(1000);
          SerialBT.begin("Relaxation Pod");
        }
        return false;
      }
    }
    else{
      Serial.print("Not an authentification sequence: " + String(messageRead));
    }
    return false;
  }
}

bool credentialsVerifiedFunction(){
  if(getRequest()){
    return true;
  }
  return false;
}

bool getRequest(){
  //Serial.println("Inside get request function.");
  http.begin("https://uwvkpdnzfeekxwmqvejp.supabase.co/rest/v1/Students?id=eq." + String(userID)); 
  http.addHeader("apikey", API_KEY);
  http.addHeader("Authorization", String("Bearer ") + API_KEY);
  http.addHeader("Accept", "application/json");
  int httpResponseCode = http.GET();
  Serial.println("Response code: "+ String(httpResponseCode));
  if(httpResponseCode > 0){
    response = http.getString();
    http.end();
    if(response){
      response.remove(0,1);
      response.remove(response.length()-1,1);
      //Serial.println(response);
      //Serial.println("Trimmed string: " + response);
      //Serial.println("Get response: " + response);

      StaticJsonDocument<400> doc;
      DeserializationError err = deserializeJson(doc, response);

      if(!err){
        userName = doc["firstName"].as<String>();
        lightValue = doc["light"].as<uint8_t>();
        melody = doc["song"].as<uint8_t>();
        soundValue = doc["volume"].as<uint8_t>();
        //lastTimeS = doc["lastTime"].as<String>();
        doc.clear();
        response = "Welcome " + userName;
        
        notifyUser(response);
        response = "";
        return true;
      }
      else{
        Serial.print("Error on GET request: ");
        Serial.println(err.c_str());
        doc.clear();
        return false;
      }
    }
    else{
      response = "Access denied: Not in the active students list.";
      notifyUser(response);
      delay(5);
      response = "";
      return false;
    }
  }
  http.end();
  return false;
}

// ---------------------- </ Authentification functions > ---------------------- 
// ----------------------    ----------------------    ----------------------
// ---------------------- < Upload function > ---------------------- 
bool postRequest(){
  http.begin("https://uwvkpdnzfeekxwmqvejp.supabase.co/rest/v1/Students?id=eq." + String(userID));
  http.addHeader("apikey", API_KEY);
  http.addHeader("Authorization", String("Bearer ") + API_KEY);
  http.addHeader("Content-Type", "application/json");
  String payload = String("[{\"light\":") + String(lightValue) + String(",\"song\":") + String(melody) + String(",\"volume\":") + String(soundValue) + String("}]");
  int httpResponseCode = http.PATCH(payload);
  Serial.println(httpResponseCode);
  if(httpResponseCode > 0){
    Serial.println("Http code for post request: " + String(httpResponseCode) + "\nResponse of request: ");
    http.end();
    response = "";
    return true;
  }
  Serial.println("Error on POST request.");
  http.end();
  return false;
}
// ---------------------- </ Upload function > ---------------------- 
/*
unsigned long parseISO8601ToSeconds(String timestamp) {
  int year, month, day, hour, minute, second;

  // Parse the timestamp
  if (sscanf(timestamp.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute, &second) == 6) {
    // Convert the date to seconds since epoch
    return dateTimeToSeconds(year, month, day, hour, minute, second);
  }
  return 0; // Return 0 if parsing fails
}

unsigned long dateTimeToSeconds(int year, int month, int day, int hour, int minute, int second) {
  // Days in each month for non-leap years
  const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned long seconds = 0;

  // Add seconds for years
  for (int y = 1970; y < year; y++) {
    seconds += (isLeapYear(y) ? 366 : 365) * 86400;
  }
  // Add seconds for months
  for (int m = 1; m < month; m++) {
    seconds += (daysInMonth[m - 1] + (m == 2 && isLeapYear(year) ? 1 : 0)) * 86400;
  }
  // Add seconds for days, hours, minutes, and seconds
  seconds += (day - 1) * 86400;
  seconds += hour * 3600;
  seconds += minute * 60;
  seconds += second;

  return seconds;
}

// Function to check if a year is a leap year
bool isLeapYear(int year) {
  return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
*/

void buttonPressed(){
  doorOpened = true;
}

void notifyUser(String mess){
  Serial.println(mess);
  for(uint8_t i = 0; i < mess.length(); i++){
    char c = mess[i];
    SerialBT.write(c);
  }
  SerialBT.write('\n');
}

bool endOfSession(){
  if(doorOpened){
    doorOpened = false;
    return true;
  }
  if(startTime - millis() > 1800000){
    response = String("Your session has ended.\nThank you for visiting. See you next time.");
    return true;
  }
  return false;
}

void loop() {
  // transitions in RelaxationPod State Machine
  switch(RPod){
    case RPOD_Idle:{
      if(SerialBT.hasClient()){
        RPod = RPOD_Credentials;
        break;
      }
      break;
    }

    case RPOD_Credentials:{
      if(!SerialBT.hasClient()){
        RPod = RPOD_Idle;
      }
      if(credentialsVerified){
        RPod = RPOD_SysOn;
        digitalWrite(RED_LED, HIGH);
        digitalWrite(GREEN_LED, LOW);
        delay(1000);
        enableLightPreference();
        light();
        delay(1000);
        enableMusicPreferences();
      }
      break;
    }

    case RPOD_SysOn:{
      if(endOfSession()){
        RPod = RPOD_SysOff;
      }
      break;
    }
    
    case RPOD_SysOff:{
      if(systemOff){
        systemOff = false;
        RPod = RPOD_Upload;
      }
      break;
    }

    case RPOD_Upload:{
      if(dataUploaded){
        RPod = RPOD_Idle;
        credentialsVerified = false;
        dataUploaded = false;
        SerialBT.end();
        delay(1000);
        SerialBT.begin("Relaxation Pod");
        digitalWrite(RED_LED, LOW);
        digitalWrite(GREEN_LED, HIGH);
      }
      break;
    }
  }

  // state actions in RelaxationPod State Machine
  switch(RPod){
    case RPOD_Idle:{
      delay(100);
      break;
    }
    
    case RPOD_Credentials:{
      if(BTReadingID()){
        if(credentialsVerifiedFunction()){
          credentialsVerified = true;
        }
      }
    }
    
    case RPOD_SysOn :{
      light();
      BTReadingMP3();
    }
    
    case RPOD_SysOff:{
      if(isPlaying){
        execute_CMD(0x16,0,0);
        melody = songInList;
        isPlaying = false;
      }
      else{
        melody = 0;
      }
      
      if(lightValue < 35){
        lightValue = 35;
      }
      systemOff = true;
    }
    
    case RPOD_Upload:{
      if(postRequest()){
        dataUploaded = true;
      }
    }
  }

}
