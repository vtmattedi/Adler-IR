#ifndef ESP32
#define ES8266 true
#endif

#include <../lib/Time-master/TimeLib.h>
#include <../lib/pubsubclient-2.8/src/PubSubClient.h>
#include <../include/Creds/WifiCred.h>
#include <../include/Creds/HiveMQCred.h>
#include <../lib/NightMare TCP/NightMareTCPServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#ifdef ES8266
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#endif

#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#endif

// Board defines.
#ifdef ESP8266
#define IR_SEND_PIN 4      // IR Emitter pin.
#define ONE_WIRE_BUS 0     // DS18b20 bus pin.
#define LED_PIN 1          // LED pin. (onboard led is pin 2 on ESP-01S boards).
#define LED_LOGIC_INVERTED // Comment if led is active on HIGH, Uncomment if led is active LOW.
#define IR_RECEIVE_PIN 13  // IR Reciever pin. Only used if USE_IR_RECIEVER is defined.
// #define LDR_PIN 34         // LDR pin (ANALOG).
#ifdef LDR_PIN
#warning "Intended to one specific use case. ESP-01 boards have no analog pin broken out and ESP8266 have just the one."
#endif
#endif
#ifdef ESP32
#define IR_SEND_PIN 19  // IR Emitter pin.
#define ONE_WIRE_BUS 16 // DS18b20 bus pin.
#define LED_PIN 2       // LED pin. (onboard led is pin 2 on ESP-01S boards).
// #define LED_LOGIC_INVERTED // Comment if led is active on HIGH, Uncomment if led is active LOW.
// #define IR_RECEIVE_PIN 19 // IR Reciever pin. Only used if USE_IR_RECIEVER is defined.
#define LDR_PIN 33 // LDR pin (ANALOG).
#endif

#define LDR_THRESHHOLD 3650
#define LDR_ADJUST 100

// #define USE_LDR // Uncomment to Read and post LDR value on pin LDR_PIN

#define DEVICE_NAME "Adler" // The device name for MNDS and MQTT.
#define NUM_OF_SENSORS 20   // Num of sensors held by the Sensors Struct.

// AC defines.
#define IDLE_TOLERANCE 0.3  // The change in temperature to which we consider it idle.
#define AC_MAX_TEMP 30      // MAX temperature of my AC unit.
#define AC_MIN_TEMP 18      // MIN temperature of my AC unit.
#define STALE_TIMESTAMP 300 // The time (in seconds) to consider a reading stale.

// Serial Defines.
#define COMPILE_SERIAL // comment to prevent Serial to be compiled.
                       // #define PRINT_FIRMWARE   // uncomment to print firmware info during setup useless if not compiling serial.
                       // #define WIFI_SCAN        // uncomment to print a wifi scan during setup useless if not compiling serial.
#define USE_OTA        // comment to prevent OTA to be compiled.

#define CONFIG_FILENAME "/configs.cfg" // the default name of the configuration file.
#define WebServerPort 80               // Web Server Port (default is 80).
#define TcpPort 100                    // Tcp Server Port
                                       // IR lib defines.

// #define USE_IR_RECIEVER //  Comment to not  use the IR reciver INPUT
#ifndef USE_IR_RECIEVER
#define DISABLE_CODE_FOR_RECEIVER // Disables the IR reciever code in the IR library. (not used here so may as well save some space).
#else
#ifdef COMPILE_SERIAL
// #undef COMPILE_SERIAL // Undefine Compile Serial in order to not have conflicts on ESP 01 boards.
#warning "Cannot have SERIAL and IR recv on ESP01 boards"
#endif

#endif
#define NO_LED_FEEDBACK_CODE // Disables the LED feedback code in the IR library.
#include <IRremote.hpp>      // Library specific asks to be included after IR_SEND_PIN, DISABLE_CODE_FOR_RECEIVER and NO_LED_FEEDBACK_CODE
                             // have been defined.
/// @brief Structure holding a temperature reading and a timestamp and fallback flag.
struct TempWithTime
{
  double temperature = 0;
  uint timestamp = 0;
  bool fallback = false;

  // Define the equality operator (==)
  bool operator==(const TempWithTime &other) const
  {
    return (temperature == other.temperature) && (timestamp == other.timestamp);
  }

  // Define the inequality operator (!=)
  bool operator!=(const TempWithTime &other) const
  {
    return !(*this == other);
  }

  // Define the less than operator (<)
  bool operator<(const TempWithTime &other) const
  {
    return temperature < other.temperature;
  }

  // Define the less than or equal to operator (<=)
  bool operator<=(const TempWithTime &other) const
  {
    return temperature <= other.temperature;
  }

  // Define the greater than operator (>)
  bool operator>(const TempWithTime &other) const
  {
    return temperature > other.temperature;
  }

  // Define the greater than or equal to operator (>=)
  bool operator>=(const TempWithTime &other) const
  {
    return temperature >= other.temperature;
  }
};

/// @brief Temperature State enum.
enum Temp_State
{
  UNKNOWNT = 0,
  WARMING = 1,
  COOLING = 2,
  IDLE = 3,
};

/// @brief AC State enum.
enum AC_STATE
{
  UNKNOWNSTATE = 0,
  AC_ON = 1,
  AC_OFF = 2,
};
// DS18 objects
OneWire oneWire(ONE_WIRE_BUS);          // Onewire object.
DallasTemperature tempSensor(&oneWire); // DallasTemperature object.
/// @brief the address of the ds18b20 onboard.
DeviceAddress sensorAddress;

/// @brief Sends an message to the Mqtt Server
/// @param topic The topic to the message
/// @param message The message
/// @param insertOwner Use the MqttTopic function to insert device's name before the topic.
/// @param retained Retained message or normal
void MqttSend(String topic, String message, bool insertOwner = true, bool retained = false);

StaticJsonDocument<512> DocJson;

/// @brief Struct holding a string and a bool to indicate whetaer or not the querry was successful.
struct NightMareMessage
{
  /// @brief The Result of the querry.
  bool result = false;
  /// @brief The Response of the querry.
  String response = "";
};
/// @brief Handles and income message.
/// @param message The Message to be processed.
/// @param delimiter The char to be used as an identifier between words.
/// @param extra_arg Any extra arguments.
/// @return A NightMareMessage with the response and wheather or not it was successfully processed;
NightMareMessage handleCommand(const String &);
#ifdef ESP8266
ESP8266WebServer _WebServer(WebServerPort); // Web Server object.
#endif
#ifdef ESP32
WebServer _WebServer(WebServerPort); // Web Server object.
#endif
NightMareTCPServer TcpServer(TcpPort); // Tcp server object.

// TCP-HiveMQ Definitions.
WiFiClientSecure hive_client;     // WiFi Client for MQTT.
PubSubClient HiveMQ(hive_client); // HiveMQ MQTT object.

/// @brief Sends an IR code, using the params of MY AC unit.
/// @param Code The code to be sent.
void sendIR(uint32_t);

/// @brief Turns on or off the LED respecting the configuration option.
/// @param state The State that you want the LED to be.
void SetLed(bool);

/// @brief The IR commands of MY AC unit.
enum Commands
{
  POWER = 0x10001,
  PLUS = 0x40004,
  MINUS = 0x20002,
  COUNT_DOWN = 0x400040,
  LED = 0x800080,
  MEIO = 0x200020,
  MODO = 0x100010,
  VENTILADOR = 0x80008,
  SLEEP1 = 0x10100,
  SLEEP2 = 0x20200,
  SLEEP3 = 0x40400
};

/// @brief Gets the IR command from the code value.
/// @param IrCode The code.
/// @return A String with the name of the code.
String getIrName(uint32_t IrCode)
{
  switch (IrCode)
  {
  case POWER:
    return "POWER";
  case PLUS:
    return "PLUS";
  case MINUS:
    return "MINUS";
  case COUNT_DOWN:
    return "COUNT_DOWN";
  case LED:
    return "LED";
  case MEIO:
    return "MEIO";
  case MODO:
    return "MODO";
  case VENTILADOR:
    return "VENTILADOR";
  case SLEEP1:
    return "SLEEP1";
  case SLEEP2:
    return "SLEEP2";
  case SLEEP3:
    return "SLEEP3";
  default:
    return "UNKNOWN";
  }
}

/// @brief Gets the code of the IR command from a String.
/// @param IrStr The name of the code.
/// @return A uint32_t with the code of the IR command associated to the name provided.
uint32_t getIrCode(const String &IrStr)
{
  if (IrStr == "POWER")
    return POWER;
  else if (IrStr == "PLUS")
    return PLUS;
  else if (IrStr == "MINUS")
    return MINUS;
  else if (IrStr == "COUNT_DOWN")
    return COUNT_DOWN;
  else if (IrStr == "LED")
    return LED;
  else if (IrStr == "MEIO")
    return MEIO;
  else if (IrStr == "MODO")
    return MODO;
  else if (IrStr == "VENTILADOR")
    return VENTILADOR;
  else if (IrStr == "SLEEP1")
    return SLEEP1;
  else if (IrStr == "SLEEP2")
    return SLEEP2;
  else if (IrStr == "SLEEP3")
    return SLEEP3;
  else
    return 0;
}

/// @brief Structure with name, value and timestamp of sensor (info from MQTT).
struct SensorData
{
  String name = "unused";
  double value = -1;
  uint timestamp = 0;
  /// @brief Updates the value of the sensor and the timestamp.
  /// @param newValue the new value of the sensor.
  void update(double newValue)
  {
    value = newValue;
    timestamp = now();
#ifdef COMPILE_SERIAL
    Serial.printf("[%s] updated [%2.2f]\n", name.c_str(), value);
#endif
  }

  /// @brief Generate a String representing the sensor data holding name value and timestamp values.
  /// @return A String that can be parsed somewhere else or by fromString().
  String toString()
  {
    String msg = "name: ";
    msg += name;
    msg += "\nvalue: ";
    msg += String(value, 1);
    msg += "\ntimestamp: ";
    msg += timestamp;
    return msg;
  }
  /// @brief restore a sensor from a String generated by toString().
  /// @param str the String with the sensor data.
  void fromString(const String &str)
  {
    int nameIndex = str.indexOf("name: ") + 6;
    int valueIndex = str.indexOf("value: ") + 7;
    int newlineIndex = str.indexOf('\n', valueIndex);
    name = str.substring(nameIndex, valueIndex - 8);
    value = str.substring(valueIndex, newlineIndex).toDouble();
    int timestampIndex = str.indexOf("timestamp: ") + 11;
    timestamp = strtoul(str.substring(timestampIndex).c_str(), NULL, 10);
  }
  /// @brief resets the Sensor object
  void reset()
  {
    name = "unused";
    value = -1;
    timestamp = 0;
  }
};

/// @brief Sensor struct to hold sensor a num of sensors defined in NUM_OF_SENSORS.
struct sensorsStruct
{
  /// @brief Array to hold the sensors data
  SensorData _sensors[NUM_OF_SENSORS];

