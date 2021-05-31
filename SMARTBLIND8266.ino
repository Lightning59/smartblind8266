#include <ESP8266WiFi.h>
#include <Stepper.h>
#include <PubSubClient.h>
#include "const_Credentials.h"

WiFiClient espClient;
PubSubClient mqttclient(espClient);
char statusTopic[32];
char connTopic[32];

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
  mqttclient.publish(statusTopic,"Connected");
  mqttclient.publish(connTopic,"True");  
  mqttclient.subscribe(blindid);
//connection should now be complete
//set stepper for 10rpm 
  myStepper.setSpeed(10);

}

void mqttRecieved(char* topic, byte* payload, unsigned int length){
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);

  Serial.print("Message: ");
  for (int i=0; i < length; i++){
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("--------------------------");
  
}

void loop() {
  // put your main code here, to run repeatedly:

  Serial.println("sclockwise");
  mqttclient.publish(statusTopic,"Clockwise");
  delay(1000);
  Serial.println("Stepping");
  mqttclient.publish(statusTopic,"Stepping");
  ESP.wdtFeed();
  myStepper.step(stepsPerRevolution/4);
  ESP.wdtFeed();
  myStepper.step(stepsPerRevolution/4);
  ESP.wdtFeed();
  myStepper.step(stepsPerRevolution/4);
  ESP.wdtFeed();
  myStepper.step(stepsPerRevolution/4);
  ESP.wdtFeed();
  delay(1000);
  ESP.wdtFeed(); 
  Serial.println("counterclockwise");
  mqttclient.publish(statusTopic,"Coutnerclockwise");
  myStepper.step(-stepsPerRevolution/4);
  ESP.wdtFeed();
  myStepper.step(-stepsPerRevolution/4);
  ESP.wdtFeed();
  myStepper.step(-stepsPerRevolution/4);
  ESP.wdtFeed();
  myStepper.step(-stepsPerRevolution/4);
  ESP.wdtFeed();
  delay(1000);
  

}
