// Classe que define estrutura dos horarios

#ifndef Time_h
#define Time_h

#include "Arduino.h"
#include <RTClib.h>

class Time {
    private:
        byte _hour;
        byte _minute;
        byte _second;

    public:
        Time();
        Time(byte hour, byte minute, byte second);
        Time(DateTime time);
        byte get_hour();
        byte get_minute();
        byte get_second();
        bool cmp(byte hour, byte minute);
        bool cmp(byte hour, byte minute, byte second);
        bool operator == (const Time& other);
        bool operator != (const Time& other);
        bool operator > (const Time& other);
        bool operator < (const Time& other);
        bool operator >= (const Time& other);
        bool operator <= (const Time& other);
        Time operator + (const Time& other);
        Time operator - (const Time& other);
        unsigned long toSeconds();
        String toStr(bool showSeconds);
};

#endif