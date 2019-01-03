/*
 *  Timer v2.0
 *  Autor: João Marcos Lana Gomes
 *  Dezembro - 2018
 * 
 *  Mapeamento dos pinos do ESP-01
 *  -      0: SDA - RTC DS3231
 *  - (TX) 1: PushButton (c/ resistor de PULL-UP)
 *  -      2: SCL - RTC DS3231
 *  - (RX) 3: Saida (Rele 5v c/ driver)
 *  
 *  Mapeamento da EEPROM:
 *  00 -> times_count
 *  01 -> timer_mode
 * 
 *  02 -> time_on.hour
 *  03 -> time_on.minute
 *  04 -> time_on.second
 * 
 *  05 -> times[0].hour
 *  06 -> times[0].minute
 *  07 -> times[1].hour
 *  08 -> times[1].minute
 *  09 -> times[2].hour
 *  10 -> times[2].minute
 *     .
 *     .
 *     .
 *  xx -> times[x].hour
 *  xx -> times[x].minute
 *  
 */

// --- Bibliotecas ---
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>

#include <EEPROM.h>
#include <Wire.h>
#include <RTClib.h>

#include "time.h"

// Constantes do projeto
#define TIMES_COUNT    1
#define TIMER_MODE     2
#define TIME_ON_HOUR   3
#define TIME_ON_MINUTE 4
#define TIME_ON_SECOND 5
#define TIMES_ADDR_INI 6

// Mapeamento de HardWare
#define button 1
#define button_lig LOW  // Estado do botao pressionado (PULL-UP)
#define relay 3

// --- Variaveis auxiliares ---
bool wifi_status = false;

bool relay_status = false;

// --- Configuracoes da rede ---
const char *ssid = "Timer";
const char *password = "";

ESP8266WebServer server(80);  // Instancia server (porta 80)

// Variaveis do tipo Time
Time *times;
Time time_on;

byte times_count = 0;  // Variavel que armazena qnt de horarios programados

bool timer_mode = 0;  // Modo: (default) 0 -> horários; 1 -> intervalo

byte last_time = 255;  // Variavel que armazena ultimo horario acionado

RTC_DS3231 rtc;  // Variavel do RTC DS3231

// --- Funcoes auxiliares ---
void buttonRead();    // Funcao de leitura do botao

void EEPROM_write();  // Funcao para salvar informacoes na memoria EEPROM
void EEPROM_read();   // Funcao para ler informacoes na memoria EEPROM

void timeOnCheck();   // Funcao que confere se esta no horario de acionar saida
void timeOffCheck();  // Funcao que confere se esta no horario de descaionar saida
void timeSort();      // Funcao para ordenar horarios
Time nextTime();      // Funcao que retorna proximo horario de acionamento

void wifiBegin();     // Funcao que liga AccessPoint
void wifiSleep();     // Funcao que desliga AccessPoint

// --- Back-End do servidor ---
void handleRoot();
void handleTime();
void handleMode();
void handleIntervalMode();
void handleTimeMode();

// --- MAIN ---
void setup() {
  // Espera 1s para estabilizacao
    delay(1000);

  // Configura IO's
  pinMode(button, INPUT);
  pinMode(relay, OUTPUT);

  // Desabilita AccessPoint
  wifiSleep();

  // Configura rotas do servidor
  server.on("/", handleRoot);
  server.on("/config", handleTime);
  server.on("/mode_select", handleMode);
  server.on("/interval_mode", handleIntervalMode);
  server.on("/time_mode" , handleTimeMode);

  // Inicia Servidor
  server.begin();

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
  if(times_count) {
    if(!relay_status)
      timeOnCheck();  // Confere se esta no horario de acionar saida
    else
      timeOffCheck();  // Confere se esta no horario de desacionar saida
  }
  
  // Se WiFi ligado...
  if(wifi_status){  // Inicia comunicacao HTTP
      server.handleClient();
  }
}

// --- Desenvolvimento das funcoes ---

void buttonRead() {
  // Vaiáveis
  static bool button_curr, button_prev;

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

  EEPROM.write(TIMES_COUNT, times_count);

  EEPROM.write(TIMER_MODE, timer_mode);

  byte j = TIMES_ADDR_INI;
  for(byte i=0; i<times_count; i++) {
    EEPROM.write(j, times[i].get_hour());
    EEPROM.write(j + 1, times[i].get_minute());
    j+=2;
  }

  EEPROM.write(TIME_ON_HOUR, time_on.get_hour());
  EEPROM.write(TIME_ON_MINUTE, time_on.get_minute());
  EEPROM.write(TIME_ON_SECOND, time_on.get_second());

  EEPROM.end();  // Salva dados na EEPROM
}

