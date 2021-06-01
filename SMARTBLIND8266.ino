#include <ESP8266WiFi.h>
#include <Stepper.h>
#define MQTT_SOCKET_TIMEOUT 1
#include <PubSubClient.h>
#include "const_Credentials.h"

WiFiClient espClient;
PubSubClient mqttclient(espClient);
char statusTopic[32];
char connTopic[32];
char controlTopic[32];
char levelTopic[32];
char rawlevelTopic[32];
int mqttconnectattempts=0;
int wificonnectattempts=0;
int setlevel;
int setuserlevel;
int currlevel;
int userlevel;
const int maxspeed = 10; //rpm
const int maxlevel = 120;
const int minlevel = 0;
const int maxuserlevel = 100;
const int minuserlevel = 0;
const int resetTrigger = 257;
const int requestStatus = 258;
const int requestRawStatus = 259;
long currstep;
long targetstep;
const int stepstolevel=2000;
bool finishedMove=true;

const int stepsPerRevolution = 2048;
Stepper myStepper(stepsPerRevolution, 16,4,5,0);

void setup() {
  // Steup Serial Connection
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');

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
    Serial.print("Failed MQTT connection with stat: ");
    Serial.println(mqttclient.state());
    delay(2000);
       ESP.restart();
  }
  Serial.println("startdelay");
  delay(2000);
  Serial.println("FinishDelay");

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
  userlevel=0;
  currlevel = userlevel+10; // value 
  currstep = currlevel*stepstolevel;
}

void loop() {
  if(mqttclient.loop())
  { //returns true if still connected if not start troubleshooting and potentially restart.
    if(setuserlevel!=userlevel)
    {
      setlevel=usertoMachine(setuserlevel);
      Serial.println("setlevelis");
      Serial.println(setlevel);
      long setstep=setlevel*stepstolevel;
      Serial.println("targetstepis");
      Serial.println(setstep);
      Serial.println("pre run currstepis");
      Serial.println(currstep);
      currstep=stepperToTarget(currstep, setstep);
      Serial.println("post run currstepis");
      Serial.println(currstep);
      currlevel=currstep/stepstolevel;
      Serial.println("currlevelis");
      Serial.println(currlevel);
      userlevel=currlevel-10;
      Serial.println("userlevelis");
      Serial.println(userlevel);
      Serial.println("setuserlevelis");
      Serial.println(setuserlevel);
    }
    else
    {
      mqttclient.publish(statusTopic,"AllGood");
      delay(500);
    }
  }
  else
  {
    mqttconnectattempts+=1;
    if (WiFi.status() != WL_CONNECTED){if(finishedMove){ESP.restart();}} //Go straight to restart if Wifi is down
    if (mqttconnectattempts<=5) //less than five attempts keep retrying connection
    {
      delay(500);
      if (mqttclient.connect(blindid,mqttUser,mqttPassword))
      {
        mqttconnectattempts=0;
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
    sprintf(currstr,"%d",userlevel);
    mqttclient.publish(levelTopic, currstr);
  }
  else if (value == 258) ////remote system requested the machines idea of Raw level
  {
    char currstr[4];
    sprintf(currstr,"%d",currlevel);
    mqttclient.publish(rawlevelTopic, currstr);
  }
  else if(value==257)/// Remot system initiated rezeroing
  {
    Serial.println("Starting Rezero Routine");
    mqttclient.publish(statusTopic,"Beginning Zero Process");
  }
  else if (value>=minuserlevel && value <=maxuserlevel){ //Valid user set values.
    setuserlevel=value;
  }
  else{
    Serial.println("Invalid Command Provided");
    mqttclient.publish(statusTopic,"Invalid Command Provided");
  }
}

int usertoMachine(int ul){
  return ul+10;
}

long stepperToTarget(long currstep, long targetsteps){
    long stepstogo=targetsteps-currstep;
    Serial.println("stepstogo");
    Serial.println(stepstogo);
    Serial.println("labs");
    long labstogo = labs(stepstogo);
    Serial.println(labstogo);
    if (labs(stepstogo)<512){
      //Record moving in the flash and update moving var
      myStepper.step(stepstogo);
      //record no longer moving in the flash and update moving var and final position var
      currstep=currstep+stepstogo;
      digitalWrite(16, LOW);
      digitalWrite(5, LOW);
      digitalWrite(4, LOW);
      digitalWrite(0, LOW);
    }
    else if (stepstogo>512)
    {
      //record moving in the flash and update moving var
      myStepper.step(512);
      //record still moving in the flash and update moving var and final position var
      currstep+=512;
    }
    else if (stepstogo<-512)
    {
      //record moving in the flash and update moving var
      myStepper.step(-512);
      //record still moving in the flash and update moving var and final position var
      currstep-=512;
    }
  ESP.wdtFeed();  
  mqttclient.loop();
  return currstep;
}