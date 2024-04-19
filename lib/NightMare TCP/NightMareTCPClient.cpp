#include "NightMareTCPClient.h"

#ifndef USE_KEEP_ALIVE_BY_DEFAULT
#define USE_KEEP_ALIVE_BY_DEFAULT false
#endif

String NightMareTCPClient::NightMareCommands_Client(String msg)
{
    String response = "";
    if (msg == "Help" || msg == "help" || msg == "H" || msg == "h")
    {
        response += ("Welcome to NightMare Home Systems ©\nThis is a ESP32 Module and it can answer to the following commands:\n");
        response += ("Quick obs.: the character int [19] or char () is ignored when received for facilitating reasons.");
        response += ("'A' > Gets the current state of available variables\n'L' > Toggles the LIGH_RELAY state\n");
        response += ("'T;******;' > Sets the TIMEOUT value for the tcp server.[Replace '******' with a long.\n");
        response += ("'SOFTWARE_RESET' requests a software reset.");
    }
    else if (msg == "TMODE=0" || msg == "7:TMODE=0")
    {
        transmissionMode = TransmissionMode::AllAvailable;
        response = "Transmission mode set to AllAvailable";
    }
    else if (msg == "TMODE=1" || msg == "7:TMODE=1")
    {
        transmissionMode = TransmissionMode::SizeColon;
        response = "Transmission mode set to SizeColon";
    }
    else if (msg == "REQ_ID")
    {
        response = "ID::" + _clientID;
    }
    else
    {
        // Handle Broadcast message
        int indicator_index = msg.indexOf("Z::");
        if (indicator_index >= 0)
        {
            if (_message_callback)
            {
                _message_callback(msg.substring(indicator_index + 3));
            }
            response = "M;ACK;";
        }
    }

    return response;
}

NightMareTCPClient::NightMareTCPClient(bool debug) : _message_callback(NULL), _fast_callback(NULL)
{

    if (USE_KEEP_ALIVE_BY_DEFAULT)
    {
        _keepAliveInterval = KEEP_ALIVE_DEFAULT_INTERVAL;
        _keepAlive = true;
        _keepAliveLastTime = millis() + KEEP_ALIVE_DEFAULT_INTERVAL/4;
    }
    else
    {
        _keepAlive = false;
    }
 
    client = new WiFiClient();
    _fastChar = DefaultFastChar;
    _debug = debug;
    _trasmissionChar = DefaultTransmissionChar;
}

NightMareTCPClient::NightMareTCPClient(String ID, bool debug) : _message_callback(NULL), _fast_callback(NULL)
{
    _clientID = ID;

    if (USE_KEEP_ALIVE_BY_DEFAULT)
    {
        _keepAliveInterval = KEEP_ALIVE_DEFAULT_INTERVAL;
        _keepAlive = true;
        _keepAliveLastTime = millis() + KEEP_ALIVE_DEFAULT_INTERVAL/4;
    }
    else
    {
        _keepAlive = false;
    }
    client = new WiFiClient();
    _fastChar = DefaultFastChar;
    _debug = debug;
    _trasmissionChar = DefaultTransmissionChar;
}

void NightMareTCPClient::handleClient()
{
    if (!client)
    {
        return;
    }

    if (millis() - _keepAliveLastTime > _keepAliveInterval && _keepAlive && client->connected())
    {
        _keepAliveLastTime = millis();
        client->write(KEEP_ALIVE_MESSAGE);
    }

    if (client->available() > 0)
    {
        String msg = "";
        int size = 0;
        int index = 0;
        bool sizeFound = false;
        while (client->available() > 0)
        {
            char newChar = client->read();
            if (newChar == _fastChar && _fast_callback)
            {
                _fast_callback();
                return;
            }

            if (newChar != (char)19)
            {

                if (!sizeFound && newChar == ':' && transmissionMode == TransmissionMode::SizeColon)
                {
                    size = atoi(msg.c_str());
                    sizeFound = true;
                    msg = "";
                    index = 0;
                }
                else if (sizeFound && index >= size && transmissionMode == TransmissionMode::SizeColon)
                {
                    if (msg != "" && _message_callback)
                        _message_callback(msg);
                    sizeFound = false;
                    msg = "";
                    msg += newChar;
                }
                else
                {
                    index++;
                    msg += newChar;
                }
            }
        }

        String nightmare = NightMareCommands_Client(msg);
        if (!nightmare.equals(""))
        {
            send(nightmare);
        }
        else
        {
            if (!msg.equals(KEEP_ALIVE_RESPONSE))
            {
               if (_message_callback)
                _message_callback(msg);
            }
        }
    }
}

void NightMareTCPClient::send(String msg)
{
    if (client != NULL)
    {
        client->print(PrepareMsg(msg, transmissionMode));
        _keepAliveInterval = millis();
    }
}

void NightMareTCPClient::setName(String newName)
{
    _clientID = newName;
}

String NightMareTCPClient::Name()
{
    if (_clientID)
        return _clientID;
    return "";
}

void NightMareTCPClient::fastSend()
{
    if (client != NULL)
        client->write(_fastChar);
}

void NightMareTCPClient::setdebug(bool newDebug)
{
    _debug = newDebug;
}

void NightMareTCPClient::setFastChar(char newFastChar)
{
    _fastChar = newFastChar;
}

void NightMareTCPClient::connect(String ip, int port)
{
    _ip = ip;
    _port = port;
    client->connect(_ip.c_str(), _port);
}

void NightMareTCPClient::connect(IPAddress ip, int port)
{
    _ip = ip.toString();
    _port = port;
    client->connect(ip, _port);
}

void NightMareTCPClient::reconnect()
{
    if (!client->connected())
        client->connect(_ip.c_str(), _port);
}

void NightMareTCPClient::setKeepAlive(int newInterval)
{
    if (newInterval < 0)
        _keepAlive = false;
    else
        _keepAliveInterval = newInterval;
}

NightMareTCPClient &NightMareTCPClient::setMessageHandler(TClientMessageHandler fn)
{
    _message_callback = fn;
    return *this;
}

NightMareTCPClient &NightMareTCPClient::setFastHandler(TFastHandler fn)
{
    _fast_callback = fn;
    return *this;
}