void EEPROM_read() {
  EEPROM.begin(512);  // Inicia EEPROM com 512 bytes

  times_count = EEPROM.read(TIMES_COUNT);

  timer_mode = EEPROM.read(TIMER_MODE);

  times = (Time*)malloc(times_count * sizeof(Time));

  for(byte i=0, j=TIMES_ADDR_INI; i<times_count; i++, j+=2) {
    times[i] = Time(EEPROM.read(j), EEPROM.read(j+1), 0);
  }

  time_on = Time(EEPROM.read(TIME_ON_HOUR), EEPROM.read(TIME_ON_MINUTE), EEPROM.read(TIME_ON_SECOND));

  EEPROM.end();  // Salva dados na EEPROM
}

void timeOnCheck() {
  DateTime now = rtc.now();  // Variavel que contem dados atuais do relogio

  // Confere se o horario atual "bate" com algum dos horarios de acionamento
  for(byte i=0; i<times_count; i++) {
    if(times[i].cmp(now.hour(), now.minute())) { // Se sim...
      if(last_time != i){
        last_time = i;  // Atualiza variável do ultimo horario acionado
        digitalWrite(relay, HIGH);  // Aciona saida
        relay_status = true;  // Atualiza Flag de estado do relay
      }
    }
  }
}

void timeOffCheck() {
  Time now = Time(rtc.now()); 
  Time time_off = times[last_time] + time_on;  // Horario de desacionamento = horario de acionamento + tempo ligado
  if(now >= time_off) {
    digitalWrite(relay, LOW);  // Desaciona saida
    relay_status = false;  // Atualiza Flag de estado do relay
  }
}

void timeSort() {
  // Compara e ordena horarios (Crescente)
  for(byte i=0; i<times_count-1; i++) {
    byte small = i;
    for(byte j=i+1; j<times_count; j++) {
      if(times[small] > times[j]) {
        small = j;
      }
    }
    if(times[i] != times[small]) {
      Time buff = times[i];
      times[i] = times[small];
      times[small] = buff;
    }
  }
}

Time nextTime() {
  // Se modo 'horarios'
  if(!timer_mode) {
    Time now = Time(rtc.now());

    // Percorre todos os horarios para verificar proximo horario de acionamento
    for(byte i=0; i<times_count; i++) {
      if(now > times[i]) {
        continue;
      } else {
        return times[i];
      }
    }
    return times[0];  // Se horario atual 'maior' que todos os horarios, entao retorna primeiro horario
  }
}

void wifiBegin() {
  WiFi.forceSleepWake();        // Forca re-ligamento do WIFI
  delay(10);                    // Delay para estabilizacao
  WiFi.softAP(ssid, password);  // Inicia AccessPoint
}

void wifiSleep() {
  WiFi.softAPdisconnect(); // Desabilita AccessPoint 
  WiFi.mode(WIFI_OFF);     // Desabilita o WIFI
  WiFi.forceSleepBegin();  // Forca desligamento do WIFI
  delay(10);               // Delay para estabilizacao
}

