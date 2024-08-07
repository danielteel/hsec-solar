#include <Arduino.h>
#include <WiFi.h>
#include <Esp.h>
#include "camera.h"
#include "encro.h"
WiFiClient Client;
WiFiClient Messaging;


uint32_t handshakeNumber=0;
uint32_t serverHandshakeNumber=0;

const uint8_t packetMagicBytes[]={73, 31};
const uint8_t handshakeMagicBytes[]={13, 37};

const uint8_t requestNightStatus[]={'i','n'};

const char* hostAddress="danteel.dedyn.io";
const uint16_t hostPort = 4004;


const char* WiFiSSID = "DnD";
const char* WiFiPass = "";

const char* deviceName = "Solar";
const char* encroKey = "";
const char* portalName = "HSEC Solar Setup";
const char* handshakeMessage="Lights Off:void:0,Lights On:void:1,Auto:void:2";


bool AutoMode=false;


void setup(){
    pinMode(14, OUTPUT);
    digitalWrite(14, LOW);

    Serial.begin(115200);
    Serial.println("Initializing...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WiFiSSID, WiFiPass);


    handshakeNumber=esp_random();


    Serial.printf("Name %i:%s\n", strlen(deviceName), deviceName);
    Serial.printf("Encrokey %i:%s\n", strlen(encroKey), encroKey);

    cameraSetup();
    Serial.println("camera setup...");

}

void sendPacket(const void* data, uint32_t dataLength);

void onPacket(uint8_t* data, uint32_t dataLength){
    if (data[0]==0){
        //turn lights off
        digitalWrite(14, LOW);
        Serial.print("off");
        AutoMode=false;
    }else if (data[0]==1){
        //turn lights on
        digitalWrite(14, HIGH);
        Serial.print("on");
        AutoMode=false;
    }else if (data[0]==2){
        AutoMode=true;
        if (Messaging.connected()) sendPacket(requestNightStatus, 2);
        digitalWrite(14, LOW);
    }else if (data[0]=='n' && data[1]=='f'){
        if (AutoMode){ 
            digitalWrite(14, LOW);
        }
    }else if (data[0]=='n' && data[1]=='t'){
        if (AutoMode){
            digitalWrite(14, HIGH);
        }
    }
}

void sendInitialHandshake(){
    uint32_t encryptedLength;
    uint8_t* encrypted=encrypt(handshakeNumber, (uint8_t*)handshakeMessage, strlen(handshakeMessage), encryptedLength, encroKey);
    if (encrypted){
        Messaging.write(handshakeMagicBytes, 2);
        Messaging.write((uint8_t) strlen(deviceName));
        Messaging.write(deviceName);
        Messaging.write((uint8_t*)&encryptedLength, 4);
        Messaging.write(encrypted, encryptedLength);
        delete[] encrypted;
    }else{
        Serial.println("failed to encrypt in sendInitialHandshake()");
    }
}

void sendPacket(const void* data, uint32_t dataLength){
    uint32_t encryptedLength;
    uint8_t* encrypted=encrypt(handshakeNumber, (const uint8_t*)data, dataLength, encryptedLength, encroKey);
    if (encrypted){
        Messaging.write(packetMagicBytes, 2);
        Messaging.write((uint8_t*)&encryptedLength, 4);
        Messaging.write(encrypted, encryptedLength);
        Serial.print("Sent message with handshake ");
        Serial.println(handshakeNumber);
        delete[] encrypted;
        handshakeNumber++;
    }else{
        Serial.println("failed to encrypt in sendPacket()");
    }
}

typedef enum {
    MAGIC1,
    MAGIC2,
    LEN1,
    LEN2,
    LEN3,
    LEN4,
    PAYLOAD
} PACKETWRITESTATE;

PACKETWRITESTATE packetState=PACKETWRITESTATE::MAGIC1;

uint8_t packetType=0;
uint32_t packetLength=0;
uint8_t* packetPayload = nullptr;
uint32_t packetPayloadWriteIndex = 0;
bool haveRecievedServerHandshakeNumber=false;


void resetPacketStatus(){
    if (packetPayload){
        delete[] packetPayload;
        packetPayload=nullptr;
    }
    packetState=PACKETWRITESTATE::MAGIC1;
    packetLength=0;
    packetPayloadWriteIndex=0;
    haveRecievedServerHandshakeNumber=false;
    serverHandshakeNumber=0;
}

void onError(const char* errorMsg){
    if (errorMsg){
        Serial.print("Error: ");
        Serial.println(errorMsg);
    }else{
        Serial.println("Error occured");
    }
    Messaging.stop();
}

void dataRecieved(uint8_t byte){
    switch (packetState){
        case PACKETWRITESTATE::MAGIC1:
            if (!haveRecievedServerHandshakeNumber){
                if (byte!=handshakeMagicBytes[0]){
                    onError("magic1 initial byte is incorrect");
                    return;
                }
            }else{
                if (byte!=packetMagicBytes[0]){
                    onError("magic1 byte is incorrect");
                    Serial.print(byte);
                    return;
                } 
            }

            packetState=PACKETWRITESTATE::MAGIC2;
            break;
        case PACKETWRITESTATE::MAGIC2:
            if (!haveRecievedServerHandshakeNumber){
                if (byte!=handshakeMagicBytes[1]){
                    onError("magic2 initial byte is incorrect");
                    return;
                }
            }else{
                if (byte!=packetMagicBytes[1]){
                    onError("magic2 byte is incorrect");
                    return;
                } 
            }
            packetState=PACKETWRITESTATE::LEN1;
            break;
        case PACKETWRITESTATE::LEN1:
            memmove(((uint8_t*)&packetLength)+0, &byte, 1);
            packetState=PACKETWRITESTATE::LEN2;
            break;
        case PACKETWRITESTATE::LEN2:
            memmove(((uint8_t*)&packetLength)+1, &byte, 1);
            packetState=PACKETWRITESTATE::LEN3;
            break;
        case PACKETWRITESTATE::LEN3:
            memmove(((uint8_t*)&packetLength)+2, &byte, 1);
            packetState=PACKETWRITESTATE::LEN4;
            break;
        case PACKETWRITESTATE::LEN4:
            memmove(((uint8_t*)&packetLength)+3, &byte, 1);
            if (packetPayload){
                delete[] packetPayload;
                packetPayload=nullptr;
            }
            if (packetLength>0x0FFFFF){
                onError("packet length > 0x0FFFFF");
                return;
            }
            Serial.printf("Recvd Len: %u\n", packetLength);
            packetPayload=new uint8_t[packetLength];//need to clean this up on an error
            packetState=PACKETWRITESTATE::PAYLOAD;
            packetPayloadWriteIndex=0;
            break;
        case PACKETWRITESTATE::PAYLOAD:
            packetPayload[packetPayloadWriteIndex]=byte;
            packetPayloadWriteIndex++;
            if (packetPayloadWriteIndex>=packetLength){
                uint32_t decryptedLength;
                uint32_t recvdServerHandshakeNumber;
                bool errorOccured = false;
                uint8_t* decrypted = decrypt(recvdServerHandshakeNumber, packetPayload, packetLength, decryptedLength, encroKey, errorOccured);
                delete[] packetPayload;
                packetPayload=nullptr;
                if (errorOccured){
                    onError("failed to decrypt");
                }else{       
                    if (!haveRecievedServerHandshakeNumber){
                        serverHandshakeNumber=recvdServerHandshakeNumber;
                        haveRecievedServerHandshakeNumber=true;
                    }else{
                        //Send off decrypted packet for processing
                        if (recvdServerHandshakeNumber==serverHandshakeNumber){
                            serverHandshakeNumber++;
                            onPacket(decrypted, decryptedLength);
                        }else{
                            onError("incorrect handshake number recieved");
                            Serial.printf("Recvd: %u  Expected: %u\n", recvdServerHandshakeNumber, serverHandshakeNumber);
                            if (decrypted){
                                delete[] decrypted;
                                decrypted=nullptr;
                            }
                            return;
                        }
                    }
                    if (decrypted){
                        delete[] decrypted;
                        decrypted=nullptr;
                    }
                }
                packetState=PACKETWRITESTATE::MAGIC1;
            }
            break;
    }
}

void loop(){
    static uint32_t lastCaptureTime=0;
    static uint32_t lastConnectAttemptTime=0;
    static uint32_t lastAutoModeCheck=0;

    uint32_t currentTime = millis();

    if (WiFi.status()!=WL_CONNECTED){
        if (WiFi.status()==WL_CONNECT_FAILED){
            WiFi.disconnect();
            WiFi.begin(WiFiSSID, WiFiPass);
        }
        delay(500);
    }else{
        if (!Messaging.connected() && ((currentTime-lastConnectAttemptTime)>=5000 || currentTime<lastConnectAttemptTime)){ 
            lastConnectAttemptTime=currentTime;
            resetPacketStatus();
            if (Messaging.connect(hostAddress, hostPort)){
                sendInitialHandshake();
            }
        }else if (Messaging.connected()){
            while (Messaging.connected() && Messaging.available()){
                byte message;
                Messaging.readBytes(&message, 1);
                dataRecieved(message);
            }

            if ((currentTime-lastCaptureTime)>=2000 || currentTime<lastCaptureTime){
                CAMERA_CAPTURE capture;

                if (cameraCapture(capture)){
                    if (Messaging.connected()) sendPacket(capture.jpgBuff, capture.jpgBuffLen);
                    cameraCaptureCleanup(capture);
                }else{
                    Serial.println("failed to capture ");
                }
                lastCaptureTime=currentTime;
            }

            if (((currentTime-lastAutoModeCheck)>=30000 || currentTime<lastAutoModeCheck) && AutoMode){
                lastAutoModeCheck=currentTime;
                if (Messaging.connected()) sendPacket(requestNightStatus, 2);
            }

        }
    }
}
