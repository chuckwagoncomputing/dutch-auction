#define MAX_BIDDERS 40

// Servo milliseconds per movement and deg/2 of movement
#define COUNTDOWN_TIME 700
#define COUNTDOWN_SPEED 1
#define DIAL_MIN 35
#define DIAL_MAX 120

#define SCAN_INTERVAL 5000
#define STATUS_INTERVAL 1000

#include "Arduino.h"
#include "NowMesh.h"
#include "Servo.h"
#include "SPI.h"
#include "NeoPixelBus.h"

NowMesh mesh_node;

Servo dial;
int dial_position = DIAL_MIN;

RgbColor on(255, 255, 255);
RgbColor off(0, 0, 0);
NeoPixelBus<NeoRgbFeature, NeoEsp8266Uart800KbpsMethod> lights(MAX_BIDDERS);
volatile bool should_update_lights = false;

struct auction_status {
 bool online = false;
 bool open = false;
 bool won = false;
 bool i_won = false;
} status;

// The ID of the bidder who won the auction
int winner_id;

// For button debouncing
volatile long last_edge_time = 0;

os_timer_t scan_timer;
volatile bool should_scan = true;
os_timer_t dial_timer;
volatile bool should_dial = false;
os_timer_t status_timer;
volatile bool should_status = false;

void scanTimerCallback(void* arg) {
 should_scan = true;
}

void dialTimerCallback(void* arg) {
 should_dial = true;
}

void statusTimerCallback(void* arg) {
 should_status = true;
}

// turn light number <i> on
void turnOn(int i) {
 lights.SetPixelColor(i - 1, on);
 should_update_lights = true;
}

// turn light number <i> off
void turnOff(int i) {
 lights.SetPixelColor(i - 1, off);
 should_update_lights = true;
}

// turn all lights off
void aOff() {
 lights.ClearTo(off);
 lights.Show();
}

void manageRequest(String request, bool self_is_target, uint8_t* originator) {
 // Get the bidder id out of the message.
 //  It won't always be there, but this will fail safely.
 int bidder_id = request.substring(3).toInt();
 // Someone is bidding.
 if (request.startsWith("bd?") && status.open) {
  status.won = true;
  status.open = false;
  String bid = "bd!" + String(bidder_id, DEC);
  mesh_node.send(bid);
  digitalWrite(5, LOW);
  aOff();
  turnOn(bidder_id);
  winner_id = bidder_id;
 }
 // Someone is requesting auction status.
 else if (request.startsWith("ao?")) {
  // If the auction is open, let them know with their ID.
  if (status.open) {
   String ack = "ao!" + String(bidder_id, DEC);
   mesh_node.send(ack);
  }
  // If the auction has been won, reply with the ID of the winner
  else if (status.won) {
   String ack = "aw!" + String(winner_id, DEC);
   mesh_node.send(ack);
  }
  // else reply that the auction is closed, with their ID.
  else {
   String ack = "ac!" + String(bidder_id, DEC);
   mesh_node.send(ack);
  }
 }
}

void sendCallback(int status) {
 Serial.println("Send status: " + String(status, DEC));
}

void buttonHandler() {
 // A little bit of a debouncing algorithm
 uint32 current_time = micros();
 if ((current_time - last_edge_time) > 250000) {
  // If the auction is closed, reset the dial and reopen the auction
  if (!status.open) {
   mesh_node.send("rs!");
   status.won = false;
   status.open = true;
   aOff();
   dial_position = DIAL_MIN;
   digitalWrite(5, HIGH);
  }
 }
}

void setup() {
 // Serial for debug messages
 Serial.begin(115200);
 // Initialize and set up the mesh network
 mesh_node.begin();
 mesh_node.setReceiveCallback(manageRequest);
 mesh_node.setSendCallback(sendCallback);
 // Initialize lights and make sure all are off.
 lights.Begin();
 lights.Show();
 // Tell servo library that our dial is on pin 13
 dial.attach(13);
 // Set LED pin as output
 pinMode(5, OUTPUT);
 // Set up button input and interrupt
 pinMode(4, INPUT_PULLUP);
 attachInterrupt(digitalPinToInterrupt(4), buttonHandler, FALLING);
 // Set up and arm timers.
 os_timer_setfn(&scan_timer, scanTimerCallback, NULL);
 os_timer_arm(&scan_timer, SCAN_INTERVAL, true);
 os_timer_setfn(&dial_timer, dialTimerCallback, NULL);
 os_timer_arm(&dial_timer, COUNTDOWN_TIME, true);
 os_timer_setfn(&status_timer, statusTimerCallback, NULL);
 os_timer_arm(&status_timer, COUNTDOWN_TIME, true);
}

void loop() {
 // should_ booleans are set to true in their respective timer callback.
 if (should_scan) {
  mesh_node.scanForPeers();
  should_scan = false;
 }
 if (should_dial) {
  if (status.open) {
   dial_position = dial_position + COUNTDOWN_SPEED;
   // If dial has reached the end of its travel
   if (dial_position >= DIAL_MAX) {
    // Notify that the auction has closed.
    mesh_node.send("ac!");
    status.open = false;
    digitalWrite(5, LOW);
    // Clamp to DIAL_MAX so we don't try to write a value over DIAL_MAX.
    dial_position = DIAL_MAX;
   }
  }
  dial.write(dial_position);
  should_dial = false;
 }
 if (should_update_lights) {
  lights.Dirty();
  lights.Show();
  should_update_lights = false;
 }
 if (should_status) {
  should_update_lights = true;
  // If the auction is open, let them know with their ID.
  if (status.open) {
   String ack = "ao!";
   mesh_node.send(ack);
  }
  // If the auction has been won, reply with the ID of the winner
  else if (status.won) {
   String ack = "aw!" + String(winner_id, DEC);
   mesh_node.send(ack);
  }
  // else reply that the auction is closed, with their ID.
  else {
   String ack = "ac!";
   mesh_node.send(ack);
  }
  should_status = false;  
 }
}