void handleRoot() {
  // Pagina HTML inicial
  String html = "<!doctype html><html lang=\"pt-br\"><head><title>Timer</title><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"><meta http-equiv=\"refresh\" content=\"1\"><style>.flex-container {display: flex;flex-direction: column;justify-content: center;}.center-x {display: block;margin-left: auto;margin-right: auto;}.no-margin {margin: 0;}.time-input {display: flex;flex-direction: row;}.time-input input {width: 50vw;margin: 5px !important;}.time-input button {margin-top: 5px;height: 42px;line-height: 30px;}.time-input label { margin: auto; }.risk {height:2px;border:none;color:#212529;background-color:#212529;margin-top: 0px;}html {background: rgb(248, 248, 248);margin-top: 24px;}div, form {margin-bottom: 12px;}input[type=\"text\"] {border-radius: 8px;font-size: 20px;margin-top: 12px;margin-bottom: 18px;height: 36px;padding-left: 10px;}h1, h2, h3, h4, h5, h6 {font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Oxygen, Ubuntu, Cantarell, \"Open Sans\", \"Helvetica Neue\", sans-serif;font-weight: 400;line-height: 1.5;color: #212529;margin: 0;}h1 { font-size: 70px; }h3 { font-size: 40px; }h5 { font-size: 20px; }.btn {display: inline-block;font-weight: 400;text-align: center;white-space: nowrap;vertical-align: middle;-webkit-user-select: none;-moz-user-select: none;-ms-user-select: none;user-select: none;border: 1px solid transparent;padding: 0.375rem 0.75rem;font-size: 1.6rem;line-height: 1.5;border-radius: 0.25rem;transition: color 0.15s ease-in-out, background-color 0.15s ease-in-out, border-color 0.15s ease-in-out, box-shadow 0.15s ease-in-out;}.btn:focus, .btn.focus {outline: 0;box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25);}.btn-primary {color: #fff;background-color: #007bff;border-color: #007bff;}.btn-primary:hover {color: #fff;background-color: #0069d9;border-color: #0062cc;}.btn-success {color: #fff;background-color: #28a745;border-color: #28a745;}.btn-success:hover {color: #fff;background-color: #218838;border-color: #1e7e34;}.btn-danger {color: #fff;background-color: #dc3545;border-color: #dc3545;}.btn-danger:hover {color: #fff;background-color: #c82333;border-color: #bd2130;}.btn-secondary {color: #fff;background-color: #6c757d;border-color: #6c757d;}.btn-secondary:hover {color: #fff;background-color: #5a6268;border-color: #545b62;}</style></head><body><div class=\"flex-container\"><div class=\"center-x\"><h1 >Timer</h1></div><div class=\"risk\"></div><div class=\"center-x\"><h3>{{ clock }}</h3></div><div class=\"center-x\"><h5>Prox. Horário: {{ time }}</h5></div><div class=\"center-x\" style=\"margin-top: 12px\"><button class=\"btn btn-primary\" style=\"width: 90vw; height: 100px\" onclick=\"window.location.replace('config')\">Configurações</button></div><form class=\"center-x\" onsubmit=\"sync_clk(this)\"><div><button type=\"submit\" class=\"btn btn-secondary\" style=\"width: 90vw\">Sincronizar relógio</button></div></form></div></body><script>var previous_length = {};function check_time(element, repeat) {id = element.name;if(!(id in previous_length)) { previous_length[id] = 0; }for(let i=0; i < repeat; i++) {if(element.value.length == ((3*i)+2) && element.value.length > previous_length[id]) {element.value += ':';}}previous_length[id] = element.value.length;}var time_count = 1;function add_cell() {let new_cell = document.createElement('div');new_cell.className = 'time-input';new_cell.innerHTML = `<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);new_cell.getElementsByTagName('input')[0].focus();let previous_button = new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className = 'btn btn-danger';previous_button.innerHTML = 'x';previous_button.onclick = function() { remove_cell(previous_button) };}function remove_cell(element) {let times_container = document.getElementById('times-container');times_container.removeChild(element.parentNode);let count = 0;for(let i=0; i < times_container.children.length; i++) {times_container.children[i].firstChild.innerText = `Horário ${++count}:`;}time_count = count;}function sync_clk(form) {let date = new Date();let input_hours = document.createElement('input');input_hours.type = 'text';input_hours.name = 'hours';input_hours.value = date.getHours();input_hours.hidden = true;form.appendChild(input_hours);let input_minutes = document.createElement('input');input_minutes.type = 'text';input_minutes.name = 'minutes';input_minutes.value = date.getMinutes();input_minutes.hidden = true;form.appendChild(input_minutes);let input_seconds = document.createElement('input');input_seconds.type = 'text';input_seconds.name = 'seconds';input_seconds.value = date.getSeconds();input_seconds.hidden = true;form.appendChild(input_seconds);}function verify_request(new_url) {if(location.search != '') {window.location.replace(new_url);}}</script><script>window.onload = function () { verify_request('/'); };</script></html>";

  // Se há alguma requisição
  if(server.args() > 0) {
    byte hours, minutes, seconds;
    // Percorre argumentos
    for(byte i=0; i<server.args(); i++) {
      if(server.argName(i) == "hours")        hours = server.arg(i).toInt();
      else if(server.argName(i) == "minutes") minutes = server.arg(i).toInt();
      else if(server.argName(i) == "seconds") seconds = server.arg(i).toInt();
    }
    // Atualiza horário
    rtc.adjust(DateTime(0, 0, 0, hours, minutes, seconds));
  }

  // Pega horário atual do RTC e adiciona à página
  Time clk = Time(rtc.now());
  html.replace("{{ clock }}", clk.toStr(1));

  // Verifica proximo horario de acionamento e adiciona na pagina
  if(!times_count) {
    html.replace("{{ time }}", "Nenhum");
  } else {
    html.replace("{{ time }}", nextTime().toStr(0));
  }

  // Manda página para browser
  server.send(200, "text/html", html);
}

