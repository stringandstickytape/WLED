/*
   LIFX WLED user mod.  
   
   Based on LIFX bulb emulator by Kayno: https://github.com/kayno/arduinolifx

   And LifxZoneStrip by VinzzB: https://github.com/VinzzB/LifxZoneStrip

   Changes required to other files:
   ================================
   FX.CPP (add Usermod mode)
   FX.H (declare and reference Usermod mode)
   WLED00.INO (disable support for some other things to free up stack space, and #include "wled06_usermod.h")
   WLED01_EEPROM.INO (#define EEPSIZE 3000 to allow Lifx data to be stored)
   
   Notes:
   ======
   * LAN support only, cloud features will not work. 
   * Functions as a "guest" bulb - rediscovered each time the app starts, so does not appear in the app until a few seconds have passed.
   * Strip will be divided into Lifx zones automatically: 8 zones for fewer than 64 LEDs; 64 zones for 64 LEDs or more

   Setup:
   ======
   1) Build WLED with usermod and flash to device.
   2) Start LIFX Android app (iOS untested, deprecated Windows app does not work).  Wait a few seconds for it to detect guest lights.
   3) You should now be able to change your location to "WLED Location" using the drop-down in the top-left of the Android app.  In "WLED Group", you will find your light.
   4) Using the normal functionality of the Android app, change the light's location, group and name as you see fit.  These details will be saved to and restored from EEPROM.
   5) Restart the Android app. Confirm that (after a few seconds), the light is available in the Android app, in the specified location and group.
   6) In the Android app, turn the light on.  WLED responds by setting the "Usermod" effect, which prevents non-Lifx changes to the light.
   7) In the Android app, set a single colour, then adjust the brightness.  WLED sets the strip to the appropriate colour.
   8) In the Android app, confirm that themes set variable colours across the strip correctly.
   9) In the Android app, turn off the light.  WLED responds by setting the effect back to what it was before step 6) above, and setting the brightness to zero.

   Related:
   ========
   An upcoming release of MaxLifx will add compatibility for Lifx strips and thus allow Windows sound/screen capture effects to be sent to both Lifx and WLED devices.

 */

#define DEPLOY_LIFX_SUPPORT 

#ifndef DEPLOY_LIFX_SUPPORT 
//HSBK* memoryEater;
void userSetup() { /*memoryEater = (HSBK*)(malloc(1));*/} 
void userConnected() {}
void userLoop() {}
#endif

#ifdef DEPLOY_LIFX_SUPPORT
/*
   This file allows you to add own functionality to WLED more easily
   See: https://github.com/Aircoookie/WLED/wiki/Add-own-functionality
   EEPROM bytes 2750+ are reserved for your custom use case. (if you extend #define EEPSIZE in wled01_eeprom.h)
   bytes 2400+ are currently ununsed, but might be used for future wled features
   - this mod requires the value 3000 at present.
*/

//Use userVar0 and userVar1 (API calls &U0=,&U1=, uint16_t)


#include <WiFiUdp.h>
#include <SPI.h>

//#include <ESP8266WiFi.h>
#include <WiFi.h>

#include <EEPROM.h>

#define EEPROM_OFFSET 2750

uint8_t zone_count;
uint8_t * leds_per_zones;

WiFiUDP Udp;



HSBK * zones;
uint16_t power_status = 0; //0 | 65535


//vars for Lifx-Z effects. currently only 'move'.
unsigned long last_move_effect_time;
uint32_t move_speed;
uint8_t  move_direction;
uint16_t move_start_led = 0;

unsigned long lastUpdate = 0;

long lastupdate = 0;
LifxPacket request;
uint16_t packetSize;

// detect userVar0 change
uint16_t prevUserVar0;

void prepareResponse(LifxPacket &response, uint16_t packet_type, uint16_t data_size) {
  response.res_required = response.ack_required = false;
  response.type =  packet_type;
  response.protocol = 1024;
  response.reserved2 = NULL_BYTE;
  response.addressable = true;

  uint8_t mac[6];
  WiFi.macAddress(mac);

  memcpy(response.target, mac, 6);
  response.target[7] = response.target[6] = NULL_BYTE;

  /* spells out "LIFXV2" - version 2 of the app changes the site address to this... */
  response.reserved1[0] = 0x4c;
  response.reserved1[1] = 0x49;
  response.reserved1[2] = 0x46;
  response.reserved1[3] = 0x58;
  response.reserved1[4] = 0x56;
  response.reserved1[5] = 0x32;
  
  response.data_size = data_size; //NOT included in response
  response.size = response.data_size + LifxPacketSize; //included in response!
}

