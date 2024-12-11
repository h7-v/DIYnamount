/*********
  Project code written by
  https://github.com/h7-v
*********/

/*********
  ESP32 Web server code used from
  Rui Santos
  Complete project details at
  https://RandomNerdTutorials.com/esp32-websocket-server-arduino/
  https://RandomNerdTutorials.com/esp8266-nodemcu-web-server-websocket-sliders/
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
*********/

// Please see README.md in this repository for more information.

/*
Includes and defines
*/
// Import required libraries
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h> // https://github.com/dvarrel/AsyncTCP
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

// Fixes for HTTP compiler errors (if needed)
//#define WEBSERVER_H
//#include "WebServer.h"

#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include <SPIFFS.h> // https://github.com/espressif/arduino-esp32/tree/master/libraries/SPIFFS
#include <Arduino_JSON.h> // https://github.com/arduino-libraries/Arduino_JSON

// Breadboard position from left to right: ESP32, TMC2208 (MOTORPOS) for
// speaker position, TMC2208 (MOTORDIST) for speaker distance.
// POS = Microphone horizontal position to front of speaker
// DIST = Microphone distance from front of speaker
// EN = Motor driver enable/disable
// DIR = Motor spin direction clockwise/anticlockwise
// STEP = Step motor step pulse
#define MOTORPOSEN 26 // HIGH turns motor off, LOW turns motor on
#define MOTORDISTEN 32 // HIGH turns motor off, LOW turns motor on
#define MOTORPOSDIR 14
#define MOTORPOSSTEP 27
#define MOTORDISTDIR 25
#define MOTORDISTSTEP 33

// See README.md
#define STEPSPERUNITMOVEMENT 58
#define MOTORSPEED 700

/*
setup() functions and lib init
*/
// Below two lines allow for WiFi connection without WiFi manager
//const char* ssid     = "your_network_ssid";
//const char* password = "your_network_pass";


// If WiFi disconnects while in use, LED connection light turns off and
// device restarts
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
    digitalWrite(2, LOW);
    ESP.restart();
}


