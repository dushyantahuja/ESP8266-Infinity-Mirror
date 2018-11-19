#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ESP8266HTTPUpdateServer.h>
//#include "FS.h"

#define FASTLED_ESP8266_RAW_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0
#include "FastLED.h"
#include "EEPROM.h"

#define NUM_LEDS 60
#define DATA_PIN 4
#define UPDATES_PER_SECOND 50
#define MQTT_MAX_PACKET_SIZE 256

#include "palette.h"
const TProgmemRGBGradientPalettePtr gGradientPalettes[] = {
  es_emerald_dragon_08_gp,
  Magenta_Evening_gp,
  blues_gp,
  nsa_gp
};
// Count of how many cpt-city gradients are defined:
const uint8_t gGradientPaletteCount =
  sizeof( gGradientPalettes) / sizeof( TProgmemRGBGradientPalettePtr );
// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 3;

CRGBPalette16 gCurrentPalette( gGradientPalettes[gCurrentPaletteNumber]);

CRGB leds[NUM_LEDS],minutes,hours,seconds,l,bg,lines;
int light_low, light_high;
boolean missed=0, ledState = 1, lastsec=1, multieffects = 0;
byte lastsecond, rain;
void callback(const MQTT::Publish& pub);

WiFiClient espClient;
IPAddress server(192, 168, 1, 232);
PubSubClient client(espClient, server);

//long lastReconnectAttempt = 0;
long lastMsg = 0;
char msg[50];
int value = 0, counter = 0;

// NTP Servers:
static const char ntpServerName[] =  "time.google.com"; //,"time-b.timefreq.bldrdoc.gov", "time-a.timefreq.bldrdoc.gov"};
float timeZone = 0;

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

time_t getNtpTime();
void sendNTPpacket(IPAddress &address);

void setup() {
    // put your setup code here, to run once:
    Serial.begin(74880);
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    fill_palette(leds, NUM_LEDS, 0, 0, gCurrentPalette, 10, LINEARBLEND);
    FastLED.setBrightness(constrain(light_high,10,255));
    //reverseLEDs();
    FastLED.show();
    //delay(1000);
    WiFi.setAutoConnect ( true );
    WiFiManager wifiManager;
    //wifiManager.resetSettings();
    //wifiManager.setConfigPortalTimeout(180);
    wifiManager.setTimeout(180);
    //wifiManager.setConnectTimeout(120);
    if(!wifiManager.autoConnect("InfinityClock")) {
      delay(3000);
      ESP.reset();
      delay(5000);
      }

    MDNS.begin("InfinityClock");
    httpUpdater.setup(&httpServer);
    httpServer.on("/time", handleRoot);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();

    MDNS.addService("http", "tcp", 80);

    Udp.begin(localPort);
    client.set_callback(callback);
    reconnect();
    setSyncProvider(getNtpTime);
    setSyncInterval(6000);

    while (year() <= 2000) {
      setTime((time_t)getNtpTime());
      delay(100);
      if (WiFi.status() != WL_CONNECTED){
        WiFi.reconnect();
        if(counter++ > 10000 && !WiFi.isConnected()) ESP.reset();  // Restart the ESP after x attempts
        else if(WiFi.isConnected()) counter = 0;                // Reset counter if connected
      }
    }

    EEPROM.begin(512);

    if (EEPROM.read(101) != 1){               // Check if colours have been set or not

      seconds.r = 0;
      seconds.g = 0;
      seconds.b = 0;
      minutes.r = 127;
      minutes.g = 25;
      minutes.b = 10;
      hours.r = 0;
      hours.g = 255;
      hours.b = 255;
      bg.r = 0;
      bg.g = 0;
      bg.b = 5;
      light_low = 0;
      light_high = 120;
      rain = 30;

      EEPROM.write(0,0);                   // Seconds Colour
      EEPROM.write(1,0);
      EEPROM.write(2,0);
      EEPROM.write(3,127);                   // Minutes Colour
      EEPROM.write(4,25);
      EEPROM.write(5,10);
      EEPROM.write(6,0);                     // Hours Colour
      EEPROM.write(7,255);
      EEPROM.write(8,255);
      EEPROM.write(9,0);                     // BG Colour
      EEPROM.write(10,0);
      EEPROM.write(11,5);
      EEPROM.write(12, 0);                   // Light sensitivity - low
      EEPROM.write(13, 100);                  // Light sensitivity - high
      EEPROM.write(14, 30);                  // Minutes for each rainbow
      EEPROM.write(101,1);
      EEPROM.commit();
    }
    // Else read the parameters from the EEPROM
    else {
      seconds.r = EEPROM.read(0);
      seconds.g = EEPROM.read(1);
      seconds.b = EEPROM.read(2);
      minutes.r = EEPROM.read(3);
      minutes.g = EEPROM.read(4);
      minutes.b = EEPROM.read(5);
      hours.r = EEPROM.read(6);
      hours.g = EEPROM.read(7);
      hours.b = EEPROM.read(8);
      bg.r = EEPROM.read(9);
      bg.g = EEPROM.read(10);
      bg.b = EEPROM.read(11);
      light_low = EEPROM.read(12);
      light_high = EEPROM.read(13);
      rain = EEPROM.read(14);
    }
    fill_solid(leds, NUM_LEDS, bg);
    //reverseLEDs();
    FastLED.show();
    gCurrentPaletteNumber = 2;
    gCurrentPalette = gGradientPalettes[gCurrentPaletteNumber];
}

void loop() {
    /*if (WiFi.status() != WL_CONNECTED)
      setup_wifi();*/
    showTime(int(hour()),int(minute()),int(second()));
    /*EVERY_N_MILLISECONDS(500){
      if(lastsec){
          l=leds[int(second())];
          leds[int(second())] = seconds;
          lastsecond = int(second());
          lastsec = 0;
          // Serial.println("ON");
        } else {
          leds[lastsecond] = l;
          // Serial.println("OFF");
          lastsec = 1;
        }
    }*/
    FastLED.show();
    if (WiFi.status() != WL_CONNECTED){
      WiFi.reconnect();
      if(counter++ > 30000 && !WiFi.isConnected()) ESP.reset();  // Restart the ESP after 150 attempts
      else if(WiFi.isConnected()) counter = 0;                // Reset counter if connected
    }
    httpServer.handleClient();
    if (!client.connected()) reconnect(); else client.loop();
    FastLED.delay(1000 / UPDATES_PER_SECOND);
    yield();
}

void showTime(int hr, int mn, int sec) {
  if(sec==0) fill_solid(leds, NUM_LEDS, bg);
  if(( mn % rain == 0 && sec == 0)){
       effects();
    }
  //fill_palette( leds, mn, 0, 6, gCurrentPalette,light_high,LINEARBLEND);
  colorwaves( leds, mn, gCurrentPalette);
  //leds[mn]= minutes;
  /*for(byte i=0; i<=mn;i++){
      leds[i] = minutes; //ColorFromPalette( currentPalette, i*6);
    }*/
  leds[hr%12*5]=hours;
  leds[hr%12*5+1]=hours;
  if(hr%12*5-1 > 0)
    leds[hr%12*5-1]=hours;
  else leds[59]=hours; for(byte i = 0; i<60; i+=5){
    leds[i]= CRGB(20,30,0); //CRGB(64,64,50);
  }
  if(hr < 7 || hr >= 22)
    FastLED.setBrightness(constrain(0,0,100)); // Set brightness to light_low during night - cools down LEDs and power supplies.
  else
    FastLED.setBrightness(constrain(light_high,10,255));
}