void sendPacket2(LifxPacket &pkt, uint8_t responseData[], uint16_t data_size, bool finishPacket) {
  //Serial.print(F("OUT "));
  //printLifxPacket(pkt, responseData);
  Udp.beginPacket(Udp.remoteIP(), 56700); //startUdpSendPacket():
  // Udp.beginPacket(broadcastIp, LifxPort); //BROADCASTS (test sync to multiple clients)
  Udp.write(pkt.raw, LifxPacketSize);
  Udp.write(responseData, data_size);
  if (finishPacket)
    Udp.endPacket();
}

void sendPacket(LifxPacket &pkt, uint8_t responseData[]) {
  sendPacket2(pkt, responseData, pkt.data_size, true);
}

void prepareAndSendPacket(LifxPacket &request, LifxPacket &response, uint16_t packet_type, uint8_t responseData[], uint16_t data_size) {
  prepareResponse(response,
                  request.ack_required ? LIFX_ACK : packet_type,
                  request.ack_required ?  0 : data_size);
  sendPacket(response, responseData);
}

void sendLightStateResponse(LifxPacket &request, LifxPacket &response, uint8_t responseData[]) {
  memcpy(responseData, zones[0].raw, 8);
  writeUInt(responseData, 8, NULL_BYTE);
  writeUInt(responseData, 10, power_status);
  uint8_t i = 0;
  // label
  lifxEeprom eeprom = readEEPROM();
  for (i = 0; i < 32; i++) {
    responseData[12 + i] = lowByte(eeprom.label[i]);
  }
  //tags (reserved)
  for (i = 0; i < 8; i++) {
    responseData[44 + i] = NULL_BYTE;
  }
  prepareAndSendPacket(request, response, LIGHT_STATUS, responseData, 52);
}


//gets called once at boot. Do all initialization that doesn't depend on network here
void userSetup()
{
  
  lifxEeprom eepromU = readEEPROM();

  if(eepromU.userVar0 == 0) {
    if(ledCount <64)
      zone_count = 8;
    else if(ledCount < 80)
      zone_count = 64;
    else zone_count = 64;
    //if(zone_count > 80) zone_count = 80;
  } else {
    zone_count = userVar0;
  }

  

  //  zone_count = ledCount / 8 * 8;
  //if(zone_count > 80) zone_count = 80;

  Serial.println("Zone ct = " + String(zone_count));
  
  // allocate zones and leds
  if (zones) free(zones);
  zones = (HSBK*)(malloc(zone_count * sizeof(HSBK)));
  
  if (leds_per_zones) free(leds_per_zones);
  leds_per_zones = (uint8_t*)(malloc(zone_count * sizeof(uint8_t)));

  float ledsRemaining = ledCount;

  for(uint8_t zone = 0; zone < zone_count; zone++) {
    leds_per_zones[zone] = (ledsRemaining/(zone_count-zone)+0.5);
    ledsRemaining = ledsRemaining - leds_per_zones[zone]; 
    //Serial.println("Zone " + String(zone) + " = " + String(leds_per_zones[zone]) + " - " + String(ledsRemaining) + " remain...");   
  }

  for (uint16_t i = 0; i < zone_count; i++) {
    zones[i].hue = 0;
    zones[i].sat = 0;
    zones[i].bri = 0;
    zones[i].kel = 0;
    zones[i].dur = 10;
  }


}

uint8_t prevMode = FX_MODE_STATIC;