void initWiFiManager() {
  //WiFiManager, Local intialization
  //Once its business is done, there is no need to keep it around
  WiFiManager wm;
  wm.setHttpPort(81);
  // WiFi manager is designed to be displayed upon connection to the stand
  // network similarly to how is done on hotel networks. This is done by
  // running a web server on port 80 that the device can then interface with.
  // This is however not possible here as the AsyncWebServer can only run on
  // port 80.
  // In order to connect to the stand network for WiFi config,
  // use a web browser to visit 192.168.4.1:81

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  // Resetting as the device will be moving to different networks often
  wm.resetSettings();

  // set dark theme
  wm.setClass("invert");

  // set static ip,gw,sn
  wm.setSTAStaticIPConfig(IPAddress(192,168,1,117), // IP address
                          IPAddress(192,168,1,1), // Gateway
                          IPAddress(255,255,255,0)); // Subnet mask

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified ssid.
  bool res; // used to indicate successful connection
  
  // wm.autoConnect("ssid", "password"); or ignore second parameter for open
  // network.
  // This method is used to set the ssid and password for connecting to the
  // ESP32 for WiFi connection setup to a WiFi network
  res = wm.autoConnect("DIYnamount Config");

  if(!res) {
      Serial.println("Failed to connect");
      // ESP.restart(); // or consider restarting manually
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // 3 ESP32 LED flashes to indicate successful connection
      digitalWrite(2, HIGH);
      delay(200);
      digitalWrite(2, LOW);
      delay(200);
      digitalWrite(2, HIGH);
      delay(200);
      digitalWrite(2, LOW);
      delay(200);
      digitalWrite(2, HIGH);
}
  

void initFileSystem() {
  if(!SPIFFS.begin()){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
  } else {
    Serial.println("Mounting SPIFFS successful");
  }
}


/*
Physical parts setup
*/
//**************************************************
// Motor setup. You will need to tweak these values according to the size
// or your microphone stand. The below settings apply to TMC2208 motor drivers.

// CAUTION: The following 2 variables were used for testing when the hardware
// did not exist.
// The new value at stepsForDividedTurn is used with the actual hardware.

// Change this to fit the number of motor steps per revolution
// 2048 for ULN2003 motors but not sure about microsteps for TMC2208 - 
// TMC2208 supposedly 200 with default settings.
//const int stepsPerRevolution = 200;

// Set the division according to how much the motor should rotate per one
// slider step
//const int stepsForDividedTurn = stepsPerRevolution / 5;

// TWEAK THIS FOR THE LENGTH OF THE ALUMINIUM!
// This value represents the amount of steps the motor should turn for
// a single increase or decrease in value on the web GUI slider
// In this case 5800 steps would move from position 1 to 100. This must be
// adjusted according to the length of your aluminium extrusion.
// This constant also assumes that both position and distance extrusions are
// the same length.
const int stepsForOneSliderMove = STEPSPERUNITMOVEMENT;

// Time spent waiting between step pulses. Lower values rotate the motors
// faster. This constant controls the speed of both motors simultaneously.
const int motorStepDelay = MOTORSPEED;
//**************************************************

// x_pos = Horizontal position to speaker
// y_pos = Distance from speaker
int stand_x_pos_control = 50; // Value retrieved from web server on pos change
int stand_y_pos_control = 100;
int stand_x_pos = 50; // Real microphone position
int stand_y_pos = 100;

// Difference ints to prevent the creation of a new var each cycle of
// controlSteppers()
int x_diff = 0;
int y_diff = 0;

/*
WebServer
*/
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String message = ""; // message sent by web socket
String sliderValue1 = "50"; // ESP32 tracking of web server slider position
String sliderValue2 = "100";

//Json Variable to Hold Slider Values
JSONVar sliderAndLiveValues;


//Get Slider Values and ACTUAL LIVE VALUES
String getSliderAndLiveValues(){
  sliderAndLiveValues["sliderValue1"] = String(sliderValue1);
  sliderAndLiveValues["sliderValue2"] = String(sliderValue2);
  sliderAndLiveValues["stateposition"] = String(stand_x_pos);
  sliderAndLiveValues["statedistance"] = String(stand_y_pos);

  String jsonString = JSON.stringify(sliderAndLiveValues);
  return jsonString;
}

/*
Page CSS, HTML, Javascript
*/
// All code for the control webpage is found below. CSS file must be uploaded
// via the Arduino IDE Tools->ESP32 Sketch Data Upload or equivalent. CSS file
// be found in the /data directory of this repository. All Javascript is in the
// below HTML in the <script> tag.
// The amount of JS got a bit out of hand and could really be placed in the
// /data dir as well. It turned out to be much easier to edit at the time
// in this file however.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>DIYnamount</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="microbotstylesheet">
</head>
<body>
  <div class="topnav">
    <h1>DIYnamount Control</h1>
    <h2 id="stateconnectionstatus" style="color: #FFB121">Connecting to stand server. Please wait...</h2>
  </div>
  <div class="content">
    <div class="card">
      <h2>Speaker Position</h2>
      <p class="state">Position slider: <span id="sliderValue1"></span></p>
      <p class="state">Real mic position: <span id="stateposition">%STATE%</span></p>
      <img src="v30speaker" style="object-fit:contain; width:100%%;">
      <p class="switch">
      <input type="range" id="slidersm57" class="slidersm57" min="0" max="100" step="1" value ="50" disabled>
      </p>
      <p class="switch">
      <input type="range" oninput="updateSliderPWM(this)" id="slider1" min="0" max="100" step="1" value ="50" class="slider">
      </p>
      <button id="buttonleft" onclick="updateSliderPWMFromButton(this)" class="button" disabled>Move Left (-1)</button>
      <button id="buttonright" onclick="updateSliderPWMFromButton(this)" class="button" disabled>Move Right (+1)</button>
    </div>
    <div class="card">
      <h2>Speaker Distance</h2>
      <p class="state">Distance slider: <span id="sliderValue2"></span></p>
      <p class="state">Real mic distance: <span id="statedistance">%STATE%</span></p>
      <p class="switch">
      <input type="range" oninput="updateSliderPWM(this)" id="slider2" min="0" max="100" step="1" value ="100" class="slider">
      </p>
      <button id="buttonfurther" onclick="updateSliderPWMFromButton(this)" class="button" disabled>Move Further (-1)</button>
      <button id="buttoncloser" onclick="updateSliderPWMFromButton(this)" class="button" disabled>Move Closer (+1)</button>
    </div>
    <button id="buttoncalibrate" class="button" disabled>Calibrate Controls</button>
  </div>
<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  window.addEventListener('load', onLoad);
  const stateconnectionstatus = document.getElementById('stateconnectionstatus');
  let hasConnectedOnceFlag = false;

  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
  }

  function setUIConnected() {
    stateconnectionstatus.style.color = '#00FF00'
    stateconnectionstatus.textContent = 'Connected';
    document.getElementById("buttonleft").disabled = false;
    document.getElementById("buttonright").disabled = false;
    document.getElementById("buttonfurther").disabled = false;
    document.getElementById("buttoncloser").disabled = false;
    document.getElementById("buttoncalibrate").disabled = false;
    document.getElementById("buttonleft").style.background = '#FFB121';
    document.getElementById("buttonright").style.background = '#FFB121';
    document.getElementById("buttonfurther").style.background = '#FFB121';
    document.getElementById("buttoncloser").style.background = '#FFB121';
    document.getElementById("buttoncalibrate").style.background = '#FFB121';
    document.getElementById("slider1").disabled = false;
    document.getElementById("slider2").disabled = false;
  }
  
  function onOpen(event) {
    console.log('Connection opened');
    setUIConnected();
    getValues();
    hasConnectedOnceFlag = true;
  }

  function setUIDisconnected() {
    if (hasConnectedOnceFlag) {
      stateconnectionstatus.style.color = '#FF0000'
      stateconnectionstatus.textContent = 'Connection lost. Attempting to reconnect...';
      document.getElementById("buttonleft").disabled = true;
      document.getElementById("buttonright").disabled = true;
      document.getElementById("buttonfurther").disabled = true;
      document.getElementById("buttoncloser").disabled = true;
      document.getElementById("buttoncalibrate").disabled = true;
      document.getElementById("buttonleft").style.background = '#666666';
      document.getElementById("buttonright").style.background = '#666666';
      document.getElementById("buttonfurther").style.background = '#666666';
      document.getElementById("buttoncloser").style.background = '#666666';
      document.getElementById("buttoncalibrate").style.background = '#666666';
      document.getElementById("slider1").disabled = true;
      document.getElementById("slider2").disabled = true;
    }
  }
  
  function onClose(event) {
    console.log('Connection closed');
    stateconnectionstatus.style.color = '#FF0000'
    stateconnectionstatus.textContent = 'Connection lost. Attempting to reconnect...';
    document.getElementById("buttonleft").disabled = true;
    document.getElementById("buttonright").disabled = true;
    document.getElementById("buttonfurther").disabled = true;
    document.getElementById("buttoncloser").disabled = true;
    document.getElementById("buttoncalibrate").disabled = true;
    document.getElementById("buttonleft").style.background = '#666666';
    document.getElementById("buttonright").style.background = '#666666';
    document.getElementById("buttonfurther").style.background = '#666666';
    document.getElementById("buttoncloser").style.background = '#666666';
    document.getElementById("buttoncalibrate").style.background = '#666666';
    document.getElementById("slider1").disabled = true;
    document.getElementById("slider2").disabled = true;
    setTimeout(initWebSocket, 2000);
  }

  function updateSliderPWM(element) {
    var sliderNumber = element.id.charAt(element.id.length-1);
    var sliderValue = document.getElementById(element.id).value;
    document.getElementById("sliderValue"+sliderNumber).innerHTML = sliderValue;
//    console.log("updateSliderPWM val:" + sliderValue);
    websocket.send(sliderNumber+"s"+sliderValue.toString());
  }

  function updateSliderPWMFromButton(element) {
    var buttonName = element.id;
    
    switch(buttonName) {
      case "buttonleft":
        var sliderValue = document.getElementById('slider1').value;
        if (sliderValue != 0) {
          sliderValue--;
          websocket.send(1+"s"+sliderValue.toString());
          break;
        } else {
          break;
        }
        
      case "buttonright":
        var sliderValue = document.getElementById('slider1').value;
        if (sliderValue != 100) {
          sliderValue++;
          websocket.send(1+"s"+sliderValue.toString());
          break;
        } else {
          break;
        }
        
      case "buttonfurther":
        var sliderValue = document.getElementById('slider2').value;
        if (sliderValue != 0) {
          sliderValue--;
          websocket.send(2+"s"+sliderValue.toString());
          break;
        } else {
          break;
        }
        
      case "buttoncloser":
        var sliderValue = document.getElementById('slider2').value;
        if (sliderValue != 100) {
          sliderValue++;
          websocket.send(2+"s"+sliderValue.toString());
          break;
        } else {
          break;
        }
        
      default:
        console.log("updateSliderPWMFromButton() case condition not met");
    }
  }

  function connectionLost() {
    if (hasConnectedOnceFlag) {
      setUIDisconnected();
    }
  }
  var connectionTimeout = setTimeout(connectionLost, 10000);

  function onMessage(event) {
//    console.log(event.data);
    setUIConnected();
    clearTimeout(connectionTimeout);
    connectionTimeout = setTimeout(connectionLost, 10000);

    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);

    for (var i = 0; i < 2; i++){ // this 2 was keys.length before I added two additional object keys which are handled below this loop
        var key = keys[i];
        document.getElementById(key).innerHTML = myObj[key];
        document.getElementById("slider"+ (i+1).toString()).value = myObj[key];
    }

    var stateposition_key = keys[2];
    var statedistance_key = keys[3];
    document.getElementById('stateposition').innerHTML = myObj[stateposition_key];
    document.getElementById('statedistance').innerHTML = myObj[statedistance_key];

    document.getElementById('slidersm57').value = myObj[stateposition_key];
  }
  
  function onLoad(event) {
    initWebSocket();
    initButtons();
  }
  
  function initButtons() {
    document.getElementById('buttonleft').addEventListener('click', moveleftclick);
    document.getElementById('buttonright').addEventListener('click', moverightclick);
    document.getElementById('buttonfurther').addEventListener('click', movefurtherclick);
    document.getElementById('buttoncloser').addEventListener('click', movecloserclick);
    document.getElementById('buttoncalibrate').addEventListener('click', calibrateclick);
  }
  
  function moveleftclick(){
    websocket.send('moveleft');
    
  }
  
  function moverightclick(){
    websocket.send('moveright');
  }

  function movefurtherclick(){
    websocket.send('movefurther');
  }

  function movecloserclick(){
    websocket.send('movecloser');
  }

  function calibrateclick(){
    websocket.send('calibrate');
  }

  function getValues(){
    if (hasConnectedOnceFlag) {
      console.log("getValues() called");
      try {
        websocket.send('getValues');
      }
      catch(err) {
        setUIDisconnected();
      }
    }
    
  }
  const interval = setInterval(getValues, 2000);
  
</script>
</body>
</html>
)rawliteral";

/*
WebSocket response functions
*/
void notifyClients(String sliderAndLiveValues) {
  Serial.println("Slider values from notifyClients: ");
  Serial.println(sliderAndLiveValues);
  ws.textAll(sliderAndLiveValues);
}


void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    message = (char*)data;
    if (message.indexOf("1s") >= 0) {
      sliderValue1 = message.substring(2);
      stand_x_pos_control = sliderValue1.toInt();
      Serial.print(getSliderAndLiveValues());
      notifyClients(getSliderAndLiveValues());
    }
    if (message.indexOf("2s") >= 0) {
      sliderValue2 = message.substring(2);
      stand_y_pos_control = sliderValue2.toInt();
      Serial.print(getSliderAndLiveValues());
      notifyClients(getSliderAndLiveValues());
    }
    if (strcmp((char*)data, "getValues") == 0) {
      notifyClients(getSliderAndLiveValues());
    }

      if (strcmp((char*)data, "calibrate") == 0) {
        stand_x_pos_control = 50;
        stand_y_pos_control = 100;
        stand_x_pos = 50;
        stand_y_pos = 100;
        sliderValue1 = "50";
        sliderValue2 = "100";
        notifyClients(getSliderAndLiveValues());
      }
  }
}