  /// @brief Creates or gets the index in the sensor array.
  /// @param name The name to be found on the array or to be created.
  /// @return -1 if could not be created nor was found or the index of the created or found sensor.
  int CreateOrGetIndex(const String &name)
  {
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      if (_sensors[i].name == name)
        return i;
    }
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      if (_sensors[i].name == "unused")
      {
        _sensors[i].name = name;
#ifdef COMPILE_SERIAL
        Serial.printf("sensor '%s' created at '%d'\n", _sensors[i].name.c_str(), i);
#endif
        return i;
      }
    }
    return -1;
  }

  /// @brief Checks if there is a sensor with that name on the array.
  /// @param name The name of the sensor
  /// @return The index of the sensor or -1 if it does not exist.
  int Exists(const String &name)
  {
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      if (_sensors[i].name == name)
        return i;
    }
    return -1;
  }

  /// @brief Checks if there is a valid sensor in the index on the array.
  /// @param index The index of the array sensor.
  /// @return True if there is a sensor at index that is been used.
  bool Exists(const int index)
  {
    if (index < 0 || index >= NUM_OF_SENSORS)
      return false;
    if (_sensors[index].name.equals("unused"))
      return false;
    else
      return true;
  }

  /// @brief Deletes a sensor by reseting.
  /// @param name The name of the sensor to be reseted.
  /// @return False if name was not found true if it was and sensor was reset.
  bool DeleteByName(const String &name)
  {
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      if (_sensors[i].name == name)
      {
        DeleteAt(i);
        return true;
      }
    }
    return false;
  }

  /// @brief Deletes a sensor by resetingit.
  /// @param index The index of the sensor to be reseted.
  /// @return False if index is invalid true otherwise.
  bool DeleteAt(int index)
  {
    if (index < 0 || index >= NUM_OF_SENSORS)
      return false;
    _sensors[index].reset();
    return true;
  }

  /// @brief Resets all sensors in the array.
  void Reset()
  {
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      _sensors[i].reset();
    }
  }

  /// @brief Gets all the used sensors on the array.
  /// @return All the sensors toString() separeted by a ';' char.
  String ToString()
  {
    String message = "";
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      if (_sensors[i].name != "unused")
      {
        message += _sensors[i].toString();
        message += ";";
      }
    }
    return message;
  }
};

/// @brief The sensor object that holds the sensor array.
sensorsStruct Sensors;

struct Internals
{
  bool time_synced = false;
  uint boot_time = 0;
  bool ac_shutdown_scheduled = false;
  uint mqtt_update = 120;
  bool LittleFS_Mounted = false;
  uint last_mqtt_reconnect_attemp = 0;
  uint ac_shutdown_delay = -1;
  bool ac_shutdown_sent = false;
  uint validIRtime = 0;
  String sensorsAvailable = "Onboard+";
  bool ldrState = false;
  int last_LDR_val = 0;

  String toString()
  {
    String result = "time_synced: " + String(time_synced) + ";boot_time: " + String(boot_time) + ";life: " + String(now() - boot_time);
    return result;
  }

  bool handleSensorAvailable(const String &SensorTopic)
  {
    int sensorsIndex = SensorTopic.indexOf("/sensors/");
    // not a sensor.
    if (sensorsIndex < 0)
      return false;

    if (sensorsAvailable.indexOf(SensorTopic) >= 0)
    {
      // sensor already added
      return false;
    }

    // if (SensorTopic.indexOf("ds18") < 0 && SensorTopic.indexOf("Temp") < 0 && SensorTopic.indexOf("Temperature") < 0)
    // {
    //   // add sensor to our string
    //   sensorsAvailable += SensorTopic;
    //   sensorsAvailable += "+";
    //   return true;
    // }
    sensorsAvailable += SensorTopic;
    sensorsAvailable += "+";
    return true;
    // sensor is not temperature
    return false;
  }
};

Internals control_variables;

struct Configs
{
  /// @brief Name or topic of the main sensor to be used.
  String MainSensor = "Onboard";
  /// @brief Wheater or not to use LED.
  bool LedFeedback = true;
  /// @brief Temperature threshold to turn off the AC if configured.
  double TemperatureThreshold = 0;
  /// @brief When to turn off the AC.
  String ShutdownTime = "00:00";
  /// @brief Wheater to turn off the AC.
  bool AcShutdown = false;
  /// @brief Wheater to only turn off the AC if temperature is low enough.
  bool UseTemperatureThrashold = false;
  /// @brief Wheater to default back to borad sensor if we do not have new info from the sensor selected.
  bool DefaultBacktoBoard = false;
  /// @brief Reinforce the shutdown options 10 minutes later.
  bool DoubleEnforcement = false;
  /// @brief Temperature diference to be considered idle.
  float IdleTolerance = 0.3;
  /// @brief Wheater or not use the open door sensor.
  bool use_door_sensor = false;
  /// @brief The time between the door opening and the AC shutting down in seconds.
  int door_sensor_delay = 90;
  /// @brief Saves the configs to a String.
  /// @return a String that represents the configuration.
  String ToString()
  {
    String result;
    result += "MainSensor:" + MainSensor + ";";
    result += "LedFeedback:" + String(LedFeedback) + ";";
    result += "TemperatureThreshold:" + String(TemperatureThreshold) + ";";
    result += "ShutdownTime:" + ShutdownTime + ";";
    result += "AcShutdown:" + String(AcShutdown) + ";";
    result += "UseTemperatureThrashold:" + String(UseTemperatureThrashold) + ";";
    result += "DefaultBacktoBoard:" + String(DefaultBacktoBoard) + ";";
    result += "DoubleEnforcement:" + String(DoubleEnforcement) + ";";
    result += "IdleTolerance:" + String(IdleTolerance) + ";";
    result += "use_door_sensor:" + String(use_door_sensor) + ";";
    result += "door_sensor_delay:" + String(door_sensor_delay) + ";";
    MqttSend("/config", result);
    return result;
  }

  /// @brief Loads the values with a previously saved configuration String.
  /// @param data a String that represents the configuration.
  void FromString(const String &data)
  {
    uint8_t pos = 0;
    while (pos < data.length())
    {
      int colonIndex = data.indexOf(':', pos);
      if (colonIndex == -1)
        break;

      int semicolonIndex = data.indexOf(';', colonIndex);
      if (semicolonIndex == -1)
        semicolonIndex = data.length();

      String key = data.substring(pos, colonIndex);
      String value = data.substring(colonIndex + 1, semicolonIndex);

      if (key.equals("MainSensor"))
        MainSensor = value;
      else if (key.equals("LedFeedback"))
        LedFeedback = (value.toInt() != 0);
      else if (key.equals("TemperatureThreshold"))
        TemperatureThreshold = value.toDouble();
      else if (key.equals("ShutdownTime"))
        ShutdownTime = value;
      else if (key.equals("AcShutdown"))
        AcShutdown = (value.toInt() != 0);
      else if (key.equals("UseTemperatureThrashold"))
        UseTemperatureThrashold = (value.toInt() != 0);
      else if (key.equals("DefaultBacktoBoard"))
        DefaultBacktoBoard = (value.toInt() != 0);
      else if (key.equals("DoubleEnforcement"))
        DoubleEnforcement = (value.toInt() != 0);
      else if (key.equals("IdleTolerance"))
        IdleTolerance = (value.toFloat());
      else if (key.equals("use_door_sensor"))
        use_door_sensor = (value == "1");
      else if (key.equals("door_sensor_delay"))
        door_sensor_delay = value.toInt();

      pos = semicolonIndex + 1;
    }

    String s = ToString();
#ifdef COMPILE_SERIAL
    s.replace(";", "\n");
    Serial.printf("--------NEW CONFIG--------\n%s\n---------------------", s.c_str());
#endif

#ifdef LED_LOGIC_INVERTED
    if (!LedFeedback)
      digitalWrite(LED_PIN, !LOW);
#else
    if (!LedFeedback)
      digitalWrite(LED_PIN, LOW);
#endif
  }

  /// @brief Saves the configuration to a file using LittleFS.
  /// @param filename The configuration filename.
  /// @return True if LittleFs was mount and therefore file was saved and false if LittleFS was not mounted.
  bool SaveToFile(const char *filename = CONFIG_FILENAME)
  {
    if (!control_variables.LittleFS_Mounted)
      return false;

    if (LittleFS.exists(filename))
      LittleFS.remove(filename);
    File configfile = LittleFS.open(filename, "w");
    configfile.print(this->ToString());
    configfile.flush();
    configfile.close();
    return true;
  }

  /// @brief Loads the configuration from a file using LittleFS.
  /// @param filename The configuration filename.
  /// @return False if the is no file with that name or LittleFS was not mounted. True otherwise.
  bool LoadFromFile(const char *filename = CONFIG_FILENAME)
  {
    if (!control_variables.LittleFS_Mounted)
      return false;
    if (!LittleFS.exists(filename))
      return false;
    File configfile = LittleFS.open(filename, "r");
    FromString(configfile.readString());
    return true;
  }
  /// @brief Compares the time now versus the one saved on shutdowntime.
  /// @return True if AcShutdown is true and now is the time specified in shutdowntime. False is AcShutdown is false or time is wrong.
  bool CompareTime()
  {
    if (!AcShutdown)
      return false;
    if (hour() == atoi(ShutdownTime.substring(0, ShutdownTime.indexOf(":")).c_str()) && minute() == atoi(ShutdownTime.substring(ShutdownTime.indexOf(":") + 1).c_str()))
      return true;
    else
      return false;
    return false;
  }
};

/// @brief Object holding the settings beeing used.
Configs Configuration;

/// @brief Gets the MQTT topic in the proper format ie. with 'DEVICE_NAME/' before the topic.
/// @param topic The topic without the owner prefix.
/// @return The topic with the owner prefix.
String MqttTopic(String topic)
{
#ifndef DEVICE_NAME
#error Please define device name
#endif
  String topicWithOwner = "";
  topicWithOwner += DEVICE_NAME;
  if (topic != "" || topic != NULL)
  {
    if (topic[0] != '/')
      topicWithOwner += "/";
    topicWithOwner += topic;
  }
  return topicWithOwner;
}

/// @brief Sends an message to the Mqtt Server
/// @param topic The topic to the message
/// @param message The message
/// @param insertOwner Use the MqttTopic function to insert device's name before the topic.
/// @param retained Retained message or normal
void MqttSend(String topic, String message, bool insertOwner, bool retained)
{
  if (insertOwner)
    topic = MqttTopic(topic);
  HiveMQ.publish(topic.c_str(), message.c_str(), retained);
}

/// @brief Attempts to connect, reconnect to the MQTT broker .
/// @return True if successful or false if not.
bool MQTT_Reconnect()
{
  String clientID = DEVICE_NAME;
  clientID += String(random(), HEX);
  if (HiveMQ.connect(clientID.c_str(), MQTT_USER, MQTT_PASSWD))
  {
    bool sub = HiveMQ.subscribe("#/sensors/#");
    HiveMQ.subscribe(MqttTopic("/console/in").c_str());
#ifdef COMPILE_SERIAL
    Serial.printf("MQTT Connected subscribe was: %s\n", sub ? "success" : "failed");
#endif
    return true;
  }
  else
  {
#ifdef COMPILE_SERIAL
    Serial.printf("Can't Connect to MQTT Error Code : %d\n", HiveMQ.state());
#endif
    return false;
  }
}

#pragma region OTA

