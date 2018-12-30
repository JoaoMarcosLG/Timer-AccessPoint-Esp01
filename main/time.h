/*
    Autor: João Marcos Lana Gomes
    Dezembro, 2018

    === Descrição dos principais métodos ===

        Time() : Construtor da classe (Pode ser iniciado nulo, passando horarios (hora, minuto, segundo) ou um DateTime);

        get_hour(), get_minute(), get_second() : Retorna respectivos atributos;

        cmp() : Compara com outro horário, e retorna verdadeiro se são iguais (Pode comparar com ou sem os segundos);

        toSeconds() : Retorna horário em segundos (ex.: 12:30:10 => 45010);

        toStr() : Converte horario em uma string (ex.: "12:30:10" ou "12:30");

    === Operadores presentes ===

       >  >=  <  <=  ==  != : Operadores booleanos
       +  - : Operadores matemáticos
*/

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