void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}


/*
WebSocket init
*/
void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  Serial.printf("WebSocket initiated.");
}


String processor(const String& var){
  Serial.println(var);
  // There's no real reason to have this code run with all the other update functions running
//  if(var == "STATE"){
//    String str_stand_x_pos = String(stand_x_pos);
//    return str_stand_x_pos;
//  }
  return String();
}

/*
setup()
*/
void setup(){
  // Serial port for debugging purposes
  Serial.begin(115200);

  pinMode(2, OUTPUT); // set the LED pin mode
  // Set pin modes for motor control pins
  pinMode(MOTORPOSEN, OUTPUT);
  pinMode(MOTORDISTEN, OUTPUT);
  pinMode(MOTORPOSDIR, OUTPUT);
  pinMode(MOTORPOSSTEP, OUTPUT);
  pinMode(MOTORDISTDIR, OUTPUT);
  pinMode(MOTORDISTSTEP, OUTPUT);

  // Motor Setup
  // Disable motors so that current is not drawn when not in use
  digitalWrite(MOTORPOSEN, HIGH);
  digitalWrite(MOTORDISTEN, HIGH);

  // WiFi setup
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  initWiFiManager();
  
  initFileSystem();
  
  initWebSocket();

  // Below files found in the /data directory of this repository
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Route for CSS file
  server.on("/microbotstylesheet", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/microbotstylesheet.css", "text/css");
  });

  // Route for Celestion v30 png file
  server.on("/v30speaker", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/vintage30.png", "image/png");
  });

  // Route for sm57 png file
  server.on("/sm57", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/sm57transcrop.png", "image/png");
  });

  // Start web server
  server.begin();
}


