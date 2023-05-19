/*
  Florian Couturier

  Complete project details at https://github.com/Flobtmkg/8266_rest_client

  Based on the work of Rui Santos (details at  https://RandomNerdTutorials.com/esp8266-nodemcu-http-get-post-arduino/)
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  
  Code compatible with ESP8266 Boards Version 3.0.0 or above 
  (see in Tools > Boards > Boards Manager > ESP8266)
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Arduino_JSON.h>
#include <WiFiClientSecureBearSSL.h>

//                                            ------------------------------
//                                            |global technical consts block|
//                                            ------------------------------

static const bool debugMode = false;

const static char START_MESSAGE_BLOCK = '\31';                          // ASCII char to trigger the start of an inter-chip message
const static char STOP_MESSAGE_BLOCK = '\23';                           // ASCII char to trigger the end of an inter-chip message

// JSON keys for inter-chip messages
PROGMEM static const char MSG_ENUM_CMD[] = "CMD";
PROGMEM static const char MSG_ENUM_CMD_SEQ[] = "CMD_SEQ";
PROGMEM static const char MSG_ENUM_CODE[] = "CODE";
PROGMEM static const char MSG_ENUM_ENDPOINT[] = "ENDPOINT";
PROGMEM static const char MSG_ENUM_LOGIN[] = "LOGIN";
PROGMEM static const char MSG_ENUM_PASSWORD[] = "PASSWORD";
PROGMEM static const char MSG_ENUM_DATA[] = "DATA";

// Other keys
PROGMEM static const char JSON_DEVICE[] = "deviceId";
PROGMEM static const char REQ_CONTENT_KEY[] = "Content-Type";
PROGMEM static const char REQ_CONTENT_VALUE[] = "application/json";
PROGMEM static const char FINGERPRINT[] = "net_sha1";

// Inter-chip commands enumeration
PROGMEM static const char CMD_HTTPGET[] = "HTTPGET";
PROGMEM static const char CMD_HTTPPOST[] = "HTTPPOST";
PROGMEM static const char CMD_HTTPSGET[] = "HTTPSGET";
PROGMEM static const char CMD_HTTPSPOST[] = "HTTPSPOST";
PROGMEM static const char CMD_NTWKCHANG[] = "NTWKCHANG";

PROGMEM static const char JSON_DEFAULT[] = "{}";
PROGMEM static const char UNDEFINED[] = "undefined";

//                                            -----------------------------
//                                            |global specific consts block|
//                                            -----------------------------

PROGMEM static const char DEVICE_ID[]= "18faa0dd7a927906cb3e38fd4ff6899fbe468e6841caac211b8d2cebe0ef4a44";

//                                            --------------------
//                                            |global struct block|
//                                            -------------------- 

// Struct to store informations about HTTP response
struct HTTPResponse {
  int responseCode;
  char* payload;
};


//                                            ---------------------------
//                                            |Internal program variables|
//                                            ---------------------------

String connexionSSID = "";                                              // wifi network name 
String connexionPSWRD = "";                                             // wifi password
String tlsFingerprint = "";                                             // TLS Cert Fingerprint

//String debugApiPath = "http://192.168.0.13:8080/api/v1/datapoints";
String debugApiPath = "https://httpbin.org/get";

unsigned long debugLastTime = 0;
static const unsigned long debugTimerDelay = 60000;

String messageBuffer = "";                                              // Inter-chip message String buffer
bool isReading = false;                                                 // to flag a "currently reading" state from serial


//                                            ----------------
//                                            |Arduino program|
//                                            ----------------


void setup() {

  delay(5000); // Convenient init delay for debug
  
  // start serial
  Serial.begin(115200);
 
  if(debugMode == true){
    Serial.println(F("debug mode enabled, the wifi module will perform a test request every 60 seconds."));
    connexionSSID="xxxx";                                               // Hardcode here your wifi SSID for testing
    connexionPSWRD="xxxxx";                                             // Hardcode here your wifi password for testing
  }

  // wifi init
  initWifiConnection();

}


void loop() {
  // debug mode, Send an HTTP request depending on debugTimerDelay
  if (debugMode == true && (millis() - debugLastTime) > debugTimerDelay) {
    debugLoop();
  } else {
    checkForArduinoCommandMessages();
  }
}


void debugLoop() {
  //Check WiFi connection status
  if(WiFi.status()== WL_CONNECTED){
      
    JSONVar myData;
    tlsFingerprint = "226EACE3C99C47AFD453CECCA4ECF0A2E530D762";        // SHA-1 Cert fingerprint of https://httpbin.org for testing
    HTTPResponse httpResponse = httpsGETRequest(debugApiPath, true, "", "", &myData, false);

    Serial.println("payload :");
    Serial.println(httpResponse.payload);

  } else {
      Serial.println(F("WiFi Disconnected"));
  }
  debugLastTime = millis();
}


void initWifiConnection(){
  // wifi init
  if(connexionSSID.length() > 0){
    WiFi.begin(connexionSSID, connexionPSWRD);
    Serial.println(F("Connecting to wifi "));
    Serial.print(connexionSSID);
    Serial.println();
    while(WiFi.status() != WL_CONNECTED) {
      delay(250);
      Serial.print(F("."));
    }
    Serial.print(F("Connected to WiFi network with IP Address: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi connexion informations are not set."));
  }
}


void checkForArduinoCommandMessages(){

  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (!isReading && inChar == START_MESSAGE_BLOCK) {
      // the start of a message has been detected => we init a string buffer
      isReading = true; 
      messageBuffer = "";
    } else if (isReading && inChar == STOP_MESSAGE_BLOCK) {
      // the end of a message has been detected => we parse the message, keep the sequence up to date then empty the buffer
      isReading = false; 
      JSONVar commandRequest = JSON.parse(messageBuffer);
      
      // If we have the commandRequest
      if(JSON.typeof(commandRequest) != String(FPSTR(UNDEFINED)) && ((int)commandRequest[String(FPSTR(MSG_ENUM_CODE))]) == 0){
        commandManagement(commandRequest);
      }
      // Free the buffer
      messageBuffer = "";

    } else if (isReading) {
      messageBuffer = messageBuffer + inChar; // we append the char to the string buffer
    }
  }
}


void commandManagement(JSONVar commandMessage){

  const char* cmd = (const char*) commandMessage[String(FPSTR(MSG_ENUM_CMD))];
  unsigned long cmd_seq = (unsigned long) commandMessage[String(FPSTR(MSG_ENUM_CMD_SEQ))];
  int code = (int) commandMessage[String(FPSTR(MSG_ENUM_CODE))];
  String entrypoint = (String) commandMessage[String(FPSTR(MSG_ENUM_ENDPOINT))];
  const char* login = (const char*) commandMessage[String(FPSTR(MSG_ENUM_LOGIN))];
  const char* password = (const char*) commandMessage[String(FPSTR(MSG_ENUM_PASSWORD))];
  JSONVar data = commandMessage[String(FPSTR(MSG_ENUM_DATA))];
        
  // Init default values
  HTTPResponse httpResponse;
  httpResponse.responseCode = -2;
  httpResponse.payload = (char*)String(FPSTR(JSON_DEFAULT)).c_str();

  // command execution
  if(strcmp(cmd, String(FPSTR(CMD_HTTPGET)).c_str()) == 0){
    if(WiFi.status() == WL_CONNECTED){
      httpResponse = httpGETRequest(entrypoint, login, password, &data, true);
    } else {
      httpResponse.responseCode = -2;
      httpResponse.payload = (char*)String(FPSTR(JSON_DEFAULT)).c_str();
    }
  } else if(strcmp(cmd, String(FPSTR(CMD_HTTPPOST)).c_str()) == 0){
    if(WiFi.status()== WL_CONNECTED){
      httpResponse = httpPOSTRequest(entrypoint, login, password, &data);
    } else {
      httpResponse.responseCode = -2;
      httpResponse.payload = (char*)String(FPSTR(JSON_DEFAULT)).c_str();
    }
  } else if(strcmp(cmd, String(FPSTR(CMD_HTTPSGET)).c_str()) == 0){
    if(WiFi.status() == WL_CONNECTED){
      if(tlsFingerprint.length() == 20){
        httpResponse = httpsGETRequest(entrypoint, true, login, password, &data, true);
      } else{
        httpResponse = httpsGETRequest(entrypoint, false, login, password, &data, true); //  Insecure mode, no cert verification if tlsFingerprint is not set 
      }
    } else {
      httpResponse.responseCode = -2;
      httpResponse.payload = (char*)String(FPSTR(JSON_DEFAULT)).c_str();
    }
  } else if(strcmp(cmd, String(FPSTR(CMD_HTTPSPOST)).c_str()) == 0){
    // not implemented yet!
  } else if(strcmp(cmd, String(FPSTR(CMD_NTWKCHANG)).c_str()) == 0){

    
    tlsFingerprint = String(data[String(FPSTR(FINGERPRINT))]);
    connexionSSID = login;
    connexionPSWRD = password;
    initWifiConnection();

    httpResponse.responseCode = 1;  // code 1 if ok else -2 (not connected) timeout logic to implement here...
    httpResponse.payload = (char*)String(FPSTR(JSON_DEFAULT)).c_str();
  } else {
    httpResponse.responseCode = -3;
    httpResponse.payload = (char*)String(FPSTR(JSON_DEFAULT)).c_str();
  }

  // build response
  JSONVar responseMessage;
  responseMessage[String(FPSTR(MSG_ENUM_CMD))] = cmd;
  responseMessage[String(FPSTR(MSG_ENUM_CMD_SEQ))] = cmd_seq;
  responseMessage[String(FPSTR(MSG_ENUM_CODE))] = httpResponse.responseCode;      
  responseMessage[String(FPSTR(MSG_ENUM_ENDPOINT))] = "";
  responseMessage[String(FPSTR(MSG_ENUM_LOGIN))] = login;
  responseMessage[String(FPSTR(MSG_ENUM_PASSWORD))] = "";
  responseMessage[String(FPSTR(MSG_ENUM_DATA))] = JSON.parse(httpResponse.payload);
  // Print response to serial
  Serial.println(START_MESSAGE_BLOCK + JSON.stringify(responseMessage) + STOP_MESSAGE_BLOCK);

}


// Send http GET to an address
HTTPResponse httpGETRequest(String contactPath, const char* authLogin, const char* authPassword, JSONVar* objectToSend, bool mustSendID) {
  
  WiFiClient client;
  HTTPClient http;

  // add DEVICE_ID
  if(mustSendID){
    (*objectToSend)[String(FPSTR(JSON_DEVICE))] = String(FPSTR(DEVICE_ID)).c_str();
  }

  if(JSON.typeof(*objectToSend) != String(FPSTR(UNDEFINED))){
    // JSON Object transform to parameter string for GET
    String params = "?";
    for (int i = 0; i < (*objectToSend).keys().length(); i++) {
      String key = (*objectToSend).keys()[i];
      String value = (String)(*objectToSend)[key];
      params = params + key + "=" + value;
    }

    // add params to contactPath
    contactPath = contactPath + params;
  }
  
  // init the request
  http.begin(client, contactPath.c_str());

  // authentication
  if(String(authLogin).length() > 0){
    http.setAuthorization(authLogin, authPassword);
  }

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  String payload = String(FPSTR(JSON_DEFAULT)); 

  if (httpResponseCode >= 200 && httpResponseCode < 300) {
    payload = http.getString();
  } else if (httpResponseCode < 0) {
    Serial.println(F("Connection to the server has failed."));
  }

  // Free resources
  http.end();

  HTTPResponse getResponse;
  getResponse.responseCode = httpResponseCode;
  getResponse.payload = (char*) payload.c_str();

  return getResponse;
}


// Send http GET to an address
HTTPResponse httpsGETRequest(String contactPath, bool isSecure, const char* authLogin, const char* authPassword, JSONVar* objectToSend, bool mustSendID) {
  
  WiFiClientSecure client;
  HTTPClient http;

  // add DEVICE_ID
  if(mustSendID){
    (*objectToSend)[String(FPSTR(JSON_DEVICE))] = String(FPSTR(DEVICE_ID)).c_str();
  }

  if(!isSecure){
    client.setInsecure();
  } else {
    client.setFingerprint(tlsFingerprint.c_str());
  }

  if(JSON.typeof(*objectToSend) != String(FPSTR(UNDEFINED))){
    // JSON Object transform to parameter string for GET
    String params = "?";
    for (int i = 0; i < (*objectToSend).keys().length(); i++) {
      String key = (*objectToSend).keys()[i];
      String value = (String)(*objectToSend)[key];
      params = params + key + "=" + value;
    }

    // add params to contactPath
    contactPath = contactPath + params;
  }
  
  // init the request
  http.begin(client, contactPath.c_str());

  // authentication
  if(String(authLogin).length() > 0){
    http.setAuthorization(authLogin, authPassword);
  }

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  String payload = String(FPSTR(JSON_DEFAULT)); 

  if (httpResponseCode >= 200 && httpResponseCode < 300) {
    payload = http.getString();
  } else if (httpResponseCode < 0) {
    Serial.println(F("Connection to the server has failed."));
  }

  // Free resources
  http.end();

  HTTPResponse getResponse;
  getResponse.responseCode = httpResponseCode;
  getResponse.payload = (char*) payload.c_str();

  return getResponse;
}


// Send http POST to an address
HTTPResponse httpPOSTRequest(String contactPath, const char* authLogin, const char* authPassword, JSONVar* objectToSend) {

  WiFiClient client;
  HTTPClient http;

  //add DEVICE_ID
  (*objectToSend)[String(FPSTR(JSON_DEVICE))] = String(FPSTR(DEVICE_ID)).c_str();

  // init the request
  http.begin(client, contactPath.c_str());

  // header JSON
  http.addHeader(String(FPSTR(REQ_CONTENT_KEY)), String(FPSTR(REQ_CONTENT_VALUE)));

  // authentication
  if(String(authLogin).length() > 0){
    http.setAuthorization(authLogin, authPassword);
  }

  // Get JSON string
  String jsonString = String(FPSTR(JSON_DEFAULT));
  if(JSON.typeof(*objectToSend) != String(FPSTR(UNDEFINED))){
    jsonString = JSON.stringify(*objectToSend);
  }

  // Send HTTP POST request => POST needs String arg
  int httpResponseCode = http.POST(jsonString);

  String payload = String(FPSTR(JSON_DEFAULT)); 

  if (httpResponseCode >= 200 && httpResponseCode < 300) {
    payload = http.getString();
  } else if (httpResponseCode < 0) {
    Serial.println(F("Connection to the server has failed."));
  }
  // Free resources
  http.end();

  HTTPResponse postResponse;
  postResponse.responseCode = httpResponseCode;
  postResponse.payload = (char*) payload.c_str();

  return postResponse;
}