#ifdef USE_OTA
/// @brief Called on OTA starting.
void startOTA()
{
#ifdef COMPILE_SERIAL
  String type;
  // caso a atualização esteja sendo gravada na memória flash externa, então informa "flash"
  if (ArduinoOTA.getCommand() == U_FLASH)
    type = "flash";
  else                   // caso a atualização seja feita pela memória interna (file system), então informa "filesystem"
    type = "filesystem"; // U_SPIFFS
  // exibe mensagem junto ao tipo de gravação
  Serial.println("Start updating " + type);
#endif
}
/// @brief Called on OTA ending.
void endOTA()
{
#ifdef COMPILE_SERIAL
  Serial.println("\nEnd");
#endif
}
/// @brief Called during OTA update.
/// @param progress Bytes done.
/// @param total Total bytes.
void progressOTA(unsigned int progress, unsigned int total)
{
#ifdef COMPILE_SERIAL
  Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
#endif
}
/// @brief Called on error during OTA.
/// @param error The error code.
void errorOTA(ota_error_t error)
{
#ifdef COMPILE_SERIAL
  Serial.printf("Error[%u]: ", error);
  if (error == OTA_AUTH_ERROR)
    Serial.println("Auth Failed");
  else if (error == OTA_BEGIN_ERROR)
    Serial.println("Begin Failed");
  else if (error == OTA_CONNECT_ERROR)
    Serial.println("Connect Failed");
  else if (error == OTA_RECEIVE_ERROR)
    Serial.println("Receive Failed");
  else if (error == OTA_END_ERROR)
    Serial.println("End Failed");
#endif
}
#endif

#pragma endregion

/// @brief Struct to hold last time something was done.
struct Timers_Last_Values
{
  uint temperature = 0;
  uint sync_time = 0;
  uint mqtt_publish = 0;
  uint mqtt_reconnect = 0;
  uint Tasks = 0;
  uint Skynet = 0;
  uint LDR = 0;
  uint Last_Skynet_Report = 0;
};

/// @brief Object holding last time something was done.
Timers_Last_Values Timers;

/// @brief Structure to take control of my AC unit.
struct Assimilate
{
// hardcoded so far
#define DOOR_SHUTDOWN_MINUTES 30

  // Interval and last timestamp
  uint interval = 500;
  uint lastRun = 0;

  // control temperature
  bool controlingTemperature = false;
  double controlTemp = 24;
  uint controlStartTimestamp = 0;

  // analyze behaviour --> retake control in case of control loss
  // not yet implemented
  uint16_t onTimer = 0;
  uint16_t offTimer = 0;
  uint powerChangeTimeStamp = 0;

  // take Control
  bool takeControlRunning = false;
  uint takeControlTimestamp = 0;
  bool takeControlWasTimeSynced = false;
  bool takeControlWaitValidTemp = false;

  // AC state
  bool areWeInControl = false;           // Flag if we have control of the AC or not
  bool powerState = false;               // Flag AC on/off
  byte tempState = 18;                   // Temeprature of the AC
  bool countDownMode = false;            //'HW' shutdown button pressed
  byte countDown = 0;                    //'HW' shutdown hours
  uint hardware_sleep = 0;               //'HW' shutdown timestamp
  bool hardware_sleep_requested = false; //'HW' shutdown configured

  uint software_sleep = 0;               //'SW' shutdown timestamp
  bool software_sleep_requested = false; // 'SW' shutdown requested flag

  uint door_time = 0; // If door is triggered
  // TempWithTime objects
  TempWithTime _tempTenMinAgo;
  TempWithTime _tempFiveMinAgo;

  /// @brief Maintains the AC controller. Should be fed at frequently to work properly.
  /// @param twt The current state of the temperature, timestamp, fallback.
  void run(TempWithTime twt)
  {
    if (millis() - lastRun > interval)
    {
      lastRun = millis();
      if (takeControlRunning)
      {
        TakeControl(twt);
        return;
      }

      // AnalyzeBehaviour();
      if (now() - _tempTenMinAgo.timestamp > 10 * 60)
      {
        _tempTenMinAgo = twt;
      }
      if (now() - _tempFiveMinAgo.timestamp > 5 * 60)
      {
        _tempFiveMinAgo = twt;
      }
      if (hardware_sleep - now() >= 0 && hardware_sleep_requested)
      {
        powerState = !powerState;
        hardware_sleep_requested = false;
      }
      if (door_time > 0 && Configuration.use_door_sensor)
      {
        if (now() - door_time > Configuration.door_sensor_delay)
        {
          setPower(false);
        }

        if (now() - door_time > DOOR_SHUTDOWN_MINUTES * 60)
        {
          setTaget(0);
          door_time = 0;
        }
        return;
      }
      handleTarget(twt.temperature);
    }
  }

  /// @brief Run the IR commands sent throught the Skynet so we know the AC state and what to expected.
  /// @param whatWasSent The code that was sent.
  void ExpectBehaviour(uint32_t whatWasSent)

  {
    if (!areWeInControl)
      return;
    if (countDownMode)
    {
      if (whatWasSent == POWER)
      {
        powerState = !powerState;
        countDownMode = false;
        countDown = 0;
      }
      else if (whatWasSent == PLUS)
        countDown++;
      else if (whatWasSent == MINUS)
        countDown--;
      else if (whatWasSent == COUNT_DOWN)
      {
        countDownMode = false;
        hardware_sleep_requested = true;
        hardware_sleep = now() + 60 * 60 * countDown;
      }

      if (countDown == 255)
        countDown = 12;
      else if (countDown == 13)
        countDown = 0;
      return;
    }
    else
    {
      if (whatWasSent == POWER)
      {
        powerState = !powerState;
        if (hardware_sleep_requested)
          hardware_sleep_requested = false;
      }
      else if (whatWasSent == PLUS)
      {
        if (powerState)
          tempState++;
      }
      else if (whatWasSent == MINUS)
      {
        if (powerState)
          tempState--;
      }
      else if (whatWasSent == COUNT_DOWN)
      {
        // it disables the ac
        if (hardware_sleep_requested)
          hardware_sleep_requested = false;
        else
        {
          countDownMode = true;
          countDown = 0;
        }
      }

      if (tempState > AC_MAX_TEMP)
        tempState = AC_MAX_TEMP;
      else if (tempState < AC_MIN_TEMP)
        tempState = AC_MIN_TEMP;
    }
    // Serial.printf("[%d] - %s --> %d\n", count, getCommandName(whatWasSent).c_str(), tempState);
  }

  /// @brief Not yet implemented. It will analyze the room temperature to see if we still have the control of the AC.
  void AnalyzeBehaviour()
  {
    return;
  }

  /// @brief Gets info from the state of the AC [Deprecated use Report()].
  /// @return A String with the state info from the AC.
  String ACstate()
  {
    String msg = "";
    if (!areWeInControl)
    {
      if (takeControlRunning)
      {
        msg += "{Skynet is taking control in ";
        msg += 10 * 60 - (now() - takeControlTimestamp);
        msg += " seconds}";
        return msg;
      }
      else
        return "{Skynet has been defeated [re-take control is not implemented yet] }";
    }
    else
    {
      msg += "{temp=";
      msg += String(tempState);
      msg += "+power=";
      msg += powerState;
      msg += "+sleep=";
      msg += hardware_sleep_requested ? String(hardware_sleep - now()) : "-1";
      msg += "}";
      return msg;
    }
  }

  /// @brief Sets the the AC temperature controller target.
  /// @param target The desired room temperature.
  void setTaget(double target)
  {
    if (!areWeInControl)
    {
#ifdef COMPILE_SERIAL
      Serial.printf("We do not have control. can't control temperature.\n");
#endif
      return;
    }
    if (target < 18 || target > 30)
    {
      controlingTemperature = false;
      MqttSend("/Control", "Control off");
      publishReport();
      return;
    }
    else
    {
      String msg = "Control start, target  =  ";
      msg += target;
      controlStartTimestamp = now();
      controlTemp = target;
      MqttSend("/Control", msg);
      controlingTemperature = true;
    }
    if (setPower(true))
      delay(150);
    SetTemperature(target > 18 ? target - 1 : target);
  }

  /// @brief Gets info from the AC temperature controller.
  /// @return A String with the AC temperature controller information.
  String targetInfo()
  {
    if (!areWeInControl)
      return "ac state unkown";
    if (!controlingTemperature)
      return powerState ? "AC on" : "AC off";
    String msg = "Set to ";
    msg.concat(controlTemp);
    return msg;
  }

  /// @brief Handles the AC control to maintain the desired temperature.
  /// @param currentTemperature The current temperature read.
  void handleTarget(double currentTemperature)
  {
    if (!controlingTemperature || !areWeInControl)
      return;
    else if (currentTemperature > (float)controlTemp + Configuration.IdleTolerance)
    {
      setPower(true);
      if (tempState - controlTemp > 1)
      {
        SetTemperature(controlTemp > 18 ? controlTemp - 1 : 18);
      }
    }
    else if (currentTemperature < (float)controlTemp - Configuration.IdleTolerance)
    {

      setPower(false);
    }
  }

  /// @brief Starts the assessment of the AC state.
  void StartControl()
  {
    takeControlTimestamp = now();
    takeControlRunning = true;
    takeControlWaitValidTemp = true;
#ifdef COMPILE_SERIAL
    Serial.println("Skynet Starting control... stop using the ac remote now.");
#endif
    sendIR(POWER);
    return;
  }

  /// @brief Sets the power state of the AC if the state is known.
  /// @param newPowerState The state you want the AC to be in.
  bool setPower(bool newPowerState)
  {
    if (!areWeInControl)
      return false;
    if (powerState != newPowerState)
    {
      sendIR(POWER);
      return true;
    }
    return false;
  }
  /// @brief Handles the assessment of the AC state.
  /// @param twt The TempWithTime with the last temperature read.
  void TakeControl(TempWithTime twt)
  {
    // Not valid temperature.
    if (twt.temperature < 1 && takeControlWaitValidTemp)
    {
#ifdef COMPILE_SERIAL
      Serial.println("...waiting for valid temperature...");
#endif
      return;
    }
    // Change in time input.
    if (control_variables.time_synced != takeControlWasTimeSynced)
    {
      takeControlWasTimeSynced = control_variables.time_synced;
      takeControlTimestamp = now();
      _tempTenMinAgo = twt;
#ifdef COMPILE_SERIAL
      Serial.println("...reseting the assimilation process, the timer was synced, in 10 minutes we will have the helm...");
#endif
    }
    // Change in temperature input.
    if (_tempTenMinAgo.fallback != twt.fallback)
    {
      takeControlWasTimeSynced = control_variables.time_synced;
      takeControlTimestamp = now();
      _tempTenMinAgo = twt;
#ifdef COMPILE_SERIAL
      Serial.println("...reseting the assimilation process, the Temperature sensor changed, in 10 minutes we will have the helm...");
#endif
    }
    // Start the assessment of the AC state
    if (takeControlWaitValidTemp)
    {
      takeControlWasTimeSynced = control_variables.time_synced;
      takeControlTimestamp = now();
      takeControlWaitValidTemp = false;
      _tempTenMinAgo = twt;
#ifdef COMPILE_SERIAL
      Serial.println("...starting, in 10 minutes we will have the helm...");
#endif
    }

    // Ends the assessment of the AC state
    if (now() - takeControlTimestamp >= 10 * 60)
    {
      // bad result, same object twice 10 minutes apart, wait for another 10.
      if (_tempTenMinAgo == twt)
      {
        takeControlTimestamp = now();
        return;
      }
      // Temp fell in 10 minutes, AC must be on.
      if (_tempTenMinAgo > twt)
        powerState = true;
      else // Temp remained unchanged or rose, ac must be off or the was external interference. only way to rule it out is multiple data points
        powerState = false;

      // Set the temperature to 24.
      bool powerissoff = !powerState;
      if (powerissoff)
      {
        sendIR(POWER);
        delay(200);
      }
      SetTemperature(24, true);
      if (powerissoff)
      {
        sendIR(POWER);
        delay(200);
      }
      tempState = 24;
      areWeInControl = true;
#ifdef COMPILE_SERIAL
      Serial.println("...control aquired...");
#endif
      takeControlRunning = false;
    }
  }