/*
loop() functions
*/
// Motor direction (false is clockwise, true is anticlockwise) and number of steps
void spinMotor1(const bool &dir, const int &amt) {
  digitalWrite(MOTORPOSEN, LOW); // Enable motor
  delayMicroseconds(motorStepDelay);
  
  if (dir == false) { // Set direction
    digitalWrite(MOTORPOSDIR, HIGH);
  } else if (dir == true) {
    digitalWrite(MOTORPOSDIR, LOW);
  }
  delayMicroseconds(motorStepDelay);

  for (int i = 0; i < amt; i++) { // Turn motor
    digitalWrite(MOTORPOSSTEP, HIGH);
    delayMicroseconds(motorStepDelay);
    digitalWrite(MOTORPOSSTEP, LOW);
    delayMicroseconds(motorStepDelay);
  }
  digitalWrite(MOTORPOSEN, HIGH); // Disable motor when work is done
}


// Motor direction (true is clockwise, false is anticlockwise) and number of steps
void spinMotor2(const bool &dir, const int &amt) {
  digitalWrite(MOTORDISTEN, LOW); // Enable motor
  delayMicroseconds(motorStepDelay);
  
  if (dir == false) { // Set direction
    digitalWrite(MOTORDISTDIR, HIGH);
  } else if (dir == true) {
    digitalWrite(MOTORDISTDIR, LOW);
  }
  delayMicroseconds(motorStepDelay);

  for (int i = 0; i < amt; i++) { // Turn motor
    digitalWrite(MOTORDISTSTEP, HIGH);
    delayMicroseconds(motorStepDelay);
    digitalWrite(MOTORDISTSTEP, LOW);
    delayMicroseconds(motorStepDelay);
  }
  digitalWrite(MOTORDISTEN, HIGH); // Disable motor when work is done
}


void controlSteppers() {
  // If the real position is not the same as the GUI control
  if (stand_x_pos != stand_x_pos_control) {
    
    if (stand_x_pos > stand_x_pos_control) {
      // Calc the difference between the real position and control
      x_diff = stand_x_pos - stand_x_pos_control;

      // Turn the motor to bring the real pos to the GUI control
      spinMotor1(false, (x_diff * stepsForOneSliderMove));
      
      // Update the real position
      stand_x_pos = stand_x_pos - x_diff;
    }

    if (stand_x_pos < stand_x_pos_control) {
      x_diff = stand_x_pos_control - stand_x_pos;
      spinMotor1(true, (x_diff * stepsForOneSliderMove));
      stand_x_pos = stand_x_pos + x_diff;
    }
  }

  if (stand_y_pos != stand_y_pos_control) {
    
    if (stand_y_pos > stand_y_pos_control) {
      y_diff = stand_y_pos - stand_y_pos_control;
      spinMotor2(true, (y_diff * stepsForOneSliderMove));
      stand_y_pos = stand_y_pos - y_diff;
    }

    if (stand_y_pos < stand_y_pos_control) {
      y_diff = stand_y_pos_control - stand_y_pos;
      spinMotor2(false, (y_diff * stepsForOneSliderMove));
      stand_y_pos = stand_y_pos + y_diff;
    }
  }
}


/*
loop()
*/
void loop() {
  //Web server housekeeping
  ws.cleanupClients();

  //Physical stepper motor response to value changes from sliders and buttons
  controlSteppers();
}
