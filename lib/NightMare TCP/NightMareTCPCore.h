/*----------------------------------------------------------*/
///
///@file NightMareTCPCore.h -
/// Some common functions to NightMare TCP Server and Client
/// Author: Vitor Mattedi Carvalho
/// Date: 29/09/2023
/// Version: 1.1
///          Created.
/*----------------------------------------------------------*/

#ifndef NIGHTMARETCPCORE_H
#define NIGHTMARETCPCORE_H

#define DefaultFastChar 0xFE
#define DefaultTransmissionChar '!'
#define DONT_COMPILE_SERIAL
#define KEEP_ALIVE_MESSAGE "***keep alive***"
#define KEEP_ALIVE_RESPONSE "***ack***"
#define USE_KEEP_ALIVE_BY_DEFAULT true
#define KEEP_ALIVE_DEFAULT_INTERVAL 60000
#define DEFAULTTIMEOUT 180000 //3 mins

#include "Arduino.h"
typedef enum
{
  // not fully implemented
  ThreeCharHeaders = 2,
  // String is sent as length:message and when new data arrives, the data is read looking for the size until it reaches a colon then it outputs the msg of that size;
  SizeColon = 1,
  // String is sent and received as is.
  AllAvailable = 0
} TransmissionMode;

// Common functions

String PrepareMsg(String msg, TransmissionMode mode);

String GetIdFromMsg(String);
#endif