void handleRequest(LifxPacket &request) {
  
  uint16_t i = 0; //global iterator used in this method for various stuff.
  LifxPacket response;  //first 36 bytes;
  response.source = request.source;
  response.sequence = request.sequence;
  //get lifx payload.
  uint8_t reqData[ request.type == SET_EXTENDED_COLOR_ZONES ? 8 : request.data_size ];
  Udp.read(reqData, request.type == SET_EXTENDED_COLOR_ZONES ? 8 : request.data_size);
  uint8_t responseData[LifxMaximumPacketSize - LifxPacketSize]; //Lifx Payload for response

  //Serial.print("IN  ");
  //printLifxPacket(request, reqData);
  lifxEeprom eeprom;
  switch (request.type) {
    case /* 0x75 (117) */ SET_POWER_STATE: {
        power_status = word(reqData[1], reqData[0]); // 0 | 65535

        if (power_status == 0) {
          bri = 0;
          effectCurrent = prevMode;
          arlsLock(0,REALTIME_MODE_GENERIC);
          colorUpdated(6);
        }
        else {
      // turn on...
          Serial.println("SET_POWER_STATE");
          setLight();
          bri = 255;
          prevMode == strip.getMode();
          arlsLock(65000,REALTIME_MODE_GENERIC);
          colorUpdated(6);
          
        }
      }
    case /* 0x74 (116) */ GET_POWER_STATE: {
        writeUInt(responseData, 0, power_status);
        prepareAndSendPacket(request, response, POWER_STATE, responseData, 2); //0x76 (118)
        return;
      }
    case /* 0x66 (102) */ SET_LIGHT_STATE: {
        
        for (i = 0; i < zone_count; i++) {
          memcpy(&zones[i], reqData + 1, 10);
          if(zones[i].dur == 0) {
            zones[i].dur = 100;
          }
          //Serial.println("SET_LIGHT_STATE: " + String(zones[i].dur));
        }
        setLight();
      }
    case /* 0x65 (101) */ GET_LIGHT_STATE: sendLightStateResponse(request, response, responseData); break;

    case /* 0x67 (103) */ SET_WAVEFORM:
    case /* 0x77 (119) */ SET_WAVEFORM_OPTIONAL: {
        WaveFormPacket dataPacket; //todo: we have other things in this packet! (waveform)
        memcpy(dataPacket.raw, reqData, 25);
        for (i = 0; i < zone_count; i++) {
          if (dataPacket.set_hue || request.type == SET_WAVEFORM)
            zones[i].hue = dataPacket.color.hue;
          if (dataPacket.set_saturation || request.type == SET_WAVEFORM)
            zones[i].sat = dataPacket.color.sat;
          if ((dataPacket.set_brightness && zones[i].bri > 0) || request.type == SET_WAVEFORM)
            zones[i].bri = dataPacket.color.bri;
          if (dataPacket.set_kelvin || request.type == SET_WAVEFORM)
            zones[i].kel = dataPacket.color.kel;
        }
        setLight();
        sendLightStateResponse(request, response, responseData);
        return;
      }
    case /* 0x1F5 (501) */ SET_COLOR_ZONES: {

        //Serial.println("SET_COLOR_ZONES: " + String(word(reqData[11], reqData[10])));

        //    byte apply = request.data[15];  //seems to be buggy?
        uint8_t zonesToSet = reqData[1] - reqData[0] + 1;
        memcpy(&zones[reqData[0]], reqData + 2, 10 * zonesToSet);
        
        setLight();
        if (request.ack_required) {
          prepareAndSendPacket(request, response, LIFX_ACK, responseData, 0);
          return;
        }
        if (!request.res_required)
          break;
      }
    case /* 0x1F6 (502) */ GET_COLOR_ZONES: {
        for (uint8_t x = 0; x < zone_count; x += 8) {
          responseData[0] = zone_count;
          responseData[1] = x; //first idx nr for each 8 zones send
          for (i = 0; i < 8; i++) { // i = zoneIdx
            memcpy(responseData + 2 + (i * 8), &zones[x + i], 8);
          }
          prepareAndSendPacket(request, response, STATE_MULTI_ZONE, responseData, 66 ); //506
        }
        break;
      }
    case /* 0x1FC (508) */ SET_MULTIZONE_EFFECT: {
        uint8_t move_enable = reqData[4];
        if (move_enable) {
          move_direction = reqData[31]; //effect.parameters[1];
          memcpy(&move_speed, reqData + 7, 4);
        } else {
          move_speed = 0;
          move_start_led = 0;
        }
      }
    case /* 0x1FB (507) */ GET_MULTIZONE_EFFECT: {
        for (i = 0; i < 60; i++)
          responseData[i] = NULL_BYTE;

        responseData[4] = move_speed > 0 ? 1 : 0;
        memcpy(responseData + 7, &move_speed, 4);
        responseData[31] = move_direction;
        prepareAndSendPacket(request, response, STATE_MULTIZONE_EFFECT, responseData, 59); //0x1FD (509)
        return;
      }
    case /* 0x31 (49) */ SET_LOCATION: {
        eeprom = readEEPROM();
        memcpy(eeprom.location, reqData , LifxLocOrGroupSize);
        memcpy(&eeprom.grp_loc_updated_at, reqData + 48, 8);
      
        writeEEPROM(eeprom);
      }
    case /* 0x30 (48) */GET_LOCATION: {
        eeprom = readEEPROM();
        memcpy(responseData, eeprom.location, LifxLocOrGroupSize);
        memcpy(responseData + 48, &eeprom.grp_loc_updated_at, 8);
        prepareAndSendPacket(request, response, STATE_LOCATION, responseData, 56); //0x32 (50)
        return;
      }
    case /* 0x33 (51) */ SET_GROUP: {
        eeprom = readEEPROM();
        memcpy(eeprom.group, reqData , LifxLocOrGroupSize);
        memcpy(&eeprom.grp_loc_updated_at, reqData + 48, 8);

        writeEEPROM(eeprom);
      }
    case /* 0x34 (52) */ GET_GROUP: {
        eeprom = readEEPROM();
        memcpy(responseData, eeprom.group, LifxLocOrGroupSize);
        memcpy(responseData + 48, &eeprom.grp_loc_updated_at, 8);
        prepareAndSendPacket(request, response, STATE_GROUP, responseData, 56); //0x35 (53)
        return;
      }
    case /* 0x18 (24) */ SET_BULB_LABEL: {
        eeprom = readEEPROM();
        for (int i = 0; i < LifxBulbLabelLength; i++) {
          Serial.println(String(reqData[i], HEX));
        }
        memcpy(&eeprom.label, &reqData, LifxBulbLabelLength);
        writeEEPROM(eeprom);
      }
    case /* 0x17 (23) */ GET_BULB_LABEL:
      eeprom = readEEPROM();
      // was eeprom.label
      prepareAndSendPacket(request, response, BULB_LABEL,(uint8_t*)eeprom.label,
                           sizeof(eeprom.label));  //0x19 RSP 25
      return;
    case /* 0x0e (14) */ GET_MESH_FIRMWARE_STATE:
    case /* 0x12 (18) */ GET_WIFI_FIRMWARE_STATE: {
        prepareAndSendPacket(request, response,
                             request.type == GET_WIFI_FIRMWARE_STATE ? WIFI_FIRMWARE_STATE : MESH_FIRMWARE_STATE,
                             FirmwareVersionData, 20);

        return;
      }
    case /* 0x20 (32) */ GET_VERSION_STATE: {
        // respond to get command
        writeUInt(responseData, 0, LifxBulbVendor);  writeUInt(responseData, 2, NULL_BYTE);
        writeUInt(responseData, 4, LifxBulbProduct); writeUInt(responseData, 6, NULL_BYTE);
        writeUInt(responseData, 8, LifxBulbVersion); writeUInt(responseData, 10, NULL_BYTE);
        prepareAndSendPacket(request, response, VERSION_STATE, responseData, 12); //0x21  (33)

        return;
      }
    case /* 0x02 (2) */ GET_PAN_GATEWAY: {
        responseData[0] = SERVICE_UDP;
        writeUInt(responseData, 1, LifxPort);
        writeUInt(responseData, 3, NULL_BYTE);
        prepareAndSendPacket(request, response, PAN_GATEWAY, responseData, 5);   //0x03 (3)
        return;
      }
    case /* 0x10 (16) */ GET_WIFI_INFO: {
        //write float signal (4bytes)
        writeUInt(responseData, 0, 0x3F7D); //0.99
        writeUInt(responseData, 2, 0x70A4); //0.99
        for (i = 4; i < 12; i++)
          responseData[i] = NULL_BYTE;
        //write short mcu_temp (2bytes)
        writeUInt(responseData, 12, 20);
        prepareAndSendPacket(request, response, STATE_WIFI_INFO, responseData, 14); // 0x11 (17)
        return;
      }
    case /* 510 */ SET_EXTENDED_COLOR_ZONES: {
        uint16_t index = word(reqData[6], reqData[5]);
        //Serial.println(word(reqData[1],reqData[0]));
        //get each HSBK value as stream and put it in the appropriate zone.
        for (i = 0; i < reqData[7] ; i++) {
          Udp.read(zones[index + i].raw, 8);
          Serial.println("!: "+String(zones[index+i].dur));
          if(zones[index+i].dur == 0) zones[index+i].dur = 1000;
        }
        setLight();
        if (request.ack_required) {
          prepareAndSendPacket(request, response, LIFX_ACK, responseData, 0);
          return;
        }
        if (!request.res_required)
          break;
      }
    case /* 511 */ GET_EXTENDED_COLOR_ZONES: {
        prepareResponse(response, STATE_EXTENDED_COLOR_ZONES, 5 + (8 * zone_count));
        writeUInt(responseData, 0, zone_count);
        writeUInt(responseData, 2, 0); //index
        responseData[4] = zone_count; //colors_count
        sendPacket2(response, responseData, 5 , false);
        //stream each hsbk zone value to client.
        for (i = 0; i < zone_count; i++)
          Udp.write(zones[i].raw, 8);
        Udp.endPacket();
        break;
      }
    case /* 0x36  */ 54: {
        //Unknown packet type. It has something to do with the Lifx cloud.Client asks bulb if it is attached to the cloud or not?
        //Responding with packet type 56 and 16 bytes as data stops the broadcast madness...
        for (i = 0; i < 16; i++)
          responseData[i] = NULL_BYTE;
        prepareAndSendPacket(request, response, 56, responseData, 16);
        return;
      }
    case 701: {
        //Responding with an empty 702 packet keeps the client happy.
        prepareAndSendPacket(request, response, 702, responseData, 0);
        return;
      }
      /*************
        DEBUG
      **************/
#ifdef DEBUG
    default: {
      /*
         uint8_t mac[6];
  WiFi.macAddress(mac);

        if (request.target[0] == mac[0] || request.target[0] == 0) {
          Serial.print(F("-> Unknown packet type, ignoring 0x"));
          Serial.print(request.type, HEX);
          Serial.print(" (");
          Serial.print(request.type);
          Serial.println(")");
        }
        break;
      }
      */
#endif
  }
}

void userConnected()
{
  Serial.println("4");

  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("UDP server on port %d\n", 56700);
  Udp.begin(56700);
}

void userLoop()
{
  
  if (!WLED_CONNECTED) return;

  if(prevUserVar0 != userVar0) {
      Serial.println("userVar0 was changed, new value is "+ userVar0);
      uint16_t newUserVar0 = userVar0;
        lifxEeprom eepromU = readEEPROM();
        eepromU.userVar0 = newUserVar0;
        writeEEPROM(eepromU);
        Serial.println("... to " + String(eepromU.userVar0));
        prevUserVar0 = eepromU.userVar0;
        userVar0 = prevUserVar0;
        userSetup();
        return;
  }
  

  uint16_t millisSince = millis() - lastUpdate;
  lastUpdate = millis();

  if (millisSince > 0)
  {
    uint16_t pixelsSoFar = 0, pixelCtr;
    uint16_t target_hue;
    uint8_t target_sat;
    CRGB pixel;
    
    
    for (uint8_t i = 0; i < zone_count; i++) {
      if (zones[i].dur > 0) {
        
        float factor = (float)millisSince / (float)zones[i].dur ;
        if(factor>1) factor = 1;

        //if(i==0) {
          //Serial.println("Duration is " + String(zones[i].dur) + ", millisSince = "  + String(millisSince) + "; factor is " + String(factor));
        //}

        if(zones[i].dur > millisSince)
        {
          zones[i].dur -= millisSince;
        }
        else
        {
          zones[i].dur = 0;
        }

        if (power_status == 65535) {

          target_hue = map(zones[i].hue, 0, 65535, 0, 360);
          target_sat = map(zones[i].sat, 0, 65535, 0, 255);

          // if we are setting a "white" colour (kelvin temp)
          if (zones[i].kel > 0 && target_sat < 1) {
            // convert kelvin to RGB
            rgb kelvin_rgb = kelvinToRGB(zones[i].kel);
            // convert the RGB into HSV
            hsv kelvin_hsv = rgb2hsv(kelvin_rgb);
            // set the new values ready to go to the bulb (brightness does not change, just hue and saturation)
            target_hue = kelvin_hsv.h;
            target_sat = map(kelvin_hsv.s * 1000, 0, 1000, 0, 255); //multiply the sat by 1000 so we can map the percentage value returned by rgb2hsv
          }

          pixel = CHSV(map(target_hue, 0, 360, 0, 255), target_sat, zones[i].bri / 256);

          //Serial.println("Zone " + String(i) + " : " + String(pixel.red) + " - " + String(pixel.Green) + " - " + String(pixel.blue));

          for(pixelCtr = pixelsSoFar; pixelCtr < pixelsSoFar + leds_per_zones[i]; pixelCtr++) {
              CRGB prevPixel = col_to_crgb(strip.getPixelColor(pixelCtr));

              CRGB newPixel;
              newPixel.red = ((pixel.red - prevPixel.red) * factor) + prevPixel.red;
              newPixel.green = ((pixel.green - prevPixel.green) * factor) + prevPixel.green;
              newPixel.blue = ((pixel.blue - prevPixel.blue) * factor) + prevPixel.blue;

              strip.setPixelColor(pixelCtr, newPixel.red, newPixel.green, newPixel.blue);   
          }
        }
      }
      pixelsSoFar = pixelsSoFar + leds_per_zones[i];
    }
    
  }

  if (power_status == 65535) {
    strip.show();
    arlsLock(65000,REALTIME_MODE_GENERIC);
    colorUpdated(5);
  }

  

  // if there's UDP data available, read the packet.
  packetSize = Udp.parsePacket();

  if (packetSize >= LifxPacketSize) {
    Udp.read(request.raw, LifxPacketSize);
    request.data_size = packetSize - LifxPacketSize;
    handleRequest(request);
  }
}

void writeUInt(uint8_t data[], uint8_t offset, uint16_t value) {
  data[offset] = value;
  data[offset + 1] = value >> 8;
}

void setLight() {

}

CRGB col_to_crgb(uint32_t color)
{
  CRGB fastled_col;
  fastled_col.red =   (color >> 16 & 0xFF);
  fastled_col.green = (color >> 8  & 0xFF);
  fastled_col.blue =  (color       & 0xFF);
  return fastled_col;
}

void hsb2rgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t rgb[]) {
  uint8_t setColorIdx[] {1, 0, 2};
  hue = hue % 360;
  rgb[0] = rgb[1] = rgb[2] = val; // (only if sat == 0) Acromatic color (gray). Hue doesn't mind.
  if (sat != 0) {
    uint8_t base = ((255 - sat) * val) >> 8;
    uint8_t slice = hue / 60;
    rgb[ slice < 2 ? 2 : slice < 4 ? 0 : 1 ] = base;
    rgb[ setColorIdx[slice % 3] ] = (((val - base) * (!slice ? hue : (slice % 2 ? (60 - (hue % 60)) : (hue % 60)))) / 60) + base;
  }
}