  /// @brief Sets AC shutdown.
  /// @param hours_till_shutdown Hours to be set.
  void Hardware_AC_Sleep(byte hours_till_shutdown)
  {
    setTaget(0);
    if (hours_till_shutdown > 12 || hours_till_shutdown == 0)
      return;
    // Turn off current schedule
    if (countDownMode)
    {
      sendIR(COUNT_DOWN);
      delay(200);
    }

    // Start shedule on AC
    sendIR(COUNT_DOWN);
    delay(200);

    for (size_t i = 0; i < hours_till_shutdown; i++)
    {
      sendIR(PLUS);
      delay(200);
    }
    // Save and Exit.
    sendIR(COUNT_DOWN);
  }

  /// @brief Sets the temperature of the AC, also turns it on if it is not (if not on forced mode).
  /// @param Target The target temperature.
  /// @param force Force the temperature to be that one targeted, ignore the known temperature.
  void SetTemperature(byte Target, bool force = false)
  {
    // Serial.printf("trying to set Temp c:%d t: %d f: %d control: %d ==> ",tempState,Target,force,areWeInControl);
    if (areWeInControl && !force)
    {
#ifdef COMPILE_SERIAL
      if (Target < AC_MIN_TEMP)
        Serial.println("Warning Trying to set temperature lower than AC is capable");
      if (Target > AC_MAX_TEMP)
        Serial.println("Warning Trying to set temperature Higher than AC is capable");
      if (Target == tempState)
        Serial.println("Exiting earlier since we are already at Target");
#endif
      if (Target == tempState || Target < AC_MIN_TEMP || Target > AC_MAX_TEMP)
        return;
      bool targetIsGreater = Target > tempState;
      int steps = Target - tempState;
      if (!targetIsGreater)
        steps = -1 * steps;
      // String s = "";
      // s += Target;
      // s += " ";
      // s += tempState;
      // s += " ";
      // s += targetIsGreater;
      // s += " ";
      // s += steps;
      // s += " ";
      // s += powerState;
      // MqttSend("/debug/state_settemp", s);
      // Serial.printf("t > c?:%d steps: %d\n",targetIsGreater,steps);

      if (setPower(true))
      {
        delay(150);
      }

      for (int i = 0; i < steps; i++)
      {
        if (targetIsGreater)
          sendIR(PLUS);
        else
          sendIR(MINUS);
        delay(150);
      }
    }
    if (force)
    {
      for (size_t i = 0; i < 12; i++)
      {
        sendIR(PLUS);
        delay(150);
        yield();
      }
      for (uint8_t i = 0; i < 30 - Target; i++)
      {
        sendIR(MINUS);
        delay(150);
        yield();
      }
    }
    publishReport();
  }

  /// @brief Manualy syncs the state of the AC.
  /// @param manualState Physical State of the AC.
  /// @param manualTemperature Physical Temperature of the AC.
  bool ManualSync(bool manualState, byte manualTemperature)
  {
    if (manualTemperature < AC_MIN_TEMP || manualTemperature > AC_MAX_TEMP)
    {
#ifdef COMPILE_SERIAL
      Serial.println("Invalid temperature, Manual Sync not completed.");
#endif
      return false;
    }
    if (takeControlRunning)
    {
      takeControlRunning = false;
    }
    tempState = manualTemperature;
    powerState = manualState;
    areWeInControl = true;
    return true;
  }

  /// @brief Handles input from door sensor
  /// @param newValue when was door sensor triggered
  void HandleDoorSensor(uint32_t newValue)
  {
    if (newValue != 0)
    {
      door_time = now();
    }
    else
      door_time = newValue;
  }
  /// @brief Generates a report with the info of the AC controller and HW/SW sleep info.
  /// @return The String with the report.
  String Report()
  {
    DocJson.clear();
    DocJson["Control"] = areWeInControl ? String(-1) : String(10 * 60 - (now() - takeControlTimestamp));
    DocJson["Temp"] = powerState ? tempState : -tempState;
    DocJson["Ssleep"] = software_sleep_requested ? software_sleep : 0;
    DocJson["Hsleep"] = hardware_sleep_requested ? hardware_sleep : 0;
    DocJson["Settemp"] = controlingTemperature ? controlTemp : -controlTemp;
    DocJson["Door"] = door_time;

    // Serialize the JSON object to a string
    String msg;
    serializeJson(DocJson, msg);

    return msg;
  }

  String toString() const
  {
    String result;

    result += "Interval: " + String(interval) + "\n";
    result += "Last Run: " + String(lastRun) + "\n";
    result += "Controlling Temperature: " + String(controlingTemperature) + "\n";
    result += "Control Temp: " + String(controlTemp) + "\n";
    result += "Control Start Timestamp: " + String(controlStartTimestamp) + "\n";
    result += "On Timer: " + String(onTimer) + "\n";
    result += "Off Timer: " + String(offTimer) + "\n";
    result += "Power Change Timestamp: " + String(powerChangeTimeStamp) + "\n";
    result += "Take Control Running: " + String(takeControlRunning) + "\n";
    result += "Take Control Timestamp: " + String(takeControlTimestamp) + "\n";
    result += "Take Control Was Time Synced: " + String(takeControlWasTimeSynced) + "\n";
    result += "Take Control Wait Valid Temp: " + String(takeControlWaitValidTemp) + "\n";
    result += "Are We In Control: " + String(areWeInControl) + "\n";
    result += "Power State: " + String(powerState) + "\n";
    result += "Temp State: " + String(tempState) + "\n";
    result += "Count Down Mode: " + String(countDownMode) + "\n";
    result += "Count Down: " + String(countDown) + "\n";
    result += "Hardware Sleep: " + String(hardware_sleep) + "\n";
    result += "Hardware Sleep Requested: " + String(hardware_sleep_requested) + "\n";
    result += "Software Sleep: " + String(software_sleep) + "\n";
    result += "Software Sleep Requested: " + String(software_sleep_requested) + "\n";
    result += "_tempTenMinAgo: {" + String(_tempTenMinAgo.temperature) + ", " + String(_tempTenMinAgo.timestamp) + "}\n";
    result += "_tempFiveMinAgo: {" + String(_tempFiveMinAgo.temperature) + ", " + String(_tempFiveMinAgo.timestamp) + "}\n";

    return result;
  }

  void publishReport()
  {
    Timers.Last_Skynet_Report = now();
    MqttSend("/state", Report());
  }
};

/// @brief Object doing the AC control.
Assimilate Skynet;

/// @brief Gets the name of the Temp_State enum.
/// @param state The Temp_State object.
/// @return The name based on the Temp_State object.
String getTempStateString(Temp_State state)
{
  switch (state)
  {
  case UNKNOWNT:
    return "Unknown";
  case WARMING:
    return "Warming";
  case COOLING:
    return "Cooling";
  case IDLE:
    return "Idle";
  default:
    return "Invalid State";
  }
}

/// @brief Struct to hold Temperature Data
struct Temperature
{
  double history[20];
  uint timestamps[20];
  double fallbackHistory[20];
  uint fallbackTimestamps[20];
  byte next_index = 0;
  byte fallbackIndex = 0;
  Temp_State curr_state = Temp_State::UNKNOWNT;
  Temp_State old_state = Temp_State::UNKNOWNT;
  AC_STATE ac_state = AC_STATE::UNKNOWNSTATE;
  bool control_temperature = false;
  byte target_temperature = 24;
  float rate_of_change = 0; // dT/dt degC/sec
  uint targetStart = 0;
  bool firstStateUnknown = 0;
  bool usingFallback = false;

  /// @brief Adds a temperature to the history array.
  /// @param temp the temperature read.
  /// @return the index of the next position on the array.
  byte add(double temp)
  {
    history[next_index] = temp;
    timestamps[next_index] = now();
    next_index++;
    if (next_index > 19)
    {
      next_index = 0;
    }
    TempWithTime twt;
    twt.temperature = temp;
    twt.timestamp = now();
    Skynet.run(twt);
    rate_of_change = (float)(history[getLastIndex()] - history[getLastIndex(-1)]);
    return next_index;
  }

  /// @brief Adds a temperature to the fallback array.
  /// @param temp the temperature read.
  /// @return the index of the next position on the fallback array.
  byte addFallback(float temp)
  {
    fallbackHistory[fallbackIndex] = temp;
    fallbackTimestamps[fallbackIndex] = now();
    fallbackIndex++;
    if (fallbackIndex > 19)
    {
      fallbackIndex = 0;
    }
    return fallbackIndex;
  }

  /// @brief Gets the last index used i.e. the last temperature added.
  /// @param offset offset on the last index used.
  /// @param _fallbackIndex True for the fallback index and false to the normal index.
  /// @return The index of last temperature added.
  byte getLastIndex(int offset = 0, bool _fallbackIndex = false)
  {
    if (offset > 20)
      offset = 20;
    if (offset < -20)
      offset = -20;

    int index_plus_offset = offset;
    if (_fallbackIndex)
      index_plus_offset += fallbackIndex;
    else
      index_plus_offset += next_index;
    // Serial.printf("index + offset = %d, next_index =  %d, offset = %d\n", index_plus_offset, (int)next_index, offset);

    if (index_plus_offset > 20)
      return index_plus_offset - 20 - 1;
    else if (index_plus_offset < 0)
      return index_plus_offset + 20 - 1;
    else if (index_plus_offset == 0)
      return 19;
    else
      return index_plus_offset - 1;
  }

  /// @brief Resets the arrays and internal states.
  void reset()
  {
    for (size_t i = 0; i < 20; i++)
    {
      history[i] = 0;
      timestamps[i] = 0;
      fallbackHistory[i] = 0;
      fallbackTimestamps[i] = 0;
    }
    next_index = 0;
    ac_state = AC_STATE::UNKNOWNSTATE;
    curr_state = Temp_State::UNKNOWNT;
    old_state = Temp_State::UNKNOWNT;
  }

  /// @brief Gets the average of the array items.
  /// @param _size The size of the array to be analyzed, i.e. the last _size reads.
  /// @param include_now Include the last temperature read or start with one before that.
  /// @param _fallback True to get average from the fallback, false to get from the normal array.
  /// @return -1 if size > 20, the average of the items stored in the array.
  float getAvg(byte _size = 20, bool include_now = true, bool _fallback = false)
  {
    if (_size > 20 || _size == 0)
      return -1;

    byte last_index = getLastIndex();
    if (!include_now)
      last_index = getLastIndex(-1);
    float avg = 0;
    for (size_t i = 0; i < _size; i++)
    {
      if (last_index < i)
      {
        if (_fallback)
          avg += fallbackHistory[20 - (i - last_index)];
        else
          avg += history[20 - (i - last_index)];
      }
      else
      {
        if (_fallback)
          avg += fallbackHistory[last_index - i];
        else
          avg += history[last_index - i];
      }
    }

    avg = avg / _size;
    return avg;
  }

