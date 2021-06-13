#include <ESP8266WiFi.h>
#include <Stepper.h>
#define MQTT_SOCKET_TIMEOUT 1
#include <PubSubClient.h>
#include "const_Credentials.h"
#include <EEPROM.h>

WiFiClient espClient;
PubSubClient mqttclient(espClient);
char statusTopic[32];
char connTopic[32];
char controlTopic[32];
char levelTopic[32];
char rawlevelTopic[32];
int mqttconnectattempts=0;
int wificonnectattempts=0;
int setRawLevel=10;
int setCalLevel=0;
int currRawLevel=10;
int currCalLevel=0;
const int maxspeed = 10; //rpm 15 is an absolute max for current motor
const int maxRawLevel = 120;
const int minRawLevel = 0;
const int maxCalLevel = 100;
const int minCalLevel = 0;
const int resetTrigger = 257;
const int requestStatus = 258;
const int requestRawStatus = 259;
const int stepstolevel=2000;
long currstep=currRawLevel*stepstolevel;
long targetstep=currstep;
unsigned long lastPhysInput = 0; // physical input in millis
unsigned long lastStatusBroadcast=0;
bool finishedMove=true;
bool eEPROM_FinishedMove;
volatile bool rezeroRequested=false;
bool requestedLevel;
bool requestedRawLevel;

//define hardware
//interrup pins
const byte interruptPin=12;

//steps per Rev is a constant for the motor; steps per level indexes the 0-100 scale to this scale.
const int stepsPerRevolution = 2048;
Stepper myStepper(stepsPerRevolution, 16,4,5,0);

void IRAM_ATTR physRezeroPress(){
  if(millis()-lastPhysInput > 500){
    rezeroRequested=true;
    lastPhysInput=millis();
  }
  //pass if it hasn't been half a second to debounce switch
}

void setup() {
  // Steup Serial Connection
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');

  //Set pinmode to interrupt pin and attach interrupt
  pinMode(interruptPin, INPUT_PULLUP);
  //rezero:
  attachInterrupt(digitalPinToInterrupt(interruptPin), physRezeroPress, FALLING);

  //setup wifi connection
  WiFi.begin(ssid, password);
  Serial.print ("Connecting to ");
  Serial.print(ssid); Serial.println(" ...");
  while(WiFi.status() != WL_CONNECTED) { //Wait for conneciton
    delay(1000);
  }
  Serial.println("\n"); //serial log connection info
  Serial.println("Connection established!");
  Serial.print("IP Address:\t");
  Serial.println(WiFi.localIP());

// Connect to mqtt
  mqttclient.setServer(mqttServer,mqttPort);
  mqttclient.setCallback(mqttRecieved);
  Serial.println("Connecting to MQTT");
  if(mqttclient.connect(blindid,mqttUser,mqttPassword)){
    Serial.println("Connected to MQTT");
  } else {
    Serial.print("Failed MQTT connection with state: ");
    Serial.println(mqttclient.state());
    delay(2000);
       ESP.restart();
  }

  //define MQTT topics
  strcpy(statusTopic,blindid);
  Serial.println("statusTopic:");
  Serial.println(statusTopic);
  strcpy(statusTopic+strlen(blindid),"/status");
  strcpy(connTopic,blindid);
  strcpy(connTopic+strlen(blindid),"/conn");
  strcpy(controlTopic,blindid);
  strcpy(controlTopic+strlen(blindid),"/control");
  strcpy(levelTopic,blindid);
  strcpy(levelTopic+strlen(blindid),"/level");
  strcpy(rawlevelTopic,blindid);
  strcpy(rawlevelTopic+strlen(blindid),"/rawlevel");
  mqttclient.publish(statusTopic,"Connected");
  mqttclient.publish(connTopic,"True");  
  mqttclient.subscribe(controlTopic);
//connection should now be complete
//set stepper for 10rpm 
  myStepper.setSpeed(maxspeed);
  //allocate EEPROM bytes for position and complete or not.
  EEPROM.begin(5);


  EEPROM.get(0,eEPROM_FinishedMove);
  if (eEPROM_FinishedMove)
  {
    ///true=nothing, all is good
    Serial.println("EEprom says true at this point we should also update currstep from eeprom");/// !!!!!!!!!!!!!!!! Work needed
    ///update all set points levels etc based off this data
    Serial.println("what's in EEPROM1-5?");
    EEPROM.get(1,currstep);
    Serial.println(currstep);
    if(currstep>=0 && currstep<=stepstolevel*maxRawLevel){
      Serial.println("took currstep from EEPROM");

      /// Calculate all initial values off of currstep loaded
      currRawLevel=currstep/stepstolevel;
      currCalLevel=machinetoUser(currRawLevel);
      /// set targets to equal
      targetstep=currstep;
      setCalLevel=currCalLevel;
      setRawLevel=currRawLevel;
      
    }
    else{
      currstep = currRawLevel*stepstolevel;//default currstep incase nothing loaded from EEPPROM
      targetstep=currstep;
      rezeroRequested=true; // since nothing good came from EEprom we still need to rezero
      Serial.println("defualtedcurrstep");
      Serial.println(currstep);
    }
  }
  else if (eEPROM_FinishedMove==false)
  {

    Serial.println("Pre-Rezero");
    Serial.println(currstep);
    Serial.println(targetstep);
    Serial.println("EEprom says false");// if EEprom is 0 is false then last move did not complete and last state can't be recovered.
    rezeroRequested=true;
  } 
}

