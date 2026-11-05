#pragma once
#include <Arduino.h>

// Resolution and frame-rate index codes shared by the DJI R SDK status push (1D02 bytes 2/3,
// documented in dji-sdk/Osmo-GPS-Controller-Demo) and the DUML "cam_video_param_v2" parameter.
namespace djiformat {
inline const char* resolution(uint8_t i){
    switch(i){case 10:return "1080P";case 12:return "1080P 4:3";case 16:return "4K";case 45:return "2.7K";case 66:return "1080P 9:16";
    case 67:return "2.7K 9:16";case 95:return "2.7K 4:3";case 103:return "4K 4:3";case 105:return "1080P 1:1";case 106:return "2160P 1:1";
    case 107:return "3K 1:1";case 108:return "3K 9:16";case 109:return "4K 9:16";case 125:return "4K OPEN";default:return "";}
}
inline int fps(uint8_t i){
    switch(i){case 1:return 24;case 2:return 25;case 3:return 30;case 4:return 48;case 5:return 50;case 6:return 60;case 7:return 120;
    case 8:return 240;case 10:return 100;case 11:return 96;case 19:return 200;case 29:return 15;default:return 0;}
}
// "4K 60", or just whichever part is known.
inline String videoText(uint8_t resIdx,uint8_t fpsIdx){
    String m=resolution(resIdx);const int f=fps(fpsIdx);
    if(f){if(m.length())m+=' ';m+=String(f);}
    return m;
}
}
