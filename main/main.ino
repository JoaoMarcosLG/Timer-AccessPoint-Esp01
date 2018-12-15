/*
 *  Timer 2.0
 *  Autor: João Marcos Lana Gomes
 *  Julho - 2018
 * 
 *  Mapeamento dos pinos do ESP-01
 *  -      0: SDA - RTC DS3231
 *  - (TX) 1: PushButton (c/ resistor de PULL-UP)
 *  -      2: SCL - RTC DS3231
 *  - (RX) 3: Saida (Rele 5v c/ driver)
 *  
 *  Mapeamento da EEPROM:
 *  00 -> times_count
 *  
 *  01 -> times[0].hour
 *  02 -> times[0].minute
 *  03 -> times[1].hour
 *  04 -> times[1].minute
 *  05 -> times[2].hour
 *  06 -> times[2].minute
 *  07 -> times[3].hour
 *  08 -> times[3].minute
 *  09 -> times[4].hour
 *  10 -> times[4].minute
 *  
 *  11 -> time_on.hour
 *  12 -> time_on.minute
 *  13 -> time_on.second
 */

// --- Bibliotecas ---
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>

#include <EEPROM.h>
#include <Wire.h>
#include <RTClib.h>

// Constantes do projeto
#define TIME_ON_HOUR   23
#define TIME_ON_MINUTE 24
#define TIME_ON_SECOND 25

// Mapeamento de HardWare
#define button 1
#define button_lig LOW  // Estado do botao pressionado
#define relay 3

// --- Variaveis auxiliares ---
bool wifi_status = false;

bool relay_status = false;

bool button_curr, button_prev;

// --- Configuracoes da rede ---
const char *ssid = "Timer";
const char *password = "";

ESP8266WebServer server(80);

// --- Estrutura dos horarios de acionamento ---
struct Hour {
  byte hour = 0;
  byte minute = 0;
  byte second = 0;

  void set(byte h, byte m, byte s = 0) {
    hour = h;
    minute = m;
    second = s;
  }

  bool cmp(byte h, byte m) {
    if(h == hour && m == minute)
      return true;
    else
      return false;
  }

  bool cmp(byte h, byte m, byte s) {
    if(h == hour && m == minute && s == second)
      return true;
    else
      return false;
  }

  unsigned long toSeconds() {
    return ((hour * 3600) + (minute * 60) + second);
  }
} times[5], time_on;

byte times_count = 0;  // Variavel que armazena qnt de horarios programados

byte last_time = 255;  // Variavel que armazena ultimo horario acionado

RTC_DS3231 rtc;  // Variavel do RTC DS3231

// --- Funcoes auxiliares ---
void buttonRead();    // Funcao de leitura do botao

void EEPROM_write();  // Funcao para salvar informacoes na memoria EEPROM
void EEPROM_read();   // Funcao para ler informacoes na memoria EEPROM

bool timesCheck();    // Funcao que confere se esta no horario de acionar saida

void wifiBegin();     // Funcao que liga AccessPoint
void wifiSleep();     // Funcao que desliga AccessPoint

void handleRoot();    // Funcao da pagina HTML principal
void handleTime();    // Funcao da pagina HTML de visualizacao dos horarios
void handleConfig();  // Funcao da pagina HTML de configuracao dos horarios de acionamento
void handleClock();   // Funcao da pagina HTML de configuracao do relogio
void handleDel();     // Funcao da pagina HTML de apagar horarios cadastrados

// --- MAIN ---
void setup() {
  // Espera 1s para estabilizacao
	delay(1000);

  // Configura IO's
  pinMode(button, INPUT);
  pinMode(relay, OUTPUT);

  // Desabilita AccessPoint
  wifiSleep();

  // Configura pinos de comunicacao com o relogio
  Wire.pins(0,2);
  Wire.begin(0,2);  // 0 = sda | 2 = scl

  // Inicia a comunicacao com o relogio (DS3231)
  rtc.begin();

  // Le dados salvos na EEPROM
  EEPROM_read();
}

// --- LOOP ---
void loop() {
  // Confere se botao foi pressionado
  buttonRead();

  // Se horarios ja configurados...
  if(times_count > 0) {
    static unsigned long time_buf;  // Variavel que ira conter o instante de acionamento da saida
    // Se relay desligado...
    if(!relay_status) {
      // Confere se esta no horario de acionar saida
      if(timesCheck()) {
        digitalWrite(relay, HIGH);  // Aciona saida
        relay_status = true;  // Atualiza Flag de estado do relay
        time_buf = millis();  // Armazena horario de acionamento da saida
      }
    }
    else { // Senao...
      if(millis() - time_buf > time_on.toSeconds() * 1000) {
        digitalWrite(relay, LOW);  // Desaciona saida
        relay_status = false;  // Atualiza Flag de estado do relay
      }
    }
  }
  
  // Se WiFi ligado...
  if(wifi_status){  // Inicia comunicacao HTTP
	  server.handleClient();
  }
}