///////////// EEPROM stuff
void printLocOrGroup(uint8_t data[]) {
  for (uint8_t x = 16; x < LifxLocOrGroupSize; x++)
    Serial.write(data[x]);
}

char eeprom_check[] = { 'L', 'I', 'F', 'X' };

lifxEeprom createDefaultEEPROM() {
  lifxEeprom newEeprom;
  Serial.println(F("Creating new Lifx EEPROM data"));
      char *myData = "WLEDLocation1234WLED Location                   ";
      memcpy(newEeprom.location, myData , LifxLocOrGroupSize);
            myData = "WLEDGroup1234567WLED Group                      ";
      memcpy(newEeprom.group, myData , LifxLocOrGroupSize);
      memcpy(newEeprom.label, serverDescription, LifxBulbLabelLength);
      newEeprom.grp_loc_updated_at = 0;
      newEeprom.userVar0 = 0;
      userVar0 = 0;
      writeEEPROM(newEeprom);
      return newEeprom;
}

lifxEeprom readEEPROM() {

  lifxEeprom eeprom;

  EEPROM.begin(140 + EEPROM_OFFSET);
  //Serial.println(F("Restoring bulb settings from EEPROM..:"));
  //read 136 bytes from eeprom and push into eeprom struct. (offset 4)
  for (uint8_t x = 0; x < 142; x++) {
    uint8_t b = EEPROM.read(x + EEPROM_OFFSET);
    //Serial.print(b, HEX);
    //Serial.print(" ");
    if (x > 3) //push every byte beyond position 3 in struct
      eeprom.raw[x - 4] = b;
    else if (b != eeprom_check[x]) { //the first 4 bytes must spell LIFX
      //Serial.println(F("EEPROM does not contain LIFX settings."));

      return createDefaultEEPROM();
      break; //if not, exit the loop.
    }
  }

  //Serial.println();
  //Serial.print(F("Label: "));
  //Serial.println(eeprom.label);
  //Serial.print(F("Location: "));
  //printLocOrGroup(eeprom.location);
  //Serial.println();
  //Serial.print(F("Group: "));
  //printLocOrGroup(eeprom.group);
  //Serial.println();
  //Serial.print("userVar0: " + String(eeprom.userVar0));
  //Serial.println();
  userVar0 = eeprom.userVar0;
  return eeprom;
}

