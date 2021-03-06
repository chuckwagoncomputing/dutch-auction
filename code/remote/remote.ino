#define ID 24
#define LED_STEP_TIME 50
#define LED_STEP_BRIGHTNESS 50
#define LED_WON_TIME 500

#define SCAN_INTERVAL 5000

#include "Arduino.h"
#include "NowMesh.h"

NowMesh mesh_node;

// For button debouncing
volatile unsigned long last_edge_time = 0;

// Used to track led brightness for oscillation before going online
int16_t led_brightness = 1023;

os_timer_t scan_timer;
volatile bool should_scan = true;
os_timer_t led_timer;

struct auction_status {
 bool online = false;
 bool open = false;
 bool won = false;
 bool i_won = false;
} status;

void scanTimerCallback(void* arg) {
 should_scan = true;
}

void buttonHandler() {
 // A little bit of a debouncing algorithm
 uint32 current_time = micros();
 if ((current_time - last_edge_time) > 250000) {
  // Send bid
  String bid = "bd?" + String(ID, DEC);
  mesh_node.send(bid);
 }
 last_edge_time = current_time;
}

void announce() {
 // Ask if the auction is open
 String syn = "ao?" + String(ID, DEC);
 mesh_node.send(syn);
}

void ledTimerCallback(void* arg) {
 // If we haven't gotten a message from the controller with our ID,
 //  we don't know that the controller can route to us, so until that
 //  happens, oscillate the LED
 if (!status.online) {
  // led_brightness is a signed integer;
  //  analogWrite accepts values 0-1023, so we start at 1023 and go down to -1023,
  //  using abs(), so in that range the LED gets dim and then bright again.
  analogWrite(5, abs(led_brightness));
  led_brightness = led_brightness - LED_STEP_BRIGHTNESS;
  // When we get to -1023, reset to 1023.
  if (led_brightness <= -1023) {
   led_brightness = 1023;
  }
  // Announce ourselves again when we are within LED_STEP_BRIGHTESS of 0,
  //  i.e. once every every cycle.
  if (abs(led_brightness) <= LED_STEP_BRIGHTNESS / 2) {
   announce();
  }
 }
 // If this callback is running because we won, toggle the LED
 else if (status.i_won) {
  // digitalRead on a digital output pin returns the current state,
  //  so we can negate it to toggle the pin.
  digitalWrite(5, !digitalRead(5));
 }
}

void manageRequest(String request, bool self_is_target, uint8_t* originator) {
 Serial.println("request: " + request);
 // Get the bidder id out of the message.
 //  It won't always be there, but this will fail safely.
 int bidder_id = request.substring(3).toInt();
 // If any message is in reply to us, we are safely connected.
 if (bidder_id == ID) {
  status.online = true;
 }
 // Auction is open!
 if (request.startsWith("ao!")) {
  status.open = true;
 }
 // Auction is closed.
 if (request.startsWith("ac!")) {
  status.open = false;
 }
 // We won the auction and either us or someone else must have crashed,
 //  and requested auction status again.
 else if (request.startsWith("aw!") && bidder_id == ID) {
  // If someone else crashed, this stuff could have already been done, so check first.
  if (!status.i_won) {
   status.open = false;
   status.won = true;
   status.i_won = true;
   digitalWrite(5, HIGH);
   os_timer_disarm(&led_timer);
   os_timer_arm(&led_timer, LED_WON_TIME, true);
  }
 }
 // Someone else has won and we didn't get the acknowledgement, or we crashed.
 else if (request.startsWith("aw!")) {
  status.open = false;
  status.won = true;
  status.i_won = false;
 }
 // Our bid has been acknowledged, we won"
 else if (request.startsWith("bd!") && bidder_id == ID) {
  status.open = false;
  status.won = true;
  status.i_won = true;
  os_timer_disarm(&led_timer);
  os_timer_arm(&led_timer, LED_WON_TIME, true);
 }
 // Someone else's bid has been acknowledged.
 else if (request.startsWith("bd!") || request.startsWith("bd?")) {
  status.open = false;
  status.won = true;
  status.i_won = false;
 }
 // Reset and re-open auction.
 else if (request.startsWith("rs!")) {
  status.open = true;
  status.won = false;
  status.i_won = false;
  // Make sure we are online before disarming the timer so if we get a reset message
  //  before being acknowledged, we will still get acknowledged.
  if (status.online) {
   os_timer_disarm(&led_timer);
  }
 }
}

void sendCallback(int status) {
}

void setup() {
 // Serial for debug messages.
 Serial.begin(115200);
 // Initialize and set up the mesh network
 mesh_node.begin();
 mesh_node.setReceiveCallback(manageRequest);
 mesh_node.setSendCallback(sendCallback);
 // Announce our presence by asking for auction status.
 // We probably haven't connected by now but might as well try.
 announce();
 // Turn LED on
 pinMode(5, OUTPUT);
 digitalWrite(5, HIGH);
 // Set up button input pin and interrupt
 pinMode(4, INPUT_PULLUP);
 attachInterrupt(digitalPinToInterrupt(4), buttonHandler, FALLING);
 // Set up and arm timers.
 os_timer_setfn(&scan_timer, scanTimerCallback, NULL);
 os_timer_arm(&scan_timer, SCAN_INTERVAL, true);
 os_timer_setfn(&led_timer, ledTimerCallback, NULL);
 os_timer_arm(&led_timer, LED_STEP_TIME, true);
}

void loop() {
 // should_ booleans are set to true in their respective timer callback.
 if (should_scan) {
  mesh_node.scanForPeers();
  should_scan = false;
 }
 // If we can bid, turn the LED on
 if (status.online && status.open) {
  digitalWrite(5, HIGH);
 }
 // Else if we can't bid, turn the LED off
 // ...unless we won, in which case let the LED timer callback take care of it.
 else if (status.online && !status.open && !status.i_won) {
  digitalWrite(5, LOW);
 }
}