// --- Desenvolvimento das funcoes ---

void buttonRead() {
  // Le o estado do botao
  button_curr = digitalRead(button);

  // Se botao pressionado...
  if(button_curr == button_lig && button_prev != button_lig) {
    // Muda estado da Flag
    wifi_status = !wifi_status;

    // Liga/Desliga o AccessPoint
    if(wifi_status)
      wifiBegin();
    else
      wifiSleep();
  }

  // Atualiza estado anterior do botao
  button_prev = button_curr;
}

void EEPROM_write() {  
  EEPROM.begin(512);  // Inicia EEPROM com 512 bytes

  EEPROM.write(0, times_count);

  byte j = 1;
  for(int i=0; i<times_count; i++) {
    EEPROM.write(j, times[i].hour);
    EEPROM.write(j + 1, times[i].minute);
    j+=2;
  }

  EEPROM.write(TIME_ON_HOUR, time_on.hour);
  EEPROM.write(TIME_ON_MINUTE, time_on.minute);
  EEPROM.write(TIME_ON_SECOND, time_on.second);

  EEPROM.end();  // Salva dados na EEPROM
}

void EEPROM_read() {
  EEPROM.begin(512);  // Inicia EEPROM com 512 bytes

  times_count = EEPROM.read(0);

  byte j = 1;
  for(int i=0; i<times_count; i++) {
    times[i].set(EEPROM.read(j), EEPROM.read(j+1));
    j+=2;
  }

  time_on.hour = EEPROM.read(TIME_ON_HOUR);
  time_on.minute = EEPROM.read(TIME_ON_MINUTE);
  time_on.second = EEPROM.read(TIME_ON_SECOND);

  EEPROM.end();  // Salva dados na EEPROM
}

bool timesCheck() {
  DateTime now = rtc.now();  // Variavel que contem dados atuais do relogio

  // Confere se o horario atual "bate" com algum dos horarios de acionamento
  for(int i=0; i<times_count; i++) {
    if(times[i].cmp(now.hour(), now.minute())) { // Se sim...
      if(last_time != i){
        last_time = i;
        return true;  // Retorna 'true'
      }
    }
  }

  return false;  // Caso nenhum horario "bata", retorna 'false'
}

void wifiBegin() {
  WiFi.forceSleepWake();  // Forca re-ligamento do WIFI
  delay(10);              // Delay para estabilizacao

  // Inicia AccessPoint
  WiFi.softAP(ssid, password);

  // Configura IP para acesso (IP: 192.168.4.1)
  IPAddress myIP = WiFi.softAPIP();
  
  // Configura Servidor
  server.on("/", handleRoot);
  server.on("/main", handleRoot);
  server.on("/time", handleTime);
  server.on("/config" , handleConfig);
  server.on("/clock", handleClock);
  server.on("/del", handleDel);

  // Inicia Servidor
  server.begin();
}

void wifiSleep() {
  WiFi.softAPdisconnect(); // Desabilita AccessPoint 
  WiFi.mode(WIFI_OFF);     // Desabilita o WIFI
  WiFi.forceSleepBegin();  // Forca desligamento do WIFI
  delay(10);               // Delay para estabilizacao
}

void handleRoot() {
  // Pagina HTML inicial
  String html = "<!DOCTYPE html> <html lang=\"en\"> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Timer</title> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #315e8a; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; border: 2px solid; font-size: 20px; font-weight: 600; text-decoration: none; } .azul { border-color: #315e8a; color: #315e8a; } .vermelho { border-color: #800000; color: #800000; } </style> </head> <body> <center> <h1>Timer</h1> <a href=\"time\" class=\"botao azul\" style=\"padding: 10px 59px;\">Horarios</a><br/><br/><br/> <a href=\"clock\" class=\"botao azul\" style=\"padding: 10px 64px;\">Relogio</a><br/><br/><br/> <a href=\"del\" class=\"botao vermelho\" style=\"padding: 10px 64px;\">Resetar</a><br/><br/><br/> </center> </body> </html>";
  server.send(200, "text/html", html);
}

