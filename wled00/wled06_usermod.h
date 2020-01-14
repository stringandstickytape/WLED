//
//struct LifxTimestamp
//{
//  byte second;
//  byte minute;
//  byte hour;
//  byte day;
//  char month[3]; // LE; ASCII encoded
//  byte year;
//}
#include <stdint.h> 
#include <Arduino.h>
//Packetsizes.
const uint8_t LifxPacketSize      = 36;
const uint8_t LifxMaximumPacketSize = 128;
const uint8_t LifxBulbLabelLength = 32;
const uint8_t LifxLocOrGroupSize  = 48; // 56;

union LifxPacket {
  uint8_t raw[];
  struct {
    /* frame */             //#       | Total #               | pos
    uint16_t size;          //2 bytes | 2 Bytes               | 0,1
    uint16_t protocol:12;   //2 bytes | 4 bytes               | 1,2
    uint8_t  addressable:1; //1 bit   | 4 bytes (in protocol)
    uint8_t  tagged:1;      //1 bit   | 4 bytes
    uint8_t  origin:2;      //2 bits  | 4 bytes
    uint32_t source;        //4 bytes | 8 bytes
    /* frame address */
    uint8_t  target[8];     //8 bytes | 16 bytes
    uint8_t  reserved1[6];  //6 bytes | 22 bytes
    uint8_t  res_required:1;//1 bit   | 23 bytes
    uint8_t  ack_required:1;//1 bit   | 23 bytes
    uint8_t  reserved2:6;   //6 bits  | 23 bytes
    uint8_t  sequence;      //1 bytes | 24 bytes
    /* protocol header */
    uint64_t reserved3;     //8 bytes | 32 bytes
    uint16_t type;          //2 bytes | 34 bytes
    uint16_t reserved4;     //2 bytes | 36 bytes
    /* DATA */
    uint16_t data_size;  //2bytes  (NOT included in responses!) 
  };
};

union HSBK {
  uint8_t raw[10];
  struct {
    uint16_t hue;
    uint16_t sat;
    uint16_t bri;
    uint16_t kel;  
    uint16_t dur;
  }; 
};

union WaveFormPacket {
  uint8_t raw[25];
  struct {
    uint8_t reserved;
    uint8_t transient;
    HSBK color;
    uint32_t period;
    float cycles;
    int16_t skew_ratio;
    uint8_t waveform;
    uint8_t set_hue;
    uint8_t set_saturation;
    uint8_t set_brightness;
    uint8_t set_kelvin;
  };
};

union lifxEeprom {
  uint8_t raw[138];
  struct {
    char label[LifxBulbLabelLength] = "";  //32 bytes
    uint8_t location[LifxLocOrGroupSize]; // = guid + label //48 bytes
    uint8_t group[LifxLocOrGroupSize];    // = guid + label //48 bytes
    uint64_t grp_loc_updated_at;                            //8  bytes
    uint16_t userVar0; // 2 bytes
  };
};

const uint16_t LifxPort = 56700;  // local port to listen on
// firmware versions, etc
//https://github.com/LIFX/products/blob/master/products.json
const uint16_t LifxBulbVendor = 1;
const uint16_t LifxBulbProduct = 32; //31 and 32 are both Lifx Z
const uint16_t LifxBulbVersion = 1;
const uint16_t LifxFirmwareVersionMajor = 2;
const uint16_t LifxFirmwareVersionMinor = 77;  //smaller than 77 = version 1 | higher or equal to 77 = version 2.
uint8_t FirmwareVersionData[] = { 
  0x44, 0x65, 0x73, 0x69, 0x67, 0x6e, 0x20, 0x62, //build timestamp
  0x79, 0x20, 0x56, 0x69, 0x6e, 0x7a, 0x7a, 0x42, //install timestamp
  lowByte(LifxFirmwareVersionMinor), highByte(LifxFirmwareVersionMinor),
  lowByte(LifxFirmwareVersionMajor), highByte(LifxFirmwareVersionMajor)
};

//messages: https://github.com/magicmonkey/lifxjs/blob/master/Protocol.md
const uint8_t NULL_BYTE = 0x0;
const uint8_t SERVICE_UDP = 0x01;
//const uint8_t SERVICE_TCP = 0x02;

// packet types
const uint8_t GET_PAN_GATEWAY = 0x02;          //REQ 2 GetService 
const uint8_t PAN_GATEWAY = 0x03;              //RSP 3 StateService

//const uint8_t GET_HOST_INFO = 0x0c;          //REQ 12
//const uint8_t STATE_HOST_INFO = 0x0d;        //RSP 13

const uint8_t GET_MESH_FIRMWARE_STATE = 0x0e;  //REQ 14 GetHostFirmware 
const uint8_t MESH_FIRMWARE_STATE = 0x0f;      //REQ 15 StateHostFirmware 

const uint8_t GET_WIFI_INFO = 0x10;          //REQ 16
const uint8_t STATE_WIFI_INFO = 0x11;        //RSP 17

const uint8_t GET_WIFI_FIRMWARE_STATE = 0x12;  //REQ 18 GetWifiFirmware 
const uint8_t WIFI_FIRMWARE_STATE = 0x13;		  //RSP 19 StateWifiFirmware 

const uint8_t GET_POWER_STATE = 0x74;          //REQ 116  GetPower 
const uint8_t SET_POWER_STATE = 0x75;	        //REQ 117 SetPower 
const uint8_t POWER_STATE = 0x76;               //RSP 118  StatePower 

const uint8_t GET_BULB_LABEL = 0x17; 		      //REQ 23 GetLabel 
const uint8_t SET_BULB_LABEL = 0x18; 		      //REQ 24 SetLabel 
const uint8_t BULB_LABEL = 0x19; 			        //RSP 25 StateLabel 

const uint8_t GET_VERSION_STATE = 0x20;	      //REQ 32 GetVersion 
const uint8_t VERSION_STATE = 0x21;		        //RSP 33 StateVersion 

//const uint8_t GET_INFO = 0x22;                 //REQ 34 GetInfo
//const uint8_t STATE_INFO = 0x23;               //RSP 35 StateInfo

const uint8_t LIFX_ACK = 0x2d;                      //RSP 45 Acknowledgement 

const uint8_t GET_LOCATION = 0x30;              //REQ 48 GetLocation
const uint8_t SET_LOCATION = 0x31;             //REQ 49 SetLocation
const uint8_t STATE_LOCATION = 0x32;           //RSP 50 StateLocation

const uint8_t GET_GROUP = 0x33;                //REQ 51
const uint8_t SET_GROUP = 0x34;                //REQ 52
const uint8_t STATE_GROUP = 0x35;              //RSP 53

//const uint8_t ECHO_REQUEST = 0x3A;             //REQ 58
//const uint8_t ECHO_RESPONSE = 0x3B;            //RSP 59

const uint8_t GET_LIGHT_STATE = 0x65;		        //REQ 101 Get
const uint8_t SET_LIGHT_STATE = 0x66;		        //REQ 102 SetColor   (reserved,color,duration)

//const uint8_t SET_DIM_ABSOLUTE = 0x68;           //dec: 104
//const uint8_t SET_DIM_RELATIVE = 0x69;	          //dec: 105
const uint8_t LIGHT_STATUS = 0x6b;               //RSP 107 State

//const uint8_t GET_INFRARED = 0x78;               //REQ 120
//const uint8_t STATE_INFRARED = 0x79;               //RSP 121
//const uint8_t SET_INFRARED = 0x7A;               //REQ 122

const uint8_t SET_WAVEFORM = 0x67;               //REQ 103
const uint8_t SET_WAVEFORM_OPTIONAL = 0x77;      //dec 119

//MULTI-ZONE MESSAGES
const uint16_t SET_COLOR_ZONES = 501;            //REQ
const uint16_t GET_COLOR_ZONES = 0x1F6;          //REQ 502

const uint16_t STATE_ZONE = 503;                 //RSP 0x1F7
const uint16_t STATE_MULTI_ZONE = 506;           //RSP

const uint16_t GET_MULTIZONE_EFFECT = 507;       //REQ 0x1FB
const uint16_t SET_MULTIZONE_EFFECT = 508;       //REQ 0x1FC
const uint16_t STATE_MULTIZONE_EFFECT = 509;     //RSP 0x1FD

//multi-zone messages (extended multizone >=v2.77)

const uint16_t SET_EXTENDED_COLOR_ZONES = 510;
const uint16_t GET_EXTENDED_COLOR_ZONES = 511;
const uint16_t STATE_EXTENDED_COLOR_ZONES = 512;

//TILE MESSAGES
//
//const uint16_t GET_DEVICE_CHAIN = 701;
//const uint16_t STATE_DEVICE_CHAIN = 702;
//const uint16_t SET_USER_POSITION = 703;
//const uint16_t GET_TILE_STATE_64 = 707;
//const uint16_t STATE_TILE_STATE_64 = 711;
//const uint16_t SET_TILE_STATE_64 = 715;
//

/* Source: http://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both */

struct rgb {
    double r;       // percent
    double g;       // percent
    double b;       // percent
};

struct hsv {
    double h;       // angle in degrees
    double s;       // percent
    double v;       // percent
};

static hsv      rgb2hsv(rgb in);
static rgb      hsv2rgb(hsv in);

hsv rgb2hsv(rgb in)
{
    hsv         out;
    double      min, max, delta;

    min = in.r < in.g ? in.r : in.g;
    min = min  < in.b ? min  : in.b;

    max = in.r > in.g ? in.r : in.g;
    max = max  > in.b ? max  : in.b;

    out.v = max;                                // v
    delta = max - min;
    if( max > 0.0 ) {
        out.s = (delta / max);                  // s
    } else {
        // r = g = b = 0                        // s = 0, v is undefined
        out.s = 0.0;
        out.h = NAN;                            // its now undefined
        return out;
    }
    if( in.r >= max )                           // > is bogus, just keeps compilor happy
        out.h = ( in.g - in.b ) / delta;        // between yellow & magenta
    else
    if( in.g >= max )
        out.h = 2.0 + ( in.b - in.r ) / delta;  // between cyan & yellow
    else
        out.h = 4.0 + ( in.r - in.g ) / delta;  // between magenta & cyan

    out.h *= 60.0;                              // degrees

    if( out.h < 0.0 )
        out.h += 360.0;

    return out;
}


rgb hsv2rgb(hsv in)
{
    double      hh, p, q, t, ff;
    long        i;
    rgb         out;

    if(in.s <= 0.0) {       // < is bogus, just shuts up warnings
        out.r = in.v;
        out.g = in.v;
        out.b = in.v;
        return out;
    }
    hh = in.h;
    if(hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long)hh;
    ff = hh - i;
    p = in.v * (1.0 - in.s);
    q = in.v * (1.0 - (in.s * ff));
    t = in.v * (1.0 - (in.s * (1.0 - ff)));

    switch(i) {
    case 0:
        out.r = in.v;
        out.g = t;
        out.b = p;
        break;
    case 1:
        out.r = q;
        out.g = in.v;
        out.b = p;
        break;
    case 2:
        out.r = p;
        out.g = in.v;
        out.b = t;
        break;

    case 3:
        out.r = p;
        out.g = q;
        out.b = in.v;
        break;
    case 4:
        out.r = t;
        out.g = p;
        out.b = in.v;
        break;
    case 5:
    default:
        out.r = in.v;
        out.g = p;
        out.b = q;
        break;
    }
    return out;     
}



/* Convert Kelvin to RGB: based on http://bit.ly/1bc83he */

rgb kelvinToRGB(long kelvin) {
  rgb kelvin_rgb;
  long temperature = kelvin / 100;

  if(temperature <= 66) {
    kelvin_rgb.r = 255;
  } 
  else {
    kelvin_rgb.r = temperature - 60;
    kelvin_rgb.r = 329.698727446 * pow(kelvin_rgb.r, -0.1332047592);
    if(kelvin_rgb.r < 0) kelvin_rgb.r = 0;
    if(kelvin_rgb.r > 255) kelvin_rgb.r = 255;
  }

  if(temperature <= 66) {
    kelvin_rgb.g = temperature;
    kelvin_rgb.g = 99.4708025861 * log(kelvin_rgb.g) - 161.1195681661;
    if(kelvin_rgb.g < 0) kelvin_rgb.g = 0;
    if(kelvin_rgb.g > 255) kelvin_rgb.g = 255;
  } 
  else {
    kelvin_rgb.g = temperature - 60;
    kelvin_rgb.g = 288.1221695283 * pow(kelvin_rgb.g, -0.0755148492);
    if(kelvin_rgb.g < 0) kelvin_rgb.g = 0;
    if(kelvin_rgb.g > 255) kelvin_rgb.g = 255;
  }

  if(temperature >= 66) {
    kelvin_rgb.b = 255;
  } 
  else {
    if(temperature <= 19) {
      kelvin_rgb.b = 0;
    } 
    else {
      kelvin_rgb.b = temperature - 10;
      kelvin_rgb.b = 138.5177312231 * log(kelvin_rgb.b) - 305.0447927307;
      if(kelvin_rgb.b < 0) kelvin_rgb.b = 0;
      if(kelvin_rgb.b > 255) kelvin_rgb.b = 255;
    }
  }

  return kelvin_rgb;
}

