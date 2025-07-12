#include <Arduino.h>

// put function declarations here:
int myFunction(int, int);

int IN1 = 2;
int IN2 = 4;
int IN3 = 14;
int IN4 = 15;


void setup() {
  // put your setup code here, to run once:
  int LEDPIN = 2;
  pinMode(LEDPIN, OUTPUT);
  //IN1 - IO12
  pinMode(IN1, OUTPUT);
  //IN2 - IO14 
  pinMode(IN2, OUTPUT);
  //IN3 - IO15
  pinMode(IN3, OUTPUT);
  //IN4 - IO36
  pinMode(IN4, OUTPUT);

  // turn off all relay pins initially - inversed logic
  digitalWrite(IN1, HIGH); // turn IN1 off
  digitalWrite(IN2, HIGH); // turn IN2 off
  digitalWrite(IN3, HIGH); // turn IN3 off
  digitalWrite(IN4, HIGH); // turn IN4 off
  
}

void loop() {
  // put your main code here, to run repeatedly:
  //serial
  Serial.begin(9600);
  Serial.println("Hello, ESP32 ETH0!");
  delay(1000);  
  //switch relay pins one by one
  digitalWrite(IN1, HIGH); // turn IN1 on
  Serial.println("IN1 is ON");
  delay(500);             // wait for a second
  digitalWrite(IN2, HIGH);  // turn IN2 on
  Serial.println("IN2 is ON");
  delay(500);             // wait for a second
  digitalWrite(IN3, HIGH);  // turn IN3 on
  Serial.println("IN3 is ON");
  delay(500);             // wait for a second
  digitalWrite(IN4, HIGH);  // turn IN4 on
  Serial.println("IN4 is ON");
  delay(500);             // wait for a second
  digitalWrite(IN1, LOW);  // turn IN1 off
  Serial.println("IN1 is OFF");
  digitalWrite(IN2, LOW);  // turn IN2 off
  Serial.println("IN2 is OFF");
  digitalWrite(IN3, LOW);  // turn IN3 off
  Serial.println("IN3 is OFF");
  digitalWrite(IN4, LOW);  // turn IN4 off
  Serial.println("IN4 is OFF");
  delay(1000);            // wait for a second before repeating
  Serial.println("Repeating the cycle...");
 
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}