  /// @brief  Gets the last temperature added and set the usingFallback to true if the last added temperature have staled.
  /// @return The last temperature from the cu
  double currentTemperature()
  {
    int _index = getLastIndex();
    if (now() - timestamps[_index] <= STALE_TIMESTAMP)
    {
      usingFallback = false;
      return history[_index];
    }
    if (Configuration.DefaultBacktoBoard)
    {
      _index = getLastIndex(0, true);
      if (now() - fallbackTimestamps[_index] <= STALE_TIMESTAMP)
      {
        usingFallback = true;
        return fallbackHistory[_index];
      }
    }
    usingFallback = false;
    return -1;
  }

  /// @brief Gets a TempWithTime object with the last temperature, timestamp and fallback flag.
  /// @return A TempWithTime object with the last temperature, timestamp and fallback flag.
  TempWithTime currentTemperatureWithTime()
  {
    TempWithTime twt;
    int _index = getLastIndex();
    if (now() - timestamps[_index] <= STALE_TIMESTAMP)
    {
      twt.temperature = history[_index];
      twt.timestamp = timestamps[_index];
      twt.fallback = false;
    }
    else if (Configuration.DefaultBacktoBoard)
    {
      _index = getLastIndex(0, true);
      if (now() - fallbackTimestamps[_index] <= STALE_TIMESTAMP)
      {
        twt.temperature = fallbackHistory[_index];
        twt.timestamp = fallbackTimestamps[_index];
        twt.fallback = true;
      }
    }
    return twt;
  }

  /// @brief Generates a report of the object. [OLD]
  /// @return A String with the report.
  String report()
  {
    String s = "Held Values: ";
    String help = "";
    if (next_index == 0)
      next_index = 19;
    else
      next_index--;
    for (size_t i = 0; i < 20; i++)
    {
      s += "[";
      help += " ";
      if (next_index < i)
      {
        size_t curr = 20 - (i - next_index);
        s += history[curr];
        help += " ";
        if (curr < 10)
          help += " ";
        help += curr;
        if (history[curr] > 10)
          help += " ";
        help += " ";
      }
      else
      {
        size_t curr = next_index - i;
        s += history[curr];
        help += " ";
        if (curr < 10)
          help += " ";
        help += curr;
        if (history[curr] > 10)
          help += " ";
        help += " ";
      }
      help += " ";
      s += "]";
    }
    if (next_index == 19)
      next_index = 0;
    else
      next_index++;
    s += "\n INDEX:      ";
    s += help;
    s += "\n Index:";
    s += next_index;
    s += "\n Last Index:";
    s += getLastIndex();
    s += "\n AVG: ";
    s += getAvg();
    s += "\n AVG [5]:";
    s += getAvg(5);
    return s;
  }
};

/// @brief Obeject handling the temperature data holding.
Temperature tempHandler;

#pragma region WebServer

/// @brief Gets the MIME type of a file based on its name.
/// @param filename the name of the file.
/// @return The MIME type.
String getMIME(String filename)
{
  if (filename.endsWith(".htm"))
    return "text/html";
  else if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "text/javascript";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".gif"))
    return "image/gif";
  else if (filename.endsWith(".jpg"))
    return "image/jpeg";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".xml"))
    return "text/xml";
  else if (filename.endsWith(".pdf"))
    return "application/x-pdf";
  else if (filename.endsWith(".zip"))
    return "application/x-zip";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  else if (filename.endsWith(".csv"))
    return "text/csv";
  else if (filename.endsWith(".log"))
    return "text/log";
  return "text/plain";
}

// #ifdef ESP8266
/// @brief Handles the IR sending from the client.
void handleIRSend()
{
  if (_WebServer.hasArg("code"))
  {
    uint32_t code = 0;
    String argValue = _WebServer.arg("code");
    code = strtoul(argValue.c_str(), NULL, HEX);
    if (code == 0)
    {
      code = getIrCode(argValue);
    }
    sendIR(code);
    _WebServer.send(200, "text/plain", "Argument received: " + argValue);
  }
#ifdef COMPILE_SERIAL
  Serial.printf("%d\n", _WebServer.args());
#endif
}

/// @brief Handles a file requested by the client, returns the file if exists.
void handleFile()
{
  String filename = "/web";
  if (_WebServer.uri() == "/")
    filename += "/page.html";
  else
    filename += _WebServer.uri();
#ifdef COMPILE_SERIAL
    // Serial.println(filename);
#endif
  if (LittleFS.exists(filename))
  {
    File file = LittleFS.open(filename, "r");
    yield();
    _WebServer.streamFile(file, getMIME(filename));
  }
  else
    _WebServer.send(404, "text/plain", "FILE not FOUND.");
}

/// @brief Handle general querries from the client.
void handleGeneral()
{
  //[Deprecated] show data on the temp handler
  if (_WebServer.hasArg("temp-vect"))
  {
    _WebServer.send(200, "text/plain", handleCommand("TEMPCONTROLLER").response);
    return;
  }
  // Set time between each temp. publish to mqtt broker.
  if (_WebServer.hasArg("set-pub-time"))
  {
    String response = "nop";
    int arg_as_int = atoi(_WebServer.arg("set-pub-time").c_str());
    if (arg_as_int > 0)
    {
      response = "new pub time: ";
      response += arg_as_int;
      response += " secs.";
      control_variables.mqtt_update = arg_as_int;
    }
    _WebServer.send(200, "text/plain", response);
    return;
  }
  if (_WebServer.hasArg("set-temp-update-time"))
  {
    // implement
    return;
  }
  // Handle MQTT reconnect request
  if (_WebServer.hasArg("mqtt-force-recon"))
  {
    String response = "result: ";
    if (now() - control_variables.last_mqtt_reconnect_attemp < 10)
    {
      response += " Wait more: ";
      response += 10 - (now() - control_variables.last_mqtt_reconnect_attemp);
      response += " secs before trying again";
      _WebServer.send(200, "text/plain", response);
      return;
    }
    bool result = MQTT_Reconnect();
    if (result)
    {
#ifdef COMPILE_SERIAL
      Serial.println("MQTT Connected");
#endif
      response += "succeess";
    }
    else
    {
#ifdef COMPILE_SERIAL
      Serial.printf("Can't Connect to MQTT Error Code : %d\n", HiveMQ.state());
#endif
      response += "failed";
    }
    control_variables.last_mqtt_reconnect_attemp = now();
    _WebServer.send(200, "text/plain", response);
    return;
  }
  if (_WebServer.hasArg("help"))
  {
    _WebServer.send(200, "text/plain", handleCommand("Help").response);
    return;
  }
  // Handles new control temperature.
  if (_WebServer.hasArg("set-temperature"))
  {
    String _command = "Target ";
    _command += atoi(_WebServer.arg("set-temperature").c_str());
    _WebServer.send(200, "text/plain", handleCommand(_command).response);
    return;
  }
  // Gets all sensors identified.
  if (_WebServer.hasArg("others"))
  {
    String response = Sensors.ToString();
    _WebServer.send(200, "text/plain", response);
    return;
  }
  // Handle shutdown requests.
  if (_WebServer.hasArg("acshutdown"))
  {
    String _command = "Shutdown ";
    _command += atoi(_WebServer.arg("acshutdown").c_str());
    if (!_WebServer.hasArg("software"))
      _command += " hw";

    _WebServer.send(200, "text/plain", handleCommand(_command).response);
  }
  if (_WebServer.hasArg("manual-sync"))
  {
    String _command = "Manualsync ";
    int arg_as_int = atoi(_WebServer.arg("manual-sync").c_str());
    bool pwrstate = true;
    if (arg_as_int < 0)
    {
      pwrstate = false;
      arg_as_int = arg_as_int * -1;
    }
    _command += pwrstate;
    _command += " ";
    _command += arg_as_int;
    _WebServer.send(200, "text/plain", handleCommand(_command).response);
    return;
  }
  if (_WebServer.hasArg("config"))
  {
    String _command = "Manualsync ";
    int arg_as_int = atoi(_WebServer.arg("manual-sync").c_str());
    bool pwrstate = true;
    if (arg_as_int < 0)
    {
      pwrstate = false;
      arg_as_int = arg_as_int * -1;
    }
    _command += pwrstate;
    _command += " ";
    _command += arg_as_int;
    _WebServer.send(200, "text/plain", handleCommand(_command).response);
    return;
  }
}

/// @brief Handle configs querries by the client.
void handleConfig()
{
  if (_WebServer.hasArg("new-config"))
  {
    String s = "";
    for (int i = 0; i < _WebServer.args(); i++)
    {
      // Serial.printf("%d -> %s: %s\n", i, server.argName(i).c_str(), server.arg(i).c_str());
      s += _WebServer.argName(i);
      s += ":";
      s += _WebServer.arg(i);
      s += ";";
    }
    Configuration.FromString(s);
    Configuration.SaveToFile();
    _WebServer.send(200, "text/plain", "Settings Saved");
    return;
  }
  else if (_WebServer.hasArg("sensorsList"))
  {
    _WebServer.send(200, "text/plain", control_variables.sensorsAvailable);
    return;
  }
  else if (_WebServer.hasArg("current-config"))
  {
    _WebServer.send(200, "text/plain", Configuration.ToString());
    return;
  }
  else if (_WebServer.hasArg("restart"))
  {
    _WebServer.send(200, "text/plain", "ESP restarting...");
    ESP.restart();
    return;
  }
  else if (_WebServer.hasArg("factory-reset"))
  {
    Configuration.AcShutdown = false;
    Configuration.DefaultBacktoBoard = true;
    Configuration.LedFeedback = true;
    Configuration.UseTemperatureThrashold = false;
    Configuration.MainSensor = "Onboard";
    Configuration.TemperatureThreshold = 24;
    Configuration.ShutdownTime = "04:00";
    Configuration.DoubleEnforcement = true;
    Configuration.SaveToFile();
    _WebServer.send(200, "text/plain", "Settings restored!");
    return;
  }
}

/// @brief Handles info querries from client.
void handleInfo()
{
  String response = "temp:";
  response += String(tempHandler.currentTemperature()).substring(0, 4);
  response += ";mqtt:";
  response += HiveMQ.state();
  response += ";";
  response += control_variables.toString();
  response += ";";
  response += "mainSensor: ";
  if (tempHandler.usingFallback)
    response += "Onboard (fallback)";
  else
    response += Configuration.MainSensor;
  response += ";";
  response += "Skynet:";
  response += Skynet.Report();
  response += ";";
  response += "setStatus: ";
  response += Skynet.targetInfo();
  response += ";";
  response += "rate-of-change: ";
  response += tempHandler.rate_of_change;
  response += ";";
  _WebServer.send(200, "text/plain", response);
}

/// @brief Adds all files in the directory to the web server response.
/// @param dirName The directory of LittleFS to be added to the web server responder.
void setServerFiles(char *dirName)
{
  if (!control_variables.LittleFS_Mounted)
    return;
#ifdef ESP8266
  Dir dir = LittleFS.openDir(dirName);
  while (dir.next())
  {
    String filename = "/";
    if (dir.fileName() != "page.html")
      filename += dir.fileName();
    _WebServer.on(filename, handleFile);
#ifdef COMPILE_SERIAL
    Serial.printf("Adding %s to the WebServer\n", filename.c_str());
#endif
  }
#endif
#ifdef ESP32
  fs::File dir = LittleFS.open(dirName);
  fs::File file = dir.openNextFile();
  while (file)
  {
    String filename = "/";
    if (strcmp(file.name(), "page.html") != 0)
      filename += file.name();
    _WebServer.on(filename, handleFile);
#ifdef COMPILE_SERIAL
    Serial.printf("Adding %s to the WebServer\n", filename.c_str());
#endif
    file = dir.openNextFile();
  }
#endif
}