void handleTime() {
  String html = "<!DOCTYPE html> <html lang=\"en\"> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Horarios</title> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #315e8a; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border: 2px solid #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Horarios</h1> <p>Horarios cadastrados: <!--times_count--></p> <!--times--> <p>Tempo ligado: <!--time_on--></p> <a href=\"config\" class=\"botao\">Configurar</a><br/><br/> <a href=\"main\" class=\"botao\">Inicio</a> </center> </body> </html>";

  html.replace("<!--times_count-->", String(times_count));

  String buf = "";
  for(int i=0; i<times_count; i++) {
    buf += String("<p>");
    buf += String("Horario ") + String(i) + String(" -> ");
    buf += String(times[i].hour) + String(":") + String(times[i].minute);
    buf += String("\n");
    buf += String("</p>");
  }
  html.replace("<!--times-->", buf);

  buf = String(time_on.hour) + String(":") + String(time_on.minute) + String(":") + String(time_on.second);
  html.replace("<!--time_on-->", buf);

  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}

void handleConfig() {
  // Variavel que contem o codigo html
  String html = "";

  // Se foram enviados argumentos... Trata-os
  if(server.args() > 0) {
    byte j = 0;

    // Repete a quantidade de argumentos enviados
    for(byte i=0; i<server.args(); i++) {
      // Se for o argumento referente ao tempo ligado...
      if(server.argName(i) == "lig") {
        // Se contem um valor de horario...
        if(server.arg(i).length() > 0) { // Configura tempo ligado
          time_on.set(server.arg(i).substring(0, 2).toInt(), 
                      server.arg(i).substring(3, 5).toInt(),
                      server.arg(i).substring(6, 8).toInt());
        }
      } 
      // Senao, e referente aos horarios de ativacao...
      else {
        // Se contem valor de horario...
        if(server.arg(i).length() > 0) {  // Configura horarios
          times[j].set(server.arg(i).substring(0, 2).toInt(), 
                       server.arg(i).substring(3, 5).toInt());
          j++;
        }        
      }
    }

    times_count = j;  // Atualiza numero de horarios

    // Validacao de horarios cadastrados
    bool success = true;
    
    if(times_count > 0) {  // Se ha pelo menos um horario cadastrado...
      for(int i=0; i<times_count; i++) {  // Repete a quantidade de horarios cadastrados...
        if(times[i].cmp(0, 0)) {  // Se algum igual a 00:00 (horario padrao)...
          success = false;  // Retorna falso
        }
      }

      if(time_on.cmp(0, 0, 0)) {  // Se tempo ligado igual a 00:00:00...
        success = false;  // Retorna falso
      }
    } 
    else {  // Se nenhum horario cadastrado...
      success = false;  // retorna falso
    }
    
  
    // Se cadastro validado...
    if(success) {  // Retorna pagina de confirmacao de configuracao
      html = "<!DOCTYPE html> <html lang=\"en\"> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Success</title> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #315e8a; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border: 2px solid #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Configurado</h1> <p>Horarios cadastrados: <!--times_count--></p> <!--times--> <p>Tempo ligado: <!--time_on--></p> <a href=\"main\" class=\"botao\">Inicio</a> </center> </body> </html>";
      
      html.replace("<!--times_count-->", String(times_count));

      String buf = "";
      for(int i=0; i<times_count; i++) {
        buf += String("<p>");
        buf += String("Horario ") + String(i) + String(" -> ");
        buf += String(times[i].hour) + String(":") + String(times[i].minute);
        buf += String("\n");
        buf += String("</p>");
      }
      html.replace("<!--times-->", buf);

      buf = String(time_on.hour) + String(":") + String(time_on.minute) + String(":") + String(time_on.second);
      html.replace("<!--time_on-->", buf);

      EEPROM_write();  // Salva alteracoes na EEPROM
    }
    else {  // Se houve erro no cadastro...
      html = "<!DOCTYPE html> <html lang=\"en\"> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Fail</title> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #800000; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border: 2px solid #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Falha na Configuracao</h1> <a href=\"main\" class=\"botao\">Inicio</a> </center> </body> </html>";
    }
  }
  
  // Se pagina html vazia, utiliza a pagina de configuracao
  if(html == "")
    html = "<!DOCTYPE html> <html> <head> <title>Config</title> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #315e8a; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border-width: 2px; border-radius: 2px; border-color: #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Timer - Config</h1> <form method=\"get\"> <table> <tr> <td><label>Horario 1:</label></td> <td><input type=\"text\" name=\"h1\" placeholder=\"00:00\"></td> </tr> <tr> <td><label>Horario 2:</label></td> <td><input type=\"text\" name=\"h2\" placeholder=\"00:00\"></td> </tr> <tr> <td><label>Horario 3:</label></td> <td><input type=\"text\" name=\"h3\" placeholder=\"00:00\"></td> </tr> <tr> <td><label>Horario 4:</label></td> <td><input type=\"text\" name=\"h4\" placeholder=\"00:00\"></td> </tr> <tr> <td><label>Horario 5:</label></td> <td><input type=\"text\" name=\"h5\" placeholder=\"00:00\"></td> </tr> <tr></tr> <tr> <td><label>Tempo Ligado:</label></td> <td><input type=\"text\" name=\"lig\" placeholder=\"00:00:00\"></td> </tr> </table> <input class=\"botao\" type=\"submit\" value=\"Enviar\"> </form> </center> </body> </html>";

  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}

