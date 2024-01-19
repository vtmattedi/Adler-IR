#include "NightMareTCPCore.h"

// Prepare the msg according to the transmission mode
String PrepareMsg(String msg, TransmissionMode mode)
{
  if (mode == TransmissionMode::AllAvailable)
    return msg; // SendMsg
  else if (mode == TransmissionMode::SizeColon)
  {
    String r_msg = "";
    r_msg += msg.length();
    r_msg += ":";
    r_msg += msg;
    return r_msg;
  }
  else if (mode == TransmissionMode::ThreeCharHeaders)
  {
    //not implemented;
  }
  return msg;
}

String GetIdFromMsg(String msg)
{
  int Index = 0;
  String rtr = "";
  for (int i = 1; i < msg.length(); i++)
  {
    char c = msg[i];
    if (c == ';')
    {
      Index++;
    }
    else
      rtr += c;
  }
  return rtr;
}