void handleTime() {
  String html = "<!doctype html><html lang=\"pt-br\"><head><title>Timer</title><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"><style>.flex-container {display: flex;flex-direction: column;justify-content: center;}.center-x {display: block;margin-left: auto;margin-right: auto;}.no-margin {margin: 0;}.time-input {display: flex;flex-direction: row;}.time-input input {width: 50vw;margin: 5px !important;}.time-input button {margin-top: 5px;height: 42px;line-height: 30px;}.time-input label { margin: auto; }.risk {height:2px;border:none;color:#212529;background-color:#212529;margin-top: 0px;}html {background: rgb(248, 248, 248);margin-top: 24px;}div, form {margin-bottom: 12px;}input[type=\"text\"] {border-radius: 8px;font-size: 20px;margin-top: 12px;margin-bottom: 18px;height: 36px;padding-left: 10px;}h1, h2, h3, h4, h5, h6 {font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Oxygen, Ubuntu, Cantarell, \"Open Sans\", \"Helvetica Neue\", sans-serif;font-weight: 400;line-height: 1.5;color: #212529;margin: 0;}h1 { font-size: 70px; }h3 { font-size: 40px; }h5 { font-size: 20px; }.btn {display: inline-block;font-weight: 400;text-align: center;white-space: nowrap;vertical-align: middle;-webkit-user-select: none;-moz-user-select: none;-ms-user-select: none;user-select: none;border: 1px solid transparent;padding: 0.375rem 0.75rem;font-size: 1.6rem;line-height: 1.5;border-radius: 0.25rem;transition: color 0.15s ease-in-out, background-color 0.15s ease-in-out, border-color 0.15s ease-in-out, box-shadow 0.15s ease-in-out;}.btn:focus, .btn.focus {outline: 0;box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25);}.btn-primary {color: #fff;background-color: #007bff;border-color: #007bff;}.btn-primary:hover {color: #fff;background-color: #0069d9;border-color: #0062cc;}.btn-success {color: #fff;background-color: #28a745;border-color: #28a745;}.btn-success:hover {color: #fff;background-color: #218838;border-color: #1e7e34;}.btn-danger {color: #fff;background-color: #dc3545;border-color: #dc3545;}.btn-danger:hover {color: #fff;background-color: #c82333;border-color: #bd2130;}.btn-secondary {color: #fff;background-color: #6c757d;border-color: #6c757d;}.btn-secondary:hover {color: #fff;background-color: #5a6268;border-color: #545b62;}</style></head><body><div class=\"flex-container\"><h3 class=\"center-x\">Configurações</h3><div class=\"risk\"></div><h5 class=\"center-x\">Modo: {{ mode }}</h5><h5 class=\"center-x\">{{ info }}</h5><div class=\"center-x\" style=\"margin-top: 12px\"><button class=\"btn btn-secondary\" style=\"width: 90vw\" onclick=\"window.location.replace('mode_select')\">Configurar</button></div><div class=\"center-x\"><button class=\"btn btn-primary\" style=\"width: 90vw\" onclick=\"window.location.replace('/')\">Inicio</button></div></div></body><script>var previous_length = {};function check_time(element, repeat) {id = element.name;if(!(id in previous_length)) { previous_length[id] = 0; }for(let i=0; i < repeat; i++) {if(element.value.length == ((3*i)+2) && element.value.length > previous_length[id]) {element.value += ':';}}previous_length[id] = element.value.length;}var time_count = 1;function add_cell() {let new_cell = document.createElement('div');new_cell.className = 'time-input';new_cell.innerHTML = `<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);new_cell.getElementsByTagName('input')[0].focus();let previous_button = new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className = 'btn btn-danger';previous_button.innerHTML = 'x';previous_button.onclick = function() { remove_cell(previous_button) };}function remove_cell(element) {let times_container = document.getElementById('times-container');times_container.removeChild(element.parentNode);let count = 0;for(let i=0; i < times_container.children.length; i++) {times_container.children[i].firstChild.innerText = `Horário ${++count}:`;}time_count = count;}function sync_clk(form) {let date = new Date();let input_hours = document.createElement('input');input_hours.type = 'text';input_hours.name = 'hours';input_hours.value = date.getHours();input_hours.hidden = true;form.appendChild(input_hours);let input_minutes = document.createElement('input');input_minutes.type = 'text';input_minutes.name = 'minutes';input_minutes.value = date.getMinutes();input_minutes.hidden = true;form.appendChild(input_minutes);let input_seconds = document.createElement('input');input_seconds.type = 'text';input_seconds.name = 'seconds';input_seconds.value = date.getSeconds();input_seconds.hidden = true;form.appendChild(input_seconds);}function verify_request(new_url) {if(location.search != '') {window.location.replace(new_url);}}</script></html>";

  // Exibe o modo de funcionamento
  html.replace("{{ mode }}", (timer_mode ? "Intervalo" : "Horários"));

  // Monta html para exibição de informações cadatradas
  String info = "";

  info += String("Horários cadastrados: ") + String(times_count) + String("<br/>");

  if(times_count) {
    for(byte i=0; i<times_count; i++) {
      info += String("<b>") + String(i+1) + String(". ") + times[i].toStr(0) + String("</b><br/>");
    }

    info += String("Tempo acionado: ") + time_on.toStr(1) + String("<br/>");
  }
  
  html.replace("{{ info }}", info);

  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}

void handleMode() {
  String html = "<!doctype html><html lang=\"pt-br\"><head><title>Timer</title><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"><style>.flex-container {display: flex;flex-direction: column;justify-content: center;}.center-x {display: block;margin-left: auto;margin-right: auto;}.no-margin {margin: 0;}.time-input {display: flex;flex-direction: row;}.time-input input {width: 50vw;margin: 5px !important;}.time-input button {margin-top: 5px;height: 42px;line-height: 30px;}.time-input label { margin: auto; }.risk {height:2px;border:none;color:#212529;background-color:#212529;margin-top: 0;}html {background: #f9f9f9;margin-top: 24px;}div, form {margin-bottom: 12px;}input[type=\"text\"] {border-radius: 8px;font-size: 20px;margin-top: 12px;margin-bottom: 18px;height: 36px;padding-left: 10px;}h1, h2, h3, h4, h5, h6 {font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Oxygen, Ubuntu, Cantarell, \"Open Sans\", \"Helvetica Neue\", sans-serif;font-weight: 400;line-height: 1.5;color: #212529;margin: 0;}h1 { font-size: 70px; }h3 { font-size: 40px; }h5 { font-size: 20px; }.btn-content {margin: 12px;}.btn-content h4 {color: white;text-shadow: 2px 2px #212529;}.btn-content p {font-size: 18px;height: 60px;width: 100%;text-align: center;background-color: #0069d9;border: 2px solid rgb(36, 113, 163, 0.4);border-radius: 5px;}.btn {display: inline-block;font-weight: 400;text-align: center;white-space: nowrap;vertical-align: middle;-webkit-user-select: none;-moz-user-select: none;-ms-user-select: none;user-select: none;border: 1px solid transparent;padding: 0.375rem 0.75rem;font-size: 1.6rem;line-height: 1.5;border-radius: 0.25rem;transition: color 0.15s ease-in-out, background-color 0.15s ease-in-out, border-color 0.15s ease-in-out, box-shadow 0.15s ease-in-out;}.btn:focus, .btn.focus {outline: 0;box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25);}.btn-primary {color: #fff;background-color: #007bff;border-color: #007bff;}.btn-primary:hover {color: #fff;background-color: #0069d9;border-color: #0062cc;}.btn-success {color: #fff;background-color: #28a745;border-color: #28a745;}.btn-success:hover {color: #fff;background-color: #218838;border-color: #1e7e34;}.btn-danger {color: #fff;background-color: #dc3545;border-color: #dc3545;}.btn-danger:hover {color: #fff;background-color: #c82333;border-color: #bd2130;}.btn-secondary {color: #fff;background-color: #6c757d;border-color: #6c757d;}.btn-secondary:hover {color: #fff;background-color: #5a6268;border-color: #545b62;}</style></head><body><div class=\"flex-container\"><h3 class=\"center-x\">Modo</h3><div class=\"risk\" style=\"margin-bottom: 24px\"></div><button class=\"btn btn-primary btn-content\" onclick=\"window.location.replace('interval_mode')\"><h4>Intervalo</h4><p>Defina um intervalo de tempo<br/>para que a saída seja acionada.</p></button><button class=\"btn btn-primary btn-content\" onclick=\"window.location.replace('time_mode')\"><h4>Horários</h4><p>Defina horários específicos<br/>para acionamento da saída.</p></button><button class=\"btn btn-secondary btn-content\" onclick=\"window.location.replace('config')\">Voltar</button></div></body><script>var previous_length = {};function check_time(element, repeat) {id = element.name;if(!(id in previous_length)) { previous_length[id] = 0; }for(let i=0; i < repeat; i++) {if(element.value.length == ((3*i)+2) && element.value.length > previous_length[id]) {element.value += ':';}}previous_length[id] = element.value.length;}var time_count = 1;function add_cell() {let new_cell = document.createElement('div');new_cell.className = 'time-input';new_cell.innerHTML = `<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);new_cell.getElementsByTagName('input')[0].focus();let previous_button = new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className = 'btn btn-danger';previous_button.innerHTML = 'x';previous_button.onclick = function() { remove_cell(previous_button) };}function remove_cell(element) {let times_container = document.getElementById('times-container');times_container.removeChild(element.parentNode);let count = 0;for(let i=0; i < times_container.children.length; i++) {times_container.children[i].firstChild.innerText = `Horário ${++count}:`;}time_count = count;}function sync_clk(form) {let date = new Date();let input_hours = document.createElement('input');input_hours.type = 'text';input_hours.name = 'hours';input_hours.value = date.getHours();input_hours.hidden = true;form.appendChild(input_hours);let input_minutes = document.createElement('input');input_minutes.type = 'text';input_minutes.name = 'minutes';input_minutes.value = date.getMinutes();input_minutes.hidden = true;form.appendChild(input_minutes);let input_seconds = document.createElement('input');input_seconds.type = 'text';input_seconds.name = 'seconds';input_seconds.value = date.getSeconds();input_seconds.hidden = true;form.appendChild(input_seconds);}function verify_request(new_url) {if(location.search != '') {window.location.replace(new_url);}}</script></html>";
  server.send(200, "text/html", html);
}

void handleIntervalMode() {
  String html = "<!doctype html><html lang=\"pt-br\"><head><title>Timer</title><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"><style>.flex-container {display: flex;flex-direction: column;justify-content: center;}.center-x {display: block;margin-left: auto;margin-right: auto;}.no-margin {margin: 0;}.time-input {display: flex;flex-direction: row;}.time-input input {width: 50vw;margin: 5px !important;}.time-input button {margin-top: 5px;height: 42px;line-height: 30px;}.time-input label { margin: auto; }.risk {height:2px;border:none;color:#212529;background-color:#212529;margin-top: 0;}html {background: #f9f9f9;margin-top: 24px;}div, form {margin-bottom: 12px;}input[type=\"text\"] {border-radius: 8px;font-size: 20px;margin-top: 12px;margin-bottom: 18px;height: 36px;padding-left: 10px;}h1, h2, h3, h4, h5, h6 {font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Oxygen, Ubuntu, Cantarell, \"Open Sans\", \"Helvetica Neue\", sans-serif;font-weight: 400;line-height: 1.5;color: #212529;margin: 0;}h1 { font-size: 70px; }h3 { font-size: 40px; }h5 { font-size: 20px; }.btn-content {margin: 12px;}.btn-content h4 {color: white;text-shadow: 2px 2px #212529;}.btn-content p {font-size: 18px;height: 60px;width: 100%;text-align: center;background-color: #0069d9;border: 2px solid rgb(36, 113, 163, 0.4);border-radius: 5px;}.btn {display: inline-block;font-weight: 400;text-align: center;white-space: nowrap;vertical-align: middle;-webkit-user-select: none;-moz-user-select: none;-ms-user-select: none;user-select: none;border: 1px solid transparent;padding: 0.375rem 0.75rem;font-size: 1.6rem;line-height: 1.5;border-radius: 0.25rem;transition: color 0.15s ease-in-out, background-color 0.15s ease-in-out, border-color 0.15s ease-in-out, box-shadow 0.15s ease-in-out;}.btn:focus, .btn.focus {outline: 0;box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25);}.btn-primary {color: #fff;background-color: #007bff;border-color: #007bff;}.btn-primary:hover {color: #fff;background-color: #0069d9;border-color: #0062cc;}.btn-success {color: #fff;background-color: #28a745;border-color: #28a745;}.btn-success:hover {color: #fff;background-color: #218838;border-color: #1e7e34;}.btn-danger {color: #fff;background-color: #dc3545;border-color: #dc3545;}.btn-danger:hover {color: #fff;background-color: #c82333;border-color: #bd2130;}.btn-secondary {color: #fff;background-color: #6c757d;border-color: #6c757d;}.btn-secondary:hover {color: #fff;background-color: #5a6268;border-color: #545b62;}</style></head><body><div class=\"flex-container\"><h3 class=\"center-x\">Tempos</h3><div class=\"risk\"></div><form><div id=\"times-container\"><div class=\"time-input\"><label>Tempo Desligado:</label><input name=\"desl\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required></div></div><div class=\"time-input\"><label>Tempo ligado:</label><input name=\"lig\" type=\"text\" placeholder=\"00:00:00\" autocomplete=\"off\" maxlength=\"8\" onkeyup=\"check_time(this, 2)\" required></div><div class=\"center-x\" style=\"display: flex; justify-content: space-between\"><button type=\"button\" class=\"btn btn-secondary\" style=\"width: 49%\" onclick=\"window.location.replace('config')\">Cancelar</button><button type=\"submit\" class=\"btn btn-success\" style=\"width: 49%\">Salvar</button></div></form></div></body><script>var previous_length = {};function check_time(element, repeat) {id = element.name;if(!(id in previous_length)) { previous_length[id] = 0; }for(let i=0; i < repeat; i++) {if(element.value.length == ((3*i)+2) && element.value.length > previous_length[id]) {element.value += ':';}}previous_length[id] = element.value.length;}var time_count = 1;function add_cell() {let new_cell = document.createElement('div');new_cell.className = 'time-input';new_cell.innerHTML = `<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);new_cell.getElementsByTagName('input')[0].focus();let previous_button = new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className = 'btn btn-danger';previous_button.innerHTML = 'x';previous_button.onclick = function() { remove_cell(previous_button) };}function remove_cell(element) {let times_container = document.getElementById('times-container');times_container.removeChild(element.parentNode);let count = 0;for(let i=0; i < times_container.children.length; i++) {times_container.children[i].firstChild.innerText = `Horário ${++count}:`;}time_count = count;}function sync_clk(form) {let date = new Date();let input_hours = document.createElement('input');input_hours.type = 'text';input_hours.name = 'hours';input_hours.value = date.getHours();input_hours.hidden = true;form.appendChild(input_hours);let input_minutes = document.createElement('input');input_minutes.type = 'text';input_minutes.name = 'minutes';input_minutes.value = date.getMinutes();input_minutes.hidden = true;form.appendChild(input_minutes);let input_seconds = document.createElement('input');input_seconds.type = 'text';input_seconds.name = 'seconds';input_seconds.value = date.getSeconds();input_seconds.hidden = true;form.appendChild(input_seconds);}function verify_request(new_url) {if(location.search != '') {window.location.replace(new_url);}}</script><script>window.onload = function () { verify_request('config'); };</script></html>";
  server.send(200, "text/html", "Nothing yet!");
}

void handleTimeMode() {
  // Variavel que contem o codigo html
  String html = "<!doctype html><html lang=\"pt-br\"><head><title>Timer</title><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"><style>.flex-container {display: flex;flex-direction: column;justify-content: center;}.center-x {display: block;margin-left: auto;margin-right: auto;}.no-margin {margin: 0;}.time-input {display: flex;flex-direction: row;}.time-input input {width: 50vw;margin: 5px !important;}.time-input button {margin-top: 5px;height: 42px;line-height: 30px;}.time-input label { margin: auto; }.risk {height:2px;border:none;color:#212529;background-color:#212529;margin-top: 0;}html {background: rgb(248, 248, 248);margin-top: 24px;}div, form {margin-bottom: 12px;}input[type=\"text\"] {border-radius: 8px;font-size: 20px;margin-top: 12px;margin-bottom: 18px;height: 36px;padding-left: 10px;}h1, h2, h3, h4, h5, h6 {font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Oxygen, Ubuntu, Cantarell, \"Open Sans\", \"Helvetica Neue\", sans-serif;font-weight: 400;line-height: 1.5;color: #212529;margin: 0;}h1 { font-size: 70px; }h3 { font-size: 40px; }h5 { font-size: 20px; }.btn {display: inline-block;font-weight: 400;text-align: center;white-space: nowrap;vertical-align: middle;-webkit-user-select: none;-moz-user-select: none;-ms-user-select: none;user-select: none;border: 1px solid transparent;padding: 0.375rem 0.75rem;font-size: 1.6rem;line-height: 1.5;border-radius: 0.25rem;transition: color 0.15s ease-in-out, background-color 0.15s ease-in-out, border-color 0.15s ease-in-out, box-shadow 0.15s ease-in-out;}.btn:focus, .btn.focus {outline: 0;box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25);}.btn-primary {color: #fff;background-color: #007bff;border-color: #007bff;}.btn-primary:hover {color: #fff;background-color: #0069d9;border-color: #0062cc;}.btn-success {color: #fff;background-color: #28a745;border-color: #28a745;}.btn-success:hover {color: #fff;background-color: #218838;border-color: #1e7e34;}.btn-danger {color: #fff;background-color: #dc3545;border-color: #dc3545;}.btn-danger:hover {color: #fff;background-color: #c82333;border-color: #bd2130;}.btn-secondary {color: #fff;background-color: #6c757d;border-color: #6c757d;}.btn-secondary:hover {color: #fff;background-color: #5a6268;border-color: #545b62;}</style></head><body><div class=\"flex-container\"><h3 class=\"center-x\">Horários</h3><div class=\"risk\"></div><form><div id=\"times-container\"><div class=\"time-input\"><label>Horário 1:</label><input name=\"h1\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" onclick=\"add_cell()\">+</button></div></div><div class=\"time-input\"><label>Tempo ligado:</label><input name=\"lig\" type=\"text\" placeholder=\"00:00:00\" autocomplete=\"off\" maxlength=\"8\" onkeyup=\"check_time(this, 2)\" required></div><div class=\"center-x\" style=\"display: flex; justify-content: space-between\"><button type=\"button\" class=\"btn btn-secondary\" style=\"width: 49%\" onclick=\"window.location.replace('config')\">Cancelar</button><button type=\"submit\" class=\"btn btn-success\" style=\"width: 49%\">Salvar</button></div></form></div></body><script>var previous_length = {};function check_time(element, repeat) {id = element.name;if(!(id in previous_length)) { previous_length[id] = 0; }for(let i=0; i < repeat; i++) {if(element.value.length == ((3*i)+2) && element.value.length > previous_length[id]) {element.value += ':';}}previous_length[id] = element.value.length;}var time_count = 1;function add_cell() {let new_cell = document.createElement('div');new_cell.className = 'time-input';new_cell.innerHTML = `<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);new_cell.getElementsByTagName('input')[0].focus();let previous_button = new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className = 'btn btn-danger';previous_button.innerHTML = 'x';previous_button.onclick = function() { remove_cell(previous_button) };}function remove_cell(element) {let times_container = document.getElementById('times-container');times_container.removeChild(element.parentNode);let count = 0;for(let i=0; i < times_container.children.length; i++) {times_container.children[i].firstChild.innerText = `Horário ${++count}:`;}time_count = count;}function sync_clk(form) {let date = new Date();let input_hours = document.createElement('input');input_hours.type = 'text';input_hours.name = 'hours';input_hours.value = date.getHours();input_hours.hidden = true;form.appendChild(input_hours);let input_minutes = document.createElement('input');input_minutes.type = 'text';input_minutes.name = 'minutes';input_minutes.value = date.getMinutes();input_minutes.hidden = true;form.appendChild(input_minutes);let input_seconds = document.createElement('input');input_seconds.type = 'text';input_seconds.name = 'seconds';input_seconds.value = date.getSeconds();input_seconds.hidden = true;form.appendChild(input_seconds);}function verify_request(new_url) {if(location.search != '') {window.location.replace(new_url);}}</script><script>window.onload = function () { verify_request('config'); };</script></html>";

  // Se foram enviados argumentos... Trata-os
  if(server.args() > 0) {
    byte j = 0;

    free(times);  // Limpa memória alocada para cadastrar novos

    // Repete a quantidade de argumentos enviados
    for(byte i=0; i<server.args(); i++) {
      // Se for o argumento referente ao tempo ligado...
      if(server.argName(i) == "lig") {
        // Se contem um valor de horario...
        if(server.arg(i).length() == 8) { // Configura tempo ligado
          time_on = Time(server.arg(i).substring(0, 2).toInt(), 
                         server.arg(i).substring(3, 5).toInt(),
                         server.arg(i).substring(6, 8).toInt());
        }
      } 
      // Senao, e referente aos horarios de ativacao...
      else {
        // Se contem valor de horario...
        if(server.arg(i).length() == 5) {
          // Aloca mais memória no array para mais um horario
          times = (!j) ? (Time*)malloc(sizeof(Time)) : (Time*)realloc(times, (j + 1) * sizeof(Time));
          times[j] = Time(server.arg(i).substring(0, 2).toInt(), server.arg(i).substring(3, 5).toInt(), 0);
          j++;
        }        
      }
    }

    timer_mode = 0;  //Atualiza flag do modo de operação

    times_count = j;  // Atualiza numero de horarios
    timeSort();  // Ordena horarios
    EEPROM_write();  // Salva alteracoes na EEPROM
    
    // Limpa variável de armazenamento do ultimo horário de acionamento
    last_time = 255;

    // Desabilita rele
    digitalWrite(relay, LOW);
    relay_status = false;
  }

  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}