void handleClock() {
  // Pagina de configuracao do relogio
  String html = "";
  
  // Verifica se existe algum argumento
  if(server.args() > 0) {
    // Repete a quantidade de argumentos enviados
    for(int i=0; i<server.args(); i++) {
      // Verifica se botao pressionado
      if(server.argName(i) == "button") {  // Retorna pagina de configuracao de horario
        html = "<!DOCTYPE html> <html> <head> <title>Relogio</title> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #315e8a; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border: 2px solid #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Relogio</h1> <form method=\"get\"> <table> <tr><td><b><label>Configurar:</label></b></td></tr> <tr> <td><label>Horas:</label></td> <td><input type=\"text\" name=\"hour\" maxlength=\"2\"></td> </tr> <tr> <td><label>Minutos:</label></td> <td><input type=\"text\" name=\"minute\" maxlength=\"2\"></td> </tr> </table> <input class=\"botao\" type=\"submit\" value=\"Enviar\"> </form> </center> </body> </html>";
      } 
      else {
        static byte hora, minuto;
        bool upd = true;

        // Configura relogio
        if(server.argName(i) == "hour")
          if(server.arg(i).toInt() != hora)
            hora = server.arg(i).toInt();
          else  // Se hora programada for igual horario ja programado...
            upd = false;  // Nao atualiza horario
        else if(server.argName(i) == "minute")
          if(server.arg(i).toInt() != minuto)
            minuto = server.arg(i).toInt();
          else  // Se minuto programada for igual horario ja programado...
            upd = false;  // Nao atualiza horario
        
        if(upd)  // Se atualiza display igual a true...
          rtc.adjust(DateTime(0, 0, 0, hora, minuto, 0));  // Atualiza horario
      }
    }
  }


  // Se pagina vazia, utiliza a pagina de exibicao do horarios
  if(html == "") {
    html = "<!DOCTYPE html> <html> <head> <title>Relogio</title> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <meta http-equiv=\"refresh\" content=\"1\"> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #315e8a; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border: 2px solid #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Relogio</h1> <h2><!--hour-->:<!--minute-->:<!--second--></h2> <form><input class=\"botao\" type=\"submit\" value=\"Configurar\" name=\"button\"></form> <br/> <a class=\"botao\" href=\"main\">Inicio</a> </center> </body> </html>";
    
    DateTime now = rtc.now();  // Variavel que armazena o horario atual
  
    html.replace("<!--hour-->", (String)now.hour());
    html.replace("<!--minute-->", (String)now.minute());
    html.replace("<!--second-->", (String)now.second());
  }
  
  
  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}

void handleDel() {
  String html = "<!DOCTYPE html> <html lang=\"en\"> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Delete</title> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #800000; font-weight: 500; padding: 10px; } .botao { background-color: #800000; color: white; padding: 5px 50px; border: 2px solid black; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Apagar horarios</h1> <form method=\"get\"> <label><b>Deseja mesmo apagar os horarios cadastrados?</b></label><br/> <input class=\"botao\" type=\"submit\" name=\"button\" value=\"Apagar\"> </form> </center> </body> </html>";

  // Verifica se existe algum argumento
  if(server.args() > 0) {
    // Percorre todos os argumentos
    for(int i=0; i<server.args(); i++) {
      // Se argumento for "button"
      if(server.argName(i) == "button") {
        // Percorre todos os horarios cadastrados
        for(int j=0; j<times_count; j++) {
          times[j].set(0, 0);  // Limpa horario
        }

        time_on.set(0, 0, 0);  // Limpa tempo ligado
        
        times_count = 0;  // Limpa quantidade de horarios cadastrados
      }
    }

    // Retorna pagina de confirmacao
    html = "<!DOCTYPE html> <html lang=\"en\"> <head> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Delete</title> <style type=\"text/css\"> html { font-family: 'Roboto', sans-serif; } h1 { background-color: #dee3e6; color: #800000; font-weight: 500; padding: 10px; } .botao { background-color: #dee3e6; color: #315e8a; padding: 5px 50px; border: 2px solid #315e8a; font-size: 20px; font-weight: 600; text-decoration: none; margin-top: 15px; } </style> </head> <body> <center> <h1>Apagar horarios</h1> <label><b>Horarios apagados com sucesso.</b></label><br/><br/> <a href=\"main\" class=\"botao\">Inicio</a> </center> </body> </html>";
  }
  
  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}