void loop() {
  if(mqttclient.loop())
  { //returns true if still connected if not start troubleshooting and potentially restart.
    if(rezeroRequested){
      //call rezore routine here then clear flag
      Serial.println("should now run rezero if implemented");
      rezeroRequested=false;
    }
    if(currstep!=targetstep){ //steps differ so need to set EEPROM flag to moving
      EEPROM.get(0,eEPROM_FinishedMove);
      if (eEPROM_FinishedMove){ //Right now finished move is true need to set to false Else already false no need to rewrite
        eEPROM_FinishedMove=false;
        EEPROM.put(0,eEPROM_FinishedMove);
        EEPROM.commit();
      }
      currstep=stepperToTarget(currstep,targetstep);
      currRawLevel=currstep/stepstolevel;
      currCalLevel=machinetoUser(currRawLevel);
      char currstr[4];
      sprintf(currstr,"%d",currCalLevel);
      mqttclient.publish(levelTopic, currstr);

      if (currstep==targetstep){
        eEPROM_FinishedMove=true;
        EEPROM.put(0,eEPROM_FinishedMove);
        EEPROM.put(1,currstep);
        EEPROM.commit();
        digitalWrite(16, LOW);
        digitalWrite(5, LOW);
        digitalWrite(4, LOW);
        digitalWrite(0, LOW);
        mqttclient.publish(statusTopic,"CompletedMove");
        delay(50);
      }
    }
    if (millis()-lastStatusBroadcast>300000){
      lastStatusBroadcast=millis();
      char currstr[4];
      sprintf(currstr,"%d",currCalLevel);
      mqttclient.publish(levelTopic, currstr);
    }
    setRawLevel=usertoMachine(setCalLevel);
    targetstep=setRawLevel*stepstolevel;
    
  }
  else/// everything down here is connection failed loop logic. Should immediately restart on wifi fail and otherwise take 5 attempts on mqtt restart.
      //all restarts will wait for finished move to be true.
  {
    mqttconnectattempts+=1;
    if (WiFi.status() != WL_CONNECTED){
      if(finishedMove)
      {
        ESP.restart();
      }
    } //Go straight to restart if Wifi is down
    if (mqttconnectattempts<=5) //less than five attempts keep retrying connection
    {
      delay(500);
      if (mqttclient.connect(blindid,mqttUser,mqttPassword))
      {
        mqttconnectattempts=0;//if we reconnect set attempts back to 1
      }
      else {
        mqttconnectattempts+=1; // increment counter otherwise
      }
    }
    else
    {
      if(finishedMove)
      {
        ESP.restart();/// after five reconnect attemps restart ESP
      }
      
    }
  }
}


void mqttRecieved(char* topic, byte* payload, unsigned int length){
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);

//Convert bytes to int
  char buffer[128];
  memcpy(buffer, payload, length);
  buffer[length]= '\0';

  char *end = nullptr;
  long value = strtol(buffer, &end, 10);
// log ints to serial log
  Serial.print("Message: ");
  Serial.println(value);

  if(value == 259){ //remote system requested the machines idea of current user level
    char currstr[4];
    sprintf(currstr,"%d",currCalLevel);
    mqttclient.publish(levelTopic, currstr);
  }
  else if (value == 258) ////remote system requested the machines idea of Raw level
  {
    char currstr[4];
    sprintf(currstr,"%d",currRawLevel);
    mqttclient.publish(rawlevelTopic, currstr);
  }
  else if(value==257)/// Remote system initiated rezeroing
  {
    Serial.println("Starting Rezero Routine");
    mqttclient.publish(statusTopic,"Beginning Zero Process");
  }
  else if (value>=minCalLevel && value <=maxCalLevel){ //Valid user set values.
    setCalLevel=value;
  }
  else{
    Serial.println("Invalid Command Provided");
    mqttclient.publish(statusTopic,"Invalid Command Provided");
  }
}


int usertoMachine(int ul){/// converts the user level (calibrated level) to raw machine level
  return ul+10;
}
int machinetoUser(int ml){/// converts the macine level (raw level) to cal User level
  return ml-10;
}



long stepperToTarget(long currentstep, long targetsteps){
  /// takes in currsteps and a traget steps moves to target.
    long stepstogo=targetsteps-currentstep;
    Serial.println("stepstogo");
    Serial.println(stepstogo);
    Serial.println("labs");
    long labstogo = labs(stepstogo);
    Serial.println(labstogo);
    if (labs(stepstogo)<=512){
      //Record moving in the flash and update moving var
      myStepper.step(stepstogo);
      //record no longer moving in the flash and update moving var and final position var
      currentstep=currentstep+stepstogo;
    }
    else if (stepstogo>512)
    {
      //record moving in the flash and update moving var
      myStepper.step(512);
      //record still moving in the flash and update moving var and final position var
      currentstep+=512;
    }
    else if (stepstogo<-512)
    {
      //record moving in the flash and update moving var
      myStepper.step(-512);
      //record still moving in the flash and update moving var and final position var
      currentstep-=512;
    }
  ESP.wdtFeed();  
  mqttclient.loop();
  return currentstep;
}