#ifdef COMPILE_SERIAL
/// @brief Handle print debug querry from client, most for debug purposes.
void handlePrint()
{
  if (_WebServer.hasArg("file"))
  {
    Serial.println(LittleFS.open(CONFIG_FILENAME, "r").readString());
    _WebServer.send(200, "text/plain", "ook - file");
    return;
  }
  Serial.printf("\n");
  String s = Configuration.ToString();
  s.replace(";", "\n");
  Serial.print(s);
  _WebServer.send(200, "text/plain", "ook");
  return;
}
#endif

// #endif

#ifdef ESP32

// void HandleAsycRequest(AsyncWebServerRequest *request)
// {
//   // Implement all
// }

// class WebHandler : public AsyncWebHandler
// {
// public:
//   WebHandler() {}
//   virtual ~WebHandler() {}

//   bool canHandle(AsyncWebServerRequest *request)
//   {
//     // request->addInterestingHeader("ANY");
//     return true;
//   }

//   void handleRequest(AsyncWebServerRequest *request)
//   {
//     HandleAsycRequest(request);
//   }
// };

#endif

#pragma endregion

/// @brief Gets the name of the MQTT state based on its value.
/// @param value The value of MQTT.state().
/// @return A String with the MQTT state name.
String getDefineName(int value)
{
  switch (value)
  {
  case -4:
    return "MQTT_CONNECTION_TIMEOUT";
  case -3:
    return "MQTT_CONNECTION_LOST";
  case -2:
    return "MQTT_CONNECT_FAILED";
  case -1:
    return "MQTT_DISCONNECTED";
  case 0:
    return "MQTT_CONNECTED";
  case 1:
    return "MQTT_CONNECT_BAD_PROTOCOL";
  case 2:
    return "MQTT_CONNECT_BAD_CLIENT_ID";
  case 3:
    return "MQTT_CONNECT_UNAVAILABLE";
  case 4:
    return "MQTT_CONNECT_BAD_CREDENTIALS";
  case 5:
    return "MQTT_CONNECT_UNAUTHORIZED";
  default:
    return "Unknown";
  }
}

/// @brief Gets if the topic have been publish as a sensor a.k.a. have '/sensors/' on the topic.
/// @param topic The topic name.
/// @return True if topic has '/sensors/', false otherwise.
bool isMqttSensor(String topic)
{
#ifdef COMPILE_SERIAL
  Serial.printf("Is topic '%s' a sensor?? - ", topic.c_str());
#endif
  if (topic.indexOf(String(DEVICE_NAME)) >= 0)
  {
#ifdef COMPILE_SERIAL
    Serial.println("own device");
#endif
    return false;
  }
  if (topic.indexOf("/sensors/") >= 0)
  {
#ifdef COMPILE_SERIAL
    Serial.println(" IT is!");
#endif
    return true;
  }
#ifdef COMPILE_SERIAL
  Serial.println(" none of the above");
#endif
  return false;
}

/// @brief Callback to a message from MQTT server.
/// @param topic The topic of the MQTT message.
/// @param payload The payload of the MQTT message.
/// @param length The length of the payload.
void HiveMQ_Callback(char *topic, byte *payload, unsigned int length)
{
  String incommingMessage = "";
  for (uint i = 0; i < length; i++)
    incommingMessage += (char)payload[i];

#ifdef COMPILE_SERIAL
  Serial.printf("MQTT::[%s]-->[%s]\n", topic, incommingMessage.c_str());
#endif

  // Only topic Shell will have valid commands and expect a response
  if (strcmp(topic, MqttTopic("/console/in").c_str()) == 0)
  {
    NightMareMessage res = handleCommand(incommingMessage);
    MqttSend("/console/out", res.result ? res.response : "Command not recognized, try 'help'.");
  }

  if (strcmp(topic, "All/control") == 0)
  {
    if (incommingMessage.equals("get_state"))
    {
      Skynet.publishReport();
    }
  }

  if (strcmp(topic, MqttTopic("/Light").c_str()) == 0)
  {
    incommingMessage == "1" ? digitalWrite(2, !HIGH) : digitalWrite(2, !LOW);
    incommingMessage == "HIGH" ? digitalWrite(2, !HIGH) : digitalWrite(2, !LOW);
    incommingMessage == "on" ? digitalWrite(2, !HIGH) : digitalWrite(2, !LOW);
  }
  else if (strcmp(topic, MqttTopic("/Hshutdown").c_str()) == 0)
  {
    Skynet.Hardware_AC_Sleep(atoi(incommingMessage.c_str()));
    MqttSend("/Response", "ack");
  }
  else if (strcmp(topic, MqttTopic("/Sshutdown").c_str()) == 0)
  {
    Skynet.software_sleep = now() + atoi(incommingMessage.c_str()) * 60 * 60;
    Skynet.software_sleep_requested = true;
    MqttSend("/Response", "ack");
  }
  else if (strcmp(topic, MqttTopic("/Send-IR").c_str()) == 0)
  {
    sendIR(strtoul(incommingMessage.c_str(), NULL, HEX));
  }

  else if (Configuration.MainSensor.equals(topic))
  {
    tempHandler.add(incommingMessage.toFloat());
  }

  if (isMqttSensor(topic))
  {
    int sensorIndex = Sensors.CreateOrGetIndex(topic);
    // Serial.printf("%s: %d\n", incommingMessage.c_str(), sensorIndex);
    if (sensorIndex >= 0)
    {
      Sensors._sensors[sensorIndex].update(incommingMessage.toDouble());
    }
  }
  control_variables.handleSensorAvailable(topic);
}

/// @brief Handles messages recieved by the tcp server.
/// @param message the message recieved already as String.
/// @param index the index of the client in the array {TcpServer.clients[index]}.
/// @return the response to the client.
String HandleTcpMsg(String message, int index)
{
  return handleCommand(message).response;
}

/// @brief Attempts to get the time synced using worldtimeapi.
/// @return True if successful or false otherwise.
bool getTime()
{
  bool result = false;
#ifdef COMPILE_SERIAL
  Serial.println("Syncing Time Online");
#endif
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://worldtimeapi.org/api/timezone/America/Bahia.txt"); // HTTP
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0)
  {
    // HTTP header has been send and Server response header has been handled
    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
#ifdef COMPILE_SERIAL
      Serial.printf("[HTTP] OK... code: %d\n", httpCode);
#endif
      String payload = http.getString();
      char str[payload.length() + 1];
      strcpy(str, payload.c_str());
      char *pch;
      pch = strtok(str, ":\n");
      int i = 0;
      int raw_offset = 0;
      while (pch != NULL)
      {
        i++;
        if (i == 23)
        {
          raw_offset = atoi(pch);
        }
        if (i == 27)
        {
          setTime(atoi(pch) + raw_offset);
        }
        // printf("%d: %s\n", i, pch);
        pch = strtok(NULL, ":\n");
      }
#ifdef COMPILE_SERIAL
      String msg = "Time Synced ";
      msg += millis();
      msg += "ms from boot.";
      Serial.println(msg);
#endif
      result = true;
    }
    else
    {
#ifdef COMPILE_SERIAL
      Serial.printf("[HTTP] Error code: %d\n", httpCode);
#endif
    }
  }
  else
  {
#ifdef COMPILE_SERIAL
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
#endif
  }
  http.end();
  return result;
}

// Coments above.
void sendIR(uint32_t Code)
{
  SetLed(HIGH);
#ifdef COMPILE_SERIAL
  Serial.printf("Sending {0x%X} [%s]\n", Code, getIrName(Code).c_str());
#endif
  IrSender.sendPulseDistanceWidth(38, 9000, 4550, 600, 1700, 600, 550, Code, 24, PROTOCOL_IS_LSB_FIRST, 0, 0);
  Skynet.ExpectBehaviour(Code);
  String payload = "Sent: ";
  payload += getIrName(Code);
  MqttSend("/IR", payload);
  SetLed(LOW);
  yield();
  delay(1);
  control_variables.validIRtime = millis() + 50;
}

// Coments above.
void SetLed(bool state)
{
  if (!Configuration.LedFeedback)
    return;
#ifdef LED_LOGIC_INVERTED
  state = !state;
#endif
  digitalWrite(LED_PIN, state);
}

#ifdef COMPILE_SERIAL
/// @brief Gets the WiFi status as a const char*.
/// @param status The WiFi status.
/// @return A const char* with the WiFi status.
const char *getWiFiStatusString(wl_status_t status)
{
  switch (status)
  {
  case WL_NO_SHIELD:
    return "No WiFi shield";
  case WL_IDLE_STATUS:
    return "Idle";
  case WL_NO_SSID_AVAIL:
    return "No WiFi networks available";
  case WL_SCAN_COMPLETED:
    return "Scan completed";
  case WL_CONNECTED:
    return "Connected to WiFi";
  case WL_CONNECT_FAILED:
    return "Failed to connect to WiFi";
  case WL_CONNECTION_LOST:
    return "Connection lost";
#ifdef ESP8266
  case WL_WRONG_PASSWORD:
    return "WiFi password is incorrect";
#endif
  case WL_DISCONNECTED:
    return "Disconnected";
  default:
    return "Unknown WiFi status";
  }
}
#endif

enum TimeStampFormat
{
  DateAndTime,
  OnlyDate,
  OnlyTime
};

/// @brief Converts a timestamp to a date and time String.
/// @param timestamp The unix timestamp to be parsed.
/// @param _format Whether you want Date, Time or Both.
/// @return a String with the date on the timestamp formatted as HH:mm DD/MM/YY, attending to the TimeStampFormat passed.
String timestampToDateString(uint32_t timestamp, TimeStampFormat _format = DateAndTime)
{
  // Convert the timestamp to a time_t object.
  time_t timeObject = timestamp;

  // Extract the date components from the time_t object.
  int _year = year(timeObject) % 100;
  int _month = month(timeObject);
  int _day = day(timeObject);

  int _hour = hour(timeObject);
  int _minute = minute(timeObject);

  String dateString = "";
  // Adds Time
  if (_format != OnlyDate)
  {
    dateString = String(_hour, DEC).length() == 1 ? "0" + String(_hour, DEC) : String(_hour, DEC);
    dateString += ":";
    dateString += String(_minute, DEC).length() == 1 ? "0" + String(_minute, DEC) : String(_minute, DEC);
  }
  // Adds a Space between Time and Date
  if (_format == DateAndTime)
    dateString += " ";
  // Adds Date
  if (_format != OnlyTime)
  {
    dateString += String(_day, DEC).length() == 1 ? "0" + String(_day, DEC) : String(_day, DEC);
    dateString += "/";
    dateString += String(_month, DEC).length() == 1 ? "0" + String(_month, DEC) : String(_month, DEC);
    dateString += "/";
    dateString += String(_year, DEC);
  }
  return dateString;
}