void callback(const MQTT::Publish& pub) {
 Serial.print(pub.topic());
 Serial.print(" => ");
 Serial.println(pub.payload_string());

 String payload = pub.payload_string();
  if(String(pub.topic()) == "infinity/brightness"){
    //int c1 = payload.indexOf(',');
    int h = payload.toInt();
    //int s = payload.substring(c1+1).toInt();
    set_light(h,l);
  }

  if(String(pub.topic()) == "infinity/hour"){
    int c1 = payload.indexOf(',');
    int c2 = payload.indexOf(',',c1+1);
    int h = map(payload.toInt(),0,360,0,255);
    int s = map(payload.substring(c1+1,c2).toInt(),0,100,0,255);
    int v = map(payload.substring(c2+1).toInt(),0,100,0,255);
    set_hour_hsv(h,s,v);
 }
 if(String(pub.topic()) == "infinity/minute"){
    gCurrentPaletteNumber++;
    if (gCurrentPaletteNumber>=gGradientPaletteCount) gCurrentPaletteNumber=0;
    gCurrentPalette = gGradientPalettes[gCurrentPaletteNumber];
    /*int c1 = payload.indexOf(',');
    int c2 = payload.indexOf(',',c1+1);
    int h = map(payload.toInt(),0,360,0,255);
    int s = map(payload.substring(c1+1,c2).toInt(),0,100,0,255);
    int v = map(payload.substring(c2+1).toInt(),0,100,0,255);
    set_minute_hsv(h,s,v);*/
 }
 if(String(pub.topic()) == "infinity/second"){
    int c1 = payload.indexOf(',');
    int c2 = payload.indexOf(',',c1+1);
    int h = map(payload.toInt(),0,360,0,255);
    int s = map(payload.substring(c1+1,c2).toInt(),0,100,0,255);
    int v = map(payload.substring(c2+1).toInt(),0,100,0,255);
    set_second_hsv(h,s,v);
 }
 if(String(pub.topic()) == "infinity/bg"){
    int c1 = payload.indexOf(',');
    int c2 = payload.indexOf(',',c1+1);
    int h = map(payload.toInt(),0,360,0,255);
    int s = map(payload.substring(c1+1,c2).toInt(),0,100,0,255);
    int v = map(payload.substring(c2+1).toInt(),0,100,0,255);
    set_bg_hsv(h,s,v);
 }
 if(String(pub.topic()) == "infinity/effects"){
    effects();
 }
 if(String(pub.topic()) == "infinity/clockstatus"){
    clockstatus();
 }
 if(String(pub.topic()) == "infinity/reset"){
   client.publish("infinity/status","Restarting");
   ESP.reset();
 }
}

void effects(){
  for( int j = 0; j< 300; j++){
    fadeToBlackBy( leds, NUM_LEDS, 20);
    byte dothue = 0;
    for( int i = 0; i < 8; i++) {
      leds[beatsin16(i+7,0,NUM_LEDS)] |= CHSV(dothue, 200, 255);
      dothue += 32;
    }
    //reverseLEDs();
    FastLED.show();
    FastLED.delay(1000/UPDATES_PER_SECOND);
   }
  fill_solid(leds, NUM_LEDS, bg);
  client.publish("infinity/status","RAINBOW");
  lastsec = 1;
}


void set_hour_hsv(int h, int s, int v){
  CHSV temp;
  temp.h = h;
  temp.s = s;
  temp.v = v;
  hsv2rgb_rainbow(temp,hours);
  EEPROM.write(6,hours.r);
  EEPROM.write(7,hours.g);
  EEPROM.write(8,hours.b);
  EEPROM.commit();
  client.publish("infinity/status","HOUR COLOUR SET");
}

void set_minute_hsv(int h, int s, int v){
  CHSV temp;
  temp.h = h;
  temp.s = s;
  temp.v = v;
  hsv2rgb_rainbow(temp,minutes);
  EEPROM.write(3,minutes.r);
  EEPROM.write(4,minutes.g);
  EEPROM.write(5,minutes.b);
  EEPROM.commit();
  client.publish("infinity/status","MINUTE COLOUR SET");
}

void set_second_hsv(int h, int s, int v){
  CHSV temp;
  temp.h = h;
  temp.s = s;
  temp.v = v;
  hsv2rgb_rainbow(temp,seconds);
  EEPROM.write(0,seconds.r);
  EEPROM.write(1,seconds.g);
  EEPROM.write(2,seconds.b);
  EEPROM.commit();
  client.publish("infinity/status","SECOND COLOUR SET");
}

void set_bg_hsv(int h, int s, int v){
  CHSV temp;
  temp.h = h;
  temp.s = s;
  temp.v = v;
  hsv2rgb_rainbow(temp,bg);
  EEPROM.write(9,bg.r);
  EEPROM.write(10,bg.g);
  EEPROM.write(11,bg.b);
  EEPROM.commit();
  client.publish("infinity/status","BG COLOUR SET");
  fill_solid(leds, NUM_LEDS, bg);
}

void clockstatus(){
  String message = "Status: \n";
  message+= "BG: " + String(bg.r) + "-" + String(bg.g) + "-" + String(bg.b) +"\n";
  message+= "SEC: " + String(seconds.r) + "-" + String(seconds.g) + "-" + String(seconds.b) +"\n";
  message+= "MINUTE: " + String(minutes.r) + "-" + String(minutes.g) + "-" + String(minutes.b) +"\n";
  message+= "HOUR: " + String(hours.r) + "-" + String(hours.g) + "-" + String(hours.b) +"\n";
  message+= "Time: " + String(hour()) + ":" + String(minute())+ ":" + String(second())+"\n";
  client.publish("infinity/status",message);
  /*client.publish("infinity/status","BG: " + String(bg.r) + "-" + String(bg.g) + "-" + String(bg.b));
  client.publish("infinity/status","SEC: " + String(seconds.r) + "-" + String(seconds.g) + "-" + String(seconds.b));
  client.publish("infinity/status","MINUTE: " + String(minutes.r) + "-" + String(minutes.g) + "-" + String(minutes.b));
  client.publish("infinity/status","HOUR: " + String(hours.r) + "-" + String(hours.g) + "-" + String(hours.b));
  client.publish("infinity/status","Time: " + String(hour()) + ":" + String(minute())+ ":" + String(second()));*/
}


