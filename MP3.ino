#include "BluetoothSerial.h"
#include "SoftwareSerial.h"

# define Start_Byte 0x7E
# define Version_Byte 0xFF
# define Command_Length 0x06
# define End_Byte 0xEF
# define Acknowledge 0x00 //Returns info with command 0x41 [0x01: info, 0x00: no info]
# define ACTIVATED LOW

// Check if Bluetooth is available
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Check Serial Port Profile
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif


SoftwareSerial Mp3Serial(21, 22);
BluetoothSerial SerialBT;

static uint8_t messageRead = 0;
static uint8_t messageWrite = 0;

static uint8_t songInList = 1;
static uint8_t totalSongs = 7;
static bool isPlaying = false;
static bool isPaused = false;
static bool firstSong = true;
static bool validIDFormat = true;
static uint8_t authentificationAttempts = 0;
static uint32_t studentID = 0;
static uint8_t volume = 20;

static uint8_t idDigit = 0;

String device_name = "Relaxation Pod";

// ****************    Functions section    *****************//

void execute_CMD(byte CMD, byte Par1, byte Par2){  // Excecute the command and parameters
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
  delay(2000);
}

void notifyUser(String mess){
  Serial.println(mess);
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
          studentID = (studentID * 10) + messageRead - 48;
          Serial.println("student id = " + String(studentID));
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
        studentID = 0;
        idDigit = 0;
        validIDFormat = true;
        notifyUser(String("Authentification failed."));
        if(++authentificationAttempts > 3){
          notifyUser(String("Maximum 3 authentification attempts.\nDisconnecting..."));
          SerialBT.end();
          delay(1000);
          SerialBT.begin(device_name);
        }
        return false
      }
    }
    else{
      Serial.print("Not an authentification sequence: " + String(messageRead));
    }
    return false;
  }
}

// ****************    End Functions section    *****************//

void setup() {
  Serial.begin(115200);
  Mp3Serial.begin(9600);
  SerialBT.begin(device_name);  //Bluetooth device name
  //SerialBT.deleteAllBondedDevices(); // Uncomment this to delete paired devices; Must be called after begin
  Serial.printf("The device with name \"%s\" is started.\nNow you can pair it with Bluetooth!\n", device_name.c_str());
}

void loop() {
  if(SerialBT.connected()){
    delay(2);
    BTReadingMP3();
  }
}