/// @brief Handles and income message.
/// @param message The Message to be processed.
/// @param delimiter The char to be used as an identifier between words.
/// @param extra_arg Any extra arguments.
/// @return A NightMareMessage with the response and wheather or not it was successfully processed;
NightMareMessage handleCommand(const String &message)
{
  char *delimiter = " ";
  // String extra_arg = String("")
  NightMareMessage result;
  String response = "";
  bool resolve = true;
  String command = "";
  String args[5] = {"", "", "", "", ""};

  String current_string = "";

  int index = 0;
  int last_index = 0;
  // gets command and args using the delimiters
  for (size_t i = 0; index >= 0; i++)
  {
    current_string = "";
    index = message.indexOf(delimiter, last_index + 1);

    if (last_index > 0)
      last_index += strlen(delimiter);

    if (index == -1)
      current_string = message.substring(last_index);
    else if (index < message.length())
      current_string = message.substring(last_index, index);

    last_index = index;

    if (i == 0)
    {
      current_string.toUpperCase();
      command = current_string;
    }
    else if (i < 6)
      args[i - 1] = current_string;
  }

  // process the command
  command.toUpperCase();
  if (command == "HELP" || command == "H")
  {
    // Do documentation
    response += ("Welcome to NightMare Home Systems ©\n");
#ifdef ESP32
    response += "This is a ESP32 Module ";
#endif
#ifdef ESP8266
    response += "This is a ESP8266 Module ";
#endif
    response += " running Adler IR.\n";
    response += "Available communication protocols: HTTP web server, TCP, MQTT.\n";
    response += "Available commands, commands are capitalized in code but parameters are not.\n";
    response += "[] = required parameter, {} = optional parameter.\n";
    response += "--------------------------------------------------------------------.\n";
    response += "SENDIR [code]             -->   Sends the code ([HEX] or name) through the ir led.\n";
    response += "SEND   [code]             -->   Sends the code ([HEX] or name) through the ir led.\n";
    response += "SENSORS                   -->   Returns all current sensors detected.\n";
    response += "HWINFO                    -->   Describes board hardware.\n";
    response += "HARDWARE_INFO             -->   Describes board hardware.\n";
    response += "MANUALSYNC [State] [Temp]       --> Syncs skynet with the state and temp.\n";
    response += "SHUTDOWN [Time] {Hw}            --> Schedule a shutdown in the Time specified, set {Hw} to 1 to use the AC controller.\n";
    response += "SKYNET                    --> Gets the report from SkyNet.\n";
    response += "TEMPCONTROLLER            --> Gets the report from the temperature controller.\n";
    response += "TARGET {Temp}             --> Sets the temperature control to {temp}°C, empty,<18,>30 or nan, turns off the controller.\n";
    response += "--------------------------------------------------------------------.\n";
  }
  else if (command == "HARDWARE_INFO" || command == "HWINFO")
  {
    response += "IR Emitter @ pin: ";
    response += IR_SEND_PIN;
#ifdef IR_RECEIVE_PIN
    response += "\nIR Reciever @ pin: ";
    response += IR_RECEIVE_PIN;
#endif
    response += "\nDS18b20 @ pin: ";
    response += ONE_WIRE_BUS;
#ifdef LDR_PIN and USE_LDR
    response += "\nLDR @ pin: ";
    response += LDR_PIN;
#endif
#ifdef LED_PIN
    response += "\nFeedback Led @ pin: ";
    response += LED_PIN;
#endif
  }
  else if (command == "SENDIR" || command == "SEND")
  {
    if (args[0] == "")
    {
      resolve = false;
      response = "Nothing to be sent.";
    }
    else
    {
      uint32_t _code = strtoul(args[0].c_str(), NULL, HEX);
      if (_code != 0)
      {
        sendIR(_code);
        response = "Sent: ";
        response += _code;
        response += ".";
      }
      else if (getIrCode(args[0]) > 0)
      {
        sendIR(getIrCode(args[0]));
        response = "Sent: ";
        response += args[0];
        response += ".";
      }
      else
      {
        response = "'";
        response += args[0];
        response += "' was not identified.";
      }
    }
  }
  else if (command == "SENSORS")
  {
    for (size_t i = 0; i < NUM_OF_SENSORS; i++)
    {
      if (Sensors.Exists(i))
      {
        response += "[";
        response += i;
        response += "] Name: ";
        response += Sensors._sensors[i].name;
        response += " Value: ";
        response += Sensors._sensors[i].value;
        response += " Age: ";
        response += now() - Sensors._sensors[i].timestamp;
        response += " sec.\n";
      }
    }
  }
  else if (command == "MANUALSYNC")
  {
    resolve = Skynet.ManualSync(args[0] == "1" ? true : false, args[1].toInt());
    response += "Trying to Sync Skynet with State: ";
    response += args[0];
    response += " Temp: ";
    response += args[1];
    response += ". Result: ";
    response += resolve ? "success." : "failed.";
  }
  else if (command == "SHUTDOWN")
  {
    int _time = args[0].toInt();

    if (_time <= 0)
    {
      resolve = false;
      result.result = resolve;
      return result;
    }
    if (args[1] == "1")
      Skynet.Hardware_AC_Sleep(_time);
    else
    {
      Skynet.software_sleep = now() + _time * 60 * 60;
      Skynet.software_sleep_requested = true;
    }
    response += "Shutdown Scheduled for ";
    response += _time;
    response += " h";
    response += args[1] == "1" ? " {AC Hw used}." : ".";
  }
  else if (command == "SKYNET")
  {
    response += Skynet.Report();
    Serial.println(Skynet.toString());
  }
  else if (command == "TEMPCONTROLLER")
  {
    response += tempHandler.report();
  }
  else if (command == "TARGET")
  {
    Skynet.setTaget(args[0].toDouble());
    response += "Target is now: ";
    response += Skynet.controlingTemperature ? String(Skynet.controlTemp) : "off";
    response += ".";
  }
  else if (command == "WIFI")
  {
    response += WiFi.RSSI();
  }
  else if (command == "LDR")
  {
    int ldr = 0;
    for (size_t i = 0; i < 10; i++)
    {
      ldr += analogRead(LDR_PIN);
    }
    ldr /= 10;
    response += ldr;
  }
  else if (command == "CONFIGURATION")
  {
    response += Configuration.ToString();
  }
  else if (command == "SETTEMP")
  {
    Skynet.setPower(args[0].equals("1"));
    response += "Power is now: " + String(Skynet.powerState);
    +".";
  }
  else if (command == "INFO")
  {

    response = "temp:";
    response += String(tempHandler.currentTemperature()).substring(0, 4);
    response += ";mqtt:";
    response += HiveMQ.state();
    response += ";";
    response += control_variables.toString();
    response += ";";
    response += "mainSensor: ";
    if (tempHandler.usingFallback)
      response += "Onboard (fallback)";
    else
      response += Configuration.MainSensor;
    response += ";";
    response += "Skynet:";
    response += Skynet.Report();
    response += ";";
    response += "setStatus: ";
    response += Skynet.targetInfo();
    response += ";";
    response += "rate-of-change: ";
    response += tempHandler.rate_of_change;
    response += ";";
  }
  else if (command == "BOOT")
  {
    if (control_variables.time_synced)
      response += "[Synced] ";
    else
      response += "[Not Synced!] ";

    response += " Boot Time was: ";
    response += control_variables.boot_time;
    response += ". (" + String(timestampToDateString(control_variables.boot_time)) + "). ";
    response += now() - control_variables.boot_time;
    response += " secs. System-Millis(): ";
    response += millis();
    response += " ms.";
  }
  else if (command == "DOOROPEN")
  {
    Skynet.HandleDoorSensor(strtoul(args[0].c_str(), NULL, 10));
    Skynet.run(tempHandler.currentTemperatureWithTime());
  }
  else if (command == "DOORSENSOR")
  {
    Configuration.use_door_sensor = (args[0] == "1");
    bool res = Configuration.SaveToFile();
    response = " [";
    response += res;
    response += "] - door sensor is now: ";
    response += Configuration.use_door_sensor;
  }
  else if (command == "NEW-CONFIG")
  {
    Configuration.FromString(args[0]);
    bool res = Configuration.SaveToFile();
    response += "Config recv = {";
    response += args[0];
    response += "} result: ";
    response += res ? "True" : "False";
    response += ".";
  }

  else
    resolve = false;

  result.response = response;
  result.result = resolve;

#ifdef COMPILE_SERIAL
  Serial.printf("message = %s | command = %s | args[0] = %s | args[1] = %s | args[2] = %s |\n", message.c_str(), command.c_str(), args[0].c_str(), args[1].c_str(), args[2].c_str());
#endif
  return result;
}

void setup()
{
  // pinModes.
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);
  pinMode(IR_SEND_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
#ifdef USE_IR_RECIEVER
  pinMode(IR_RECEIVE_PIN, INPUT);
  IrReceiver.begin(IR_RECEIVE_PIN, false);
#endif

#ifdef LDR_PIN
  pinMode(LDR_PIN, INPUT);
  int _initialState = analogRead(LDR_PIN);
  if (_initialState > LDR_THRESHHOLD)
    control_variables.ldrState = true;
  control_variables.last_LDR_val = _initialState;
#endif
  // Resets objects.
  tempHandler.reset();
  Sensors.Reset();
  // DS18 Start.
  tempSensor.begin();
  tempSensor.getAddress(sensorAddress, 0);
  tempSensor.setResolution(sensorAddress, 11);
  // IR Start.
  IrSender.begin();

#ifdef COMPILE_SERIAL
// Start Serial.
#ifdef ESP8266
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
#endif
#ifdef ESP32
  Serial.begin(115200);
#endif
  // Prints DS18 info.
  int sensors_found = tempSensor.getDeviceCount();
  Serial.printf("DS18 SENSORS: %d\n", sensors_found);
  for (uint8_t i = 0; i < sensors_found; i++)
  {
    Serial.printf("[%d] 0x", i);
    DeviceAddress addr;
    tempSensor.getAddress(addr, i);
    for (uint8_t j = 0; j < 8; j++)
    {
      if (addr[j] < 0x10)
        Serial.print("0");
      Serial.print(addr[j], HEX);
    }
    Serial.println("");
  }
  Serial.println();

#ifdef WiFiScan
  // Performs a wifiscan and prints out before try to connect
  WiFi.mode(WIFI_STA); // Important to be explicitly connected as client
  WiFi.begin();

  int networksFound = WiFi.scanNetworks();

  if (networksFound == 0)
  {
    Serial.println("No WiFi networks found.");
  }
  else
  {
    Serial.print("WiFi networks found: ");
    Serial.println(networksFound);

    for (int i = 0; i < networksFound; ++i)
    {
      Serial.print("Network name: ");
      Serial.println(WiFi.SSID(i));
      Serial.print("Signal strength: ");
      Serial.println(WiFi.RSSI(i));
      Serial.println("--------------------");
    }
  }
#endif
  Serial.print("Getting Internet Acess");
#endif

  // Wifi Setup.
  WiFi.mode(WIFI_STA); // Important to be explicitly connected as client
#ifdef DEVICE_NAME
  WiFi.hostname(DEVICE_NAME);
#endif
  WiFi.begin(WIFISSID, WIFIPASSWD);

  bool led = false;
  uint8_t counter = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1);
    if (millis() % 100 == 0)
    {
      counter++;
#ifdef COMPILE_SERIAL
      Serial.print(".");
#endif
      SetLed(led);
      led = !led;
    }

#ifdef COMPILE_SERIAL

    if (counter == 10)
    {
      Serial.print(getWiFiStatusString(WiFi.status()));
      Serial.println();
      counter = 0;
    }