void set_light(int low, int high){
  light_low = low;
  light_high = high;
  EEPROM.write(12,light_low);
  EEPROM.write(13,light_high);
  client.publish("infinity/status","LIGHT SET");
}

// FastLED colorwaves

void colorwaves( CRGB* ledarray, uint16_t numleds, CRGBPalette16& palette)
{
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;

  //uint8_t sat8 = beatsin88( 87, 220, 250);
  uint8_t brightdepth = beatsin88( 341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);

  uint16_t hue16 = sHue16;//gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 300, 1500);

  uint16_t ms = millis();
  uint16_t deltams = ms - sLastMillis ;
  sLastMillis  = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88( 400, 5,9);
  uint16_t brightnesstheta16 = sPseudotime;

  for( uint16_t i = 0 ; i < numleds; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;
    uint16_t h16_128 = hue16 >> 7;
    if( h16_128 & 0x100) {
      hue8 = 255 - (h16_128 >> 1);
    } else {
      hue8 = h16_128 >> 1;
    }

    brightnesstheta16  += brightnessthetainc16;
    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    uint8_t index = hue8;
    //index = triwave8( index);
    index = scale8( index, 240);

    CRGB newcolor = ColorFromPalette( palette, index, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (numleds-1) - pixelnumber;

    nblend( ledarray[pixelnumber], newcolor, 128);
  }
}

boolean reconnect() {
    if (client.connect("infinity")) {
      // Once connected, publish an announcement...
      client.publish("infinity/status", "Infinity Mirror Alive - topics infinity/brighness,infinity/hour,infinity/minute,infinity/second,infinity/bg,infinity/effects subscribed");
      client.publish("infinity/status",ESP.getResetReason());
      // ... and resubscribe
      client.subscribe("infinity/hour");
      client.subscribe("infinity/minute");
      client.subscribe("infinity/second");
      client.subscribe("infinity/bg");
      client.subscribe("infinity/effects");
      client.subscribe("infinity/brightness");
      client.subscribe("infinity/clockstatus");
      client.subscribe("infinity/reset");
    }
  return client.connected();
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime() {
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1600) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      // client.publish("infinity/status",secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;

    }
  }
  client.publish("infinity/status","NTP Time Updated");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void handleRoot() {
  //digitalWrite(led, 1);
  String message = "Time: ";
  message+= String(hour()) + ":" + String(minute())+ ":" + String(second());
  httpServer.send(200, "text/plain", message);
  //digitalWrite(led, 0);
}

void handleNotFound(){
  //digitalWrite(led, 1);
  String message = "Time: ";
  message+= String(hour()) + ":" + String(minute())+ ":" + String(second());
  /* message += "URI: ";
  message += httpServer.uri();
  message += "\nMethod: ";
  message += (httpServer.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += httpServer.args();
  message += "\n";
  for (uint8_t i=0; i<httpServer.args(); i++){
    message += " " + httpServer.argName(i) + ": " + httpServer.arg(i) + "\n";
  }*/
  httpServer.send(404, "text/plain", message);
  //digitalWrite(led, 0);
}

// ----------------- Code to create a failsafe update

/*void updateFailSafe()
{
  SPIFFS.begin();
  Dir dir = SPIFFS.openDir("/");
  File file = SPIFFS.open("/blinkESP.bin", "r");

  uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!Update.begin(maxSketchSpace, U_FLASH)) { //start with max available size
    Update.printError(Serial);
    Serial.println("ERROR");
  }

  while (file.available()) {
    uint8_t ibuffer[128];
    file.read((uint8_t *)ibuffer, 128);
    Serial.println((char *)ibuffer);
    Update.write(ibuffer, sizeof(ibuffer));
  }

  Serial.print(Update.end(true));
  file.close();
  delay(5000);
  ESP.restart();
}*/