void writeEEPROM(lifxEeprom eeprom) {
  EEPROM.begin(140 + EEPROM_OFFSET);
  Serial.println(F("Writing settings to EEPROM..."));
  for (uint8_t x = 0; x < 4; x++) {
    Serial.println(eeprom_check[x]);
    (F("Writing settings to EEPROM..."));
    EEPROM.write(x + EEPROM_OFFSET, eeprom_check[x]);
    Serial.println(EEPROM.read(x + EEPROM_OFFSET));
  }
  for (uint8_t x = 4; x < 142; x++) {
    EEPROM.write(x + EEPROM_OFFSET, eeprom.raw[x - 4]);
  }

  EEPROM.commit();
  Serial.println(F("Done!"));
}


void printLifxPacket(LifxPacket &request, uint8_t reqData[]) {
  uint8_t i = 0;

  Serial.print(Udp.remoteIP()); //todo: broadcast or unicast...
  Serial.print(F(":"));
  Serial.print(Udp.remotePort()); //todo: will change when implementing decent broadcasts

  Serial.print(F(" Size:"));
  Serial.print(request.size);

  //    Serial.print(F(" | Proto: "));
  //    Serial.print(request.protocol);

  //    Serial.print(F(" (0x"));
  //    Serial.print(request.raw[2], HEX);
  //    Serial.print(F(" "));
  //    Serial.print(request.raw[3], HEX);
  //    Serial.print(F(" "));
  //Serial.print(F(")"));

  //    Serial.print(F(") | addressable: "));
  //    Serial.print(request.addressable);
  //
  //    Serial.print(F(" | tagged: "));
  //    Serial.print(request.tagged);
  //
  //    Serial.print(F(" | origin: "));
  //    Serial.print(request.origin);

  Serial.print(F(" | source: 0x"));
  Serial.print(request.source, HEX);

  Serial.print(F(" | target: 0x"));
  for (i = 0; i < 8; i++) {
    Serial.print(request.target[i], HEX);
    Serial.print(F(" "));
  }
/*
  if((request.target[0] == mac[0] && 
      request.target[1] == mac[1] &&
      request.target[2] == mac[2] &&
      request.target[3] == mac[3] &&
      request.target[4] == mac[4] &&
      request.target[5] == mac[5]) ||
      (request.target[0] ==0 && 
      request.target[1] == 0 &&
      request.target[2] == 0 &&
      request.target[3] == 0 &&
      request.target[4] == 0 &&
      request.target[5] == 0)) Serial.print(F(" (me!) "));
      else Serial.print(F(" (not me!) "));*/
  //    Serial.print(F(" | reserved1: 0x"));
  //    for(i = 0; i < 6; i++) {
  //      Serial.print(request.reserved1[i], HEX);
  //      Serial.print(F(" "));
  //    }

  Serial.print(F(" | res_required:"));
  Serial.print(request.res_required);

  Serial.print(F(" | ack_required:"));
  Serial.print(request.ack_required);

  //    Serial.print(F(" | reserved2: 0x"));
  //    Serial.print(request.reserved2, HEX);

  Serial.print(F(" | sequence: 0x"));
  Serial.print(request.sequence, HEX);

  //    Serial.print(F(" | reserved3: 0x"));
  //    Serial.print(request2.reserved3, HEX);

  Serial.print(F(" | type: 0x"));
  Serial.print(request.type, HEX);
  Serial.print(" (");
  Serial.print(request.type);
  Serial.print(")");

  //    Serial.print(F(" | reserved4: 0x"));
  //    Serial.print(request.reserved4, HEX);

  Serial.print(F(" | data: "));
  if (request.type == 510 || request.type == 512)
    Serial.print(F("< Stream >"));
  else {
    for (i = 0; i < request.data_size; i++) {
      Serial.print(reqData[i], HEX);
      Serial.print(F(" "));
    }
  }

  //    Serial.print(F(" | data_size:"));
  //    Serial.print(request.data_size);
  Serial.println();
}
#endif