#endif

    yield();
    if (millis() > 150000)
    {
#ifdef COMPILE_SERIAL
      uint timing = millis();
      Serial.print("Reseting at: ");
      Serial.print(timing);
      Serial.print("ms / ");
      Serial.print(timing / 1000);
      Serial.print("sec / ");
      Serial.print(timing / 60000);
      Serial.println("min");
#endif
      ESP.restart();
    }
  }
  SetLed(LOW);
#ifdef COMPILE_SERIAL
  Serial.println();
  Serial.print("Connection took (ms) : ");
  Serial.println(millis());
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
#endif

  // MNDS config.
  if (!MDNS.begin(DEVICE_NAME))
  {
#ifdef COMPILE_SERIAL
    Serial.println("Error setting up MDNS responder!");
#endif
  }
  else
  {
#ifdef COMPILE_SERIAL
    Serial.println("mDNS responder started");
#endif
  }

#ifdef USE_OTA
  // OTA config.
  ArduinoOTA.setHostname(DEVICE_NAME);

#ifdef ESP8266
  ArduinoOTA.setPort(8266);
#endif
#ifdef ESP32
  ArduinoOTA.setPort(3232);
#endif

  ArduinoOTA.onStart(startOTA);
  ArduinoOTA.onEnd(endOTA);
  ArduinoOTA.onProgress(progressOTA);
  ArduinoOTA.onError(errorOTA);
  ArduinoOTA.begin();
#endif
  // Hive MQTT Config.
  hive_client.setInsecure();
  HiveMQ.setServer(MQTT_URL, MQTT_PORT);
  HiveMQ.setCallback(HiveMQ_Callback);

  control_variables.time_synced = getTime();
  control_variables.boot_time = now();
  // Start LittleFS.
  control_variables.LittleFS_Mounted = LittleFS.begin(true);
  // Load configuration.
  bool loadResult = Configuration.LoadFromFile();

#ifdef COMPILE_SERIAL
  Serial.printf("Configuration was: %s\n", loadResult ? " loaded. " : " NOT LOADED!.");
#ifdef PRINT_FIRMWARE
  Serial.println("\n#####################");
  Serial.println("__________________________");
  Serial.println("Firmware:");

  Serial.printf(" Chip Id: %08X\n", ESP.getChipId());
  Serial.print(" Core version: ");
  Serial.println(ESP.getCoreVersion());
  Serial.print(" SDK version: ");
  Serial.println(ESP.getSdkVersion());
  Serial.print(" Boot version: ");
  Serial.println(ESP.getBootVersion());
  Serial.print(" Boot mode: ");
  Serial.println(ESP.getBootMode());

  Serial.println("__________________________");
  Serial.println("Flash chip information:");
  Serial.printf(" Flash chip Id: %08X\n", ESP.getFlashChipId());
  Serial.printf(" Sketch thinks Flash RAM is size: %.2f MB\n", ESP.getFlashChipSize() / 1024.0 / 1024.0);
  Serial.printf(" Actual size based on chip Id: %.2f MB\n", ESP.getFlashChipRealSize() / 1024.0 / 1024.0);
  Serial.printf(" Flash frequency: %.2f MHz\n", ESP.getFlashChipSpeed() / 1000.0 / 1000.0);
  Serial.printf(" Flash write mode: %s\n", (ESP.getFlashChipMode() == FM_QIO ? "QIO" : ESP.getFlashChipMode() == FM_QOUT ? "QOUT"
                                                                                   : ESP.getFlashChipMode() == FM_DIO    ? "DIO"
                                                                                   : ESP.getFlashChipMode() == FM_DOUT   ? "DOUT"
                                                                                                                         : "UNKNOWN"));

  Serial.println("__________________________");
  Serial.println("File system (LittleFS):");

  Serial.println("__________________________");
  Serial.printf("CPU frequency: %u MHz\n", ESP.getCpuFreqMHz());
  Serial.println("#####################");

  Serial.println("====== Reading from LittleFS file =======");
  // read and print file content line by line
  FSInfo fs_info;
  LittleFS.info(fs_info);

  float totalSizeMB = (float)fs_info.totalBytes / 1024.0 / 1024.0;
  float usedSizeMB = (float)fs_info.usedBytes / 1024.0 / 1024.0;

  Serial.println("File system (LittleFS):");
  Serial.printf(" Total size: %.2f MB\n", totalSizeMB);
  Serial.printf(" Used size: %.2f MB\n", usedSizeMB);
#ifdef ESP8266
  Dir dir = LittleFS.openDir("/");
  while (dir.next())
  {
    Serial.print(" - ");
    Serial.println(dir.fileName());
  }
#endif
#ifdef ESP32
  fs::File dir = LittleFS.open("/");
  fs::File file = dir.openNextFile();
  while (file)
  {
    Serial.print(" - ");
    Serial.println(file.name());
  }
#endif
#endif
#endif

  // Web Server config.

  setServerFiles("/web/");
  _WebServer.on("/settings", handleConfig);
  _WebServer.on("/send-ir", handleIRSend);
  _WebServer.on("/general", handleGeneral);
  _WebServer.on("/info", handleInfo);
#ifdef COMPILE_SERIAL
//  _WebServer.on("/print", handlePrint);
#endif

  _WebServer.begin();
#ifdef COMPILE_SERIAL
  Serial.println("HTTP server started");
#endif
  TcpServer.setMessageHandler(HandleTcpMsg);
  TcpServer.begin();
#ifdef COMPILE_SERIAL
  Serial.println("Tcp server started");
#endif
  // Start AC control.
  Skynet.StartControl();
  SetLed(LOW);
}

void loop()
{
#ifdef ESP8266
  MDNS.update();
#endif
#ifdef ESP32
#ifdef COMPILE_SERIAL
  // Handles Serial Messaging
  if (Serial.available() > 0)
  {
    String _input = "";
    while (Serial.available() > 0)
    {
      char c = Serial.read();
      // Control bytes from PIO implementation of serial monitor.
      if (c != 10 && c != 13)
      {
        _input += c;
      }
    }
    Serial.println("");
    NightMareMessage m = handleCommand(_input);
    Serial.printf("'%s' ---->>> [%s]\n%s\n", _input.c_str(), m.result ? "Pass" : "FAILED", m.response.c_str());
  }
#endif
#endif
  _WebServer.handleClient();
#ifdef USE_OTA
  ArduinoOTA.handle();
#endif
  HiveMQ.loop();
  TcpServer.handleServer();
#ifdef LDR_PIN
  if (millis() - Timers.LDR > 200)
  {
    Timers.LDR = millis();
#define NUM_OF_ANALOG_READS 3
    int _ldrVal = 0;
    for (size_t i = 0; i < NUM_OF_ANALOG_READS; i++)
    {
      _ldrVal += analogRead(LDR_PIN);
    }
    _ldrVal = _ldrVal / NUM_OF_ANALOG_READS;
    int adptableThreshhold = LDR_THRESHHOLD;
    adptableThreshhold += control_variables.ldrState ? -1 * LDR_ADJUST : LDR_ADJUST;
    control_variables.last_LDR_val = _ldrVal;
    bool _newState = _ldrVal > adptableThreshhold;
    // String s = "";
    // s += _ldrVal;
    // s += " ";
    // s += _newState;
    // s += " ";
    // s += control_variables.ldrState;
    // s += " ";
    // s += _ldrVal > adptableThreshhold;
    // s += " ";
    // s += adptableThreshhold;
    // MqttSend("debug/ldr", s);
    if (_newState != control_variables.ldrState)
    {
      // TcpServer.broadcast("L" + String(_newState));
      // control_variables.ldrState = _newState;
      // MqttSend("Sherlock/Lights", _newState ? "1" : "0", false);
      MqttSend("/sensors/ldr", String(_ldrVal));
#ifdef COMPILE_SERIAL
      Serial.println(_newState ? "LightDetected" : "LightOff");
#endif
    }
  }
#endif
#ifdef USE_IR_RECIEVER
  if (IrReceiver.decode())
  {
    if (millis() > control_variables.validIRtime)
    {
      // Grab an IR code
      Skynet.ExpectBehaviour(IrReceiver.decodedIRData.decodedRawData); // Handles Ir commands sent from physiscal remote.
#ifdef COMPILE_SERIAL
      Serial.printf("Recv: %s [%d]\n", getIrName(IrReceiver.decodedIRData.decodedRawData).c_str(), millis());
#endif
      control_variables.validIRtime = millis() + 200;
    }
    IrReceiver.resume(); // Prepare for the next value
  }
#endif
  // Gets the onboard temperature.
  if (now() - Timers.temperature > 2)
  {
    Timers.temperature = now();
    // tempSensor.requestTemperatures();
    // Serial.println(tempSensor.getTempCByIndex(0));
    // Serial.println(analogRead(LDR_PIN));
    if (Configuration.MainSensor.equals("Onboard"))
    {
      tempSensor.requestTemperatures();
      tempHandler.add(tempSensor.getTempCByIndex(0));
      Serial.println(tempSensor.getTempCByIndex(0));
    }
    else if (Configuration.DefaultBacktoBoard)
    {
      tempSensor.requestTemperatures();
      tempHandler.addFallback(tempSensor.getTempCByIndex(0));
    }
  }
  // Runs Skynet AC control.
  if (now() - Timers.Skynet > 2)
  {
    Timers.Skynet = now();
    Skynet.run(tempHandler.currentTemperatureWithTime());
  }
  // Try to sync time if its not synced yet.
  if (now() - Timers.sync_time > 300 && !control_variables.time_synced)
  {
    Timers.sync_time = now();
    control_variables.time_synced = getTime();
  }
  // Publishes
  if (now() - Timers.mqtt_publish > 2)
  {
    Timers.mqtt_publish = now();
#ifdef USE_LDR
    int _ldrVal = 0;
    for (size_t i = 0; i < NUM_OF_ANALOG_READS; i++)
    {
      _ldrVal += analogRead(LDR_PIN);
    }
    _ldrVal = _ldrVal / NUM_OF_ANALOG_READS;
    MqttSend("/sensors/ldr", String(_ldrVal));
#endif
    MqttSend("/sensors/Temperature", String(tempHandler.currentTemperature()));
  }
  // Try to reconnect to MQTT broker if it is not connected.
  if (now() - Timers.mqtt_reconnect > 10 && !HiveMQ.connected())
  {
    Timers.mqtt_reconnect = now();
    MQTT_Reconnect();
  }
  // Runs the Skynet for AC Control
  if (Configuration.CompareTime())
  {
    if (!control_variables.ac_shutdown_sent)
    {
      if (Configuration.TemperatureThreshold)
      {
        if (tempHandler.currentTemperature() <= Configuration.TemperatureThreshold - IDLE_TOLERANCE)
        {
          // turn off the ac and stops the temperature control
          Skynet.setPower(false);
          Skynet.setTaget(0);
          control_variables.ac_shutdown_sent = true;
        }
      }
      else
      {
        // turn off the ac and stops the temperature control
        Skynet.setPower(false);
        Skynet.setTaget(0);
        control_variables.ac_shutdown_sent = true;
      }
    }
  }
  else
  {
    control_variables.ac_shutdown_sent = false;
  }
  // Handle software shutdown
  if (now() >= Skynet.software_sleep && Skynet.software_sleep_requested)
  {
    Skynet.setPower(false);
    Skynet.software_sleep_requested = false;
  }
  if (now() - Timers.Last_Skynet_Report > 10)
  {
    Skynet.publishReport();
  }
}
