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
#define TIMES_COUNT    1
#define TIMER_MODE     2
#define TIME_ON_HOUR   3
#define TIME_ON_MINUTE 4
#define TIME_ON_SECOND 5
#define TIMES_ADDR_INI 6

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

ESP8266WebServer server(80);  // Define porta 

// --- Estrutura dos horarios de acionamento ---
typedef struct Time {
  byte hour = 0;
  byte minute = 0;
  byte second = 0;

  void set(byte h, byte m, byte s = 0) {
    hour = h;
    minute = m;
    second = s;
  }

  void set(DateTime time) {
    hour = time.hour();
    minute = time.minute();
    second = time.second();
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

  bool operator == (const Time& other) {
    if(hour == other.hour && minute == other.minute && second == other.second)
      return true;
    else
      return false;
  }

  bool operator > (const Time& other) {
    if(hour != other.hour) {
      return (hour > other.hour ? true : false);
    } else {
      if(minute != other.minute) {
        return (minute > other.minute ? true : false);
      } else {
        if(second != other.second) {
          return (second > other.second ? true : false);
        } else {
          return false;
        }
      }
    }
  }

  Time operator + (const Time& other) {
    Time sum;
    sum.set(hour + other.hour, minute + other.minute, second + other.second);
    return sum;
  }

  unsigned long toSeconds() {
    return ((hour * 3600) + (minute * 60) + second);
  }

  String toStr(bool seconds = true) {
    String times_str[] = {String(hour), String(minute), String(second)};

    for(int i=0; i<3; i++) {
      // Adiciona zeros se preciso (9:5 -> 09:05)
      if(times_str[i].length() < 2) {
        times_str[i] = '0' + times_str[i];
      }
    }
    
    if(seconds) {
      return (times_str[0] + ':' + times_str[1] + ':' + times_str[2]); 
    } else {
      return (times_str[0] + ':' + times_str[1]); 
    }
  }
} Time; 

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

bool timesCheck();    // Funcao que confere se esta no horario de acionar saida
void timeSort();      // Funcao para ordenar horarios
Time nextTime();      // Funcao que retorna proximo horario de acionamento

void wifiBegin();     // Funcao que liga AccessPoint
void wifiSleep();     // Funcao que desliga AccessPoint

void handleRoot();    // Funcao da pagina HTML principal
void handleTime();    // Funcao da pagina HTML de visualizacao dos horarios
void handleConfig();  // Funcao da pagina HTML de configuracao dos horarios de acionamento

// --- MAIN ---
void setup() {
  // Espera 1s para estabilizacao
	delay(1000);

  // Configura IO's
  pinMode(button, INPUT);
  pinMode(relay, OUTPUT);

  // Desabilita AccessPoint
  wifiBegin(); //wifiSleep();
  wifi_status = true;

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
    static Time time_buf, now;  // Variavel que ira conter o instante de desacionamento da saida
    now.set(rtc.now());

    // Se relay desligado...
    if(!relay_status) {
      // Confere se esta no horario de acionar saida
      if(timesCheck()) {
        digitalWrite(relay, HIGH);  // Aciona saida
        time_buf = now + time_on;  // Armazena horario de desacionamento da saida
        relay_status = true;  // Atualiza Flag de estado do relay
      }
    }
    else { // Senao...
      if(now > time_buf) {
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

  EEPROM.write(TIMES_COUNT, times_count);

  EEPROM.write(TIMER_MODE, timer_mode);

  byte j = TIMES_ADDR_INI;
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

  times_count = EEPROM.read(TIMES_COUNT);

  timer_mode = EEPROM.read(TIMER_MODE);

  times = (Time*)malloc(times_count * sizeof(Time));

  byte j = TIMES_ADDR_INI;
  for(byte i=0; i<times_count; i++) {
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

void timeSort() {
  // Compara e ordena horarios (Crescente)
  for(byte i=0; i<times_count-1; i++) {
    byte small = i;
    for(byte j=i+1; j<times_count; j++) {
      if(times[small].isBiggest(times[j])) {
        small = j;
      }
    }
    if(!times[i].cmp(times[small])) {
      Time buff = times[i];
      times[i] = times[small];
      times[small] = buff;
    }
  }
}

Time nextTime() {
  // Se modo 'horarios'
  if(!timer_mode) {
    if(last_time == 255 || last_time == times_count - 1) {
      return times[0];
    } else {
      return times[last_time+1];
    }
  }
}

void wifiBegin() {
  WiFi.forceSleepWake();  // Forca re-ligamento do WIFI
  delay(10);              // Delay para estabilizacao

  // Inicia AccessPoint
  WiFi.softAP(ssid, password);

  // Obtem IP para acesso (IP: 192.168.4.1)
  IPAddress myIP = WiFi.softAPIP();
  
  // Configura rotas
  server.on("/", handleRoot);
  server.on("/config", handleTime);
  server.on("/configTime" , handleConfig);

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
  String html ="<!doctype html> <html lang=\"pt-br\"> <head> <title>Timer</title> <meta charset=\"utf-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"> <meta http-equiv=\"refresh\" content=\"1\"> <style>.flex-container{display:flex;flex-direction:column;justify-content:center}.center-x{display:block;margin-left:auto;margin-right:auto}.no-margin{margin:0}.time-input{display:flex;flex-direction:row}.time-input input{width:55%;margin:5px!important}.time-input button{margin-top:5px;height:42px;line-height:30px}.time-input label{margin:auto}.risk{height:2px;border:none;color:#212529;background-color:#212529;margin-top:0}html{background:rgb(248,248,248);margin-top:64px}div{margin-bottom:12px}input[type=\"text\"]{border-radius:8px;font-size:20px;margin-top:12px;margin-bottom:18px;height:36px;padding-left:10px}h1,h2,h3,h4,h5,h6{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Oxygen,Ubuntu,Cantarell,\"Open Sans\",\"Helvetica Neue\",sans-serif;font-weight:400;line-height:1.5;color:#212529;margin:0}h1{font-size:70px}h3{font-size:40px}h5{font-size:20px}.btn{display:inline-block;font-weight:400;text-align:center;white-space:nowrap;vertical-align:middle;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;padding:.375rem .75rem;font-size:1.6rem;line-height:1.5;border-radius:.25rem;transition:color 0.15s ease-in-out,background-color 0.15s ease-in-out,border-color 0.15s ease-in-out,box-shadow 0.15s ease-in-out}.btn:focus,.btn.focus{outline:0;box-shadow:0 0 0 .2rem rgba(0,123,255,.25)}.btn-primary{color:#fff;background-color:#007bff;border-color:#007bff}.btn-primary:hover{color:#fff;background-color:#0069d9;border-color:#0062cc}.btn-success{color:#fff;background-color:#28a745;border-color:#28a745}.btn-success:hover{color:#fff;background-color:#218838;border-color:#1e7e34}.btn-danger{color:#fff;background-color:#dc3545;border-color:#dc3545}.btn-danger:hover{color:#fff;background-color:#c82333;border-color:#bd2130}.btn-secondary{color:#fff;background-color:#6c757d;border-color:#6c757d}.btn-secondary:hover{color:#fff;background-color:#5a6268;border-color:#545b62}</style> </head> <body> <div class=\"flex-container\"> <div class=\"center-x\"> <h1 >Timer</h1> </div> <div class=\"risk\"></div> <div class=\"center-x\"> <h3>{{ clock }}</h3> </div> <div class=\"center-x\"> <h5>Prox. Horário: {{ time }}</h5> </div> <div class=\"center-x\" style=\"margin-top: 12px\"> <button class=\"btn btn-primary\" style=\"width: 90vw; height: 100px\" onclick=\"window.location.replace('config')\">Configurações</button> </div> <form class=\"center-x\" onsubmit=\"sync_clk(this)\"> <div> <button type=\"submit\" class=\"btn btn-secondary\" style=\"width: 90vw\">Sincronizar relógio</button> </div> </form> </div> </body> <script>var previous_length={};function check_time(element,repeat){id=element.name;if(!(id in previous_length)){previous_length[id]=0}for(let i=0;i<repeat;i++){if(element.value.length==((3*i)+2)&&element.value.length>previous_length[id]){element.value+=':'}}previous_length[id]=element.value.length}var time_count=1;function add_cell(){let new_cell=document.createElement('div');new_cell.className='time-input';new_cell.innerHTML=`<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);let previous_button=new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className='btn btn-danger';previous_button.innerHTML='x';previous_button.onclick=function(){remove_cell(previous_button)}}function remove_cell(element){let times_container=document.getElementById('times-container');times_container.removeChild(element.parentNode);let count=0;for(let i=0;i<times_container.children.length;i++){times_container.children[i].firstChild.innerText=`Horário ${++count}:`}time_count=count}function sync_clk(form){let date=new Date();let input_hours=document.createElement('input');input_hours.type='text';input_hours.name='hours';input_hours.value=date.getHours();input_hours.hidden=!0;form.appendChild(input_hours);let input_minutes=document.createElement('input');input_minutes.type='text';input_minutes.name='minutes';input_minutes.value=date.getMinutes();input_minutes.hidden=!0;form.appendChild(input_minutes);let input_seconds=document.createElement('input');input_seconds.type='text';input_seconds.name='seconds';input_seconds.value=date.getSeconds();input_seconds.hidden=!0;form.appendChild(input_seconds)}function verify_request(new_url){if(location.search!=''){window.location.replace(new_url)}}</script> <script>window.onload = function () { verify_request('/'); };</script> </html>";

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
  Time clk;
  DateTime now = rtc.now();
  clk.set(now.hour(), now.minute(), now.second());
  html.replace("{{ clock }}", clk.toStr());

  // Verifica proximo horario de acionamento e adiciona na pagina
  if(!times_count) {
    html.replace("{{ time }}", "Nenhum");
  } else {
  html.replace("{{ time }}", nextTime().toStr(false));
  }

  // Manda página para browser
  server.send(200, "text/html", html);
}

void handleTime() {
  String html ="<!doctype html> <html lang=\"pt-br\"> <head> <title>Timer</title> <meta charset=\"utf-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"> <style>.flex-container{display:flex;flex-direction:column;justify-content:center}.center-x{display:block;margin-left:auto;margin-right:auto}.no-margin{margin:0}.time-input{display:flex;flex-direction:row}.time-input input{width:55%;margin:5px!important}.time-input button{margin-top:5px;height:42px;line-height:30px}.time-input label{margin:auto}.risk{height:2px;border:none;color:#212529;background-color:#212529;margin-top:0}html{background:rgb(248,248,248);margin-top:64px}div{margin-bottom:12px}input[type=\"text\"]{border-radius:8px;font-size:20px;margin-top:12px;margin-bottom:18px;height:36px;padding-left:10px}h1,h2,h3,h4,h5,h6{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Oxygen,Ubuntu,Cantarell,\"Open Sans\",\"Helvetica Neue\",sans-serif;font-weight:400;line-height:1.5;color:#212529;margin:0}h1{font-size:70px}h3{font-size:40px}h5{font-size:20px}.btn{display:inline-block;font-weight:400;text-align:center;white-space:nowrap;vertical-align:middle;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;padding:.375rem .75rem;font-size:1.6rem;line-height:1.5;border-radius:.25rem;transition:color 0.15s ease-in-out,background-color 0.15s ease-in-out,border-color 0.15s ease-in-out,box-shadow 0.15s ease-in-out}.btn:focus,.btn.focus{outline:0;box-shadow:0 0 0 .2rem rgba(0,123,255,.25)}.btn-primary{color:#fff;background-color:#007bff;border-color:#007bff}.btn-primary:hover{color:#fff;background-color:#0069d9;border-color:#0062cc}.btn-success{color:#fff;background-color:#28a745;border-color:#28a745}.btn-success:hover{color:#fff;background-color:#218838;border-color:#1e7e34}.btn-danger{color:#fff;background-color:#dc3545;border-color:#dc3545}.btn-danger:hover{color:#fff;background-color:#c82333;border-color:#bd2130}.btn-secondary{color:#fff;background-color:#6c757d;border-color:#6c757d}.btn-secondary:hover{color:#fff;background-color:#5a6268;border-color:#545b62}</style> </head> <body> <div class=\"flex-container\"> <h3 class=\"center-x\">Configurações</h3> <div class=\"risk\"></div> <h5 class=\"center-x\">Modo: {{ mode }}</h5> <h5 class=\"center-x\">{{ info }}</h5> <div class=\"center-x\" style=\"margin-top: 12px\"> <button class=\"btn btn-secondary\" style=\"width: 90vw\" onclick=\"window.location.replace('configTime')\">Configurar</button> </div> <div class=\"center-x\"> <button class=\"btn btn-primary\" style=\"width: 90vw\" onclick=\"window.location.replace('/')\">Inicio</button> </div> </div> </body> <script>var previous_length={};function check_time(element,repeat){id=element.name;if(!(id in previous_length)){previous_length[id]=0}for(let i=0;i<repeat;i++){if(element.value.length==((3*i)+2)&&element.value.length>previous_length[id]){element.value+=':'}}previous_length[id]=element.value.length}var time_count=1;function add_cell(){let new_cell=document.createElement('div');new_cell.className='time-input';new_cell.innerHTML=`<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);let previous_button=new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className='btn btn-danger';previous_button.innerHTML='x';previous_button.onclick=function(){remove_cell(previous_button)}}function remove_cell(element){let times_container=document.getElementById('times-container');times_container.removeChild(element.parentNode);let count=0;for(let i=0;i<times_container.children.length;i++){times_container.children[i].firstChild.innerText=`Horário ${++count}:`}time_count=count}function sync_clk(form){let date=new Date();let input_hours=document.createElement('input');input_hours.type='text';input_hours.name='hours';input_hours.value=date.getHours();input_hours.hidden=!0;form.appendChild(input_hours);let input_minutes=document.createElement('input');input_minutes.type='text';input_minutes.name='minutes';input_minutes.value=date.getMinutes();input_minutes.hidden=!0;form.appendChild(input_minutes);let input_seconds=document.createElement('input');input_seconds.type='text';input_seconds.name='seconds';input_seconds.value=date.getSeconds();input_seconds.hidden=!0;form.appendChild(input_seconds)}function verify_request(new_url){if(location.search!=''){window.location.replace(new_url)}}</script> </html>";

  // Se houve uma requisição para apagar os horários: seta variável de contagem, limpa variavel de horarios e escreve na memória
  if(server.args() > 0) {
    for(int i=0; i<server.args(); i++) {
      if(server.argName(i) == "del") {
        times_count = 0;
        time_on.set(0, 0, 0);
        last_time = 255;
        free(times);
        EEPROM_write();
      }
    }
  }

  // Exibe o modo de funcionamento
  html.replace("{{ mode }}", (timer_mode ? "Intervalo" : "Horários"));

  // Monta html para exibição de informações cadatradas
  String info = "";
  info += String("Horários cadastrados: ") + String(times_count) + String("<br/>");

  for(int i=0; i<times_count; i++) {
    info += String(i+1) + String(". ");
    info += times[i].toStr(false);
    info += String("<br/>");
  }

  info += String("Tempo acionado: ") + time_on.toStr() + String("<br/>");
  
  html.replace("{{ info }}", info);

  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}

void handleConfig() {
  // Variavel que contem o codigo html
  String html ="<!doctype html> <html lang=\"pt-br\"> <head> <title>Timer</title> <meta charset=\"utf-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"> <style>.flex-container{display:flex;flex-direction:column;justify-content:center}.center-x{display:block;margin-left:auto;margin-right:auto}.no-margin{margin:0}.time-input{display:flex;flex-direction:row}.time-input input{width:55%;margin:5px!important}.time-input button{margin-top:5px;height:42px;line-height:30px}.time-input label{margin:auto}.risk{height:2px;border:none;color:#212529;background-color:#212529;margin-top:0}html{background:rgb(248,248,248);margin-top:64px}div{margin-bottom:12px}input[type=\"text\"]{border-radius:8px;font-size:20px;margin-top:12px;margin-bottom:18px;height:36px;padding-left:10px}h1,h2,h3,h4,h5,h6{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Oxygen,Ubuntu,Cantarell,\"Open Sans\",\"Helvetica Neue\",sans-serif;font-weight:400;line-height:1.5;color:#212529;margin:0}h1{font-size:70px}h3{font-size:40px}h5{font-size:20px}.btn{display:inline-block;font-weight:400;text-align:center;white-space:nowrap;vertical-align:middle;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;padding:.375rem .75rem;font-size:1.6rem;line-height:1.5;border-radius:.25rem;transition:color 0.15s ease-in-out,background-color 0.15s ease-in-out,border-color 0.15s ease-in-out,box-shadow 0.15s ease-in-out}.btn:focus,.btn.focus{outline:0;box-shadow:0 0 0 .2rem rgba(0,123,255,.25)}.btn-primary{color:#fff;background-color:#007bff;border-color:#007bff}.btn-primary:hover{color:#fff;background-color:#0069d9;border-color:#0062cc}.btn-success{color:#fff;background-color:#28a745;border-color:#28a745}.btn-success:hover{color:#fff;background-color:#218838;border-color:#1e7e34}.btn-danger{color:#fff;background-color:#dc3545;border-color:#dc3545}.btn-danger:hover{color:#fff;background-color:#c82333;border-color:#bd2130}.btn-secondary{color:#fff;background-color:#6c757d;border-color:#6c757d}.btn-secondary:hover{color:#fff;background-color:#5a6268;border-color:#545b62}</style> </head> <body> <div class=\"flex-container\"> <h3 class=\"center-x\">Horários</h3> <div class=\"risk\"></div> <form id=\"test\"> <div id=\"times-container\"> <div class=\"time-input\"> <label>Horário 1:</label> <input name=\"h1\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required> <button type=\"button\" class=\"btn btn-success\" onclick=\"add_cell()\">+</button> </div> </div> <div class=\"time-input\"> <label>Tempo ligado:</label> <input name=\"lig\" type=\"text\" placeholder=\"00:00:00\" autocomplete=\"off\" maxlength=\"8\" onkeyup=\"check_time(this, 2)\" required> </div> <div class=\"center-x\" style=\"padding-bottom: 14px\"> <button type=\"button\" class=\"btn btn-secondary\" style=\"width: 49%\" onclick=\"window.location.replace('config')\">Cancelar</button> <button type=\"submit\" class=\"btn btn-success\" style=\"width: 49%\">Salvar</button> </div> </form> </div> </body> <script>var previous_length={};function check_time(element,repeat){id=element.name;if(!(id in previous_length)){previous_length[id]=0}for(let i=0;i<repeat;i++){if(element.value.length==((3*i)+2)&&element.value.length>previous_length[id]){element.value+=':'}}previous_length[id]=element.value.length}var time_count=1;function add_cell(){let new_cell=document.createElement('div');new_cell.className='time-input';new_cell.innerHTML=`<label>Horário ${++time_count}:</label><input name=\"h${time_count}\" type=\"text\" placeholder=\"00:00\" autocomplete=\"off\" maxlength=\"5\" onkeyup=\"check_time(this, 1)\" required><button type=\"button\" class=\"btn btn-success\" style=\"height: 42px\" onclick=\"add_cell()\">+</button>`;document.getElementById('times-container').appendChild(new_cell);let previous_button=new_cell.previousElementSibling.getElementsByTagName('button')[0];previous_button.className='btn btn-danger';previous_button.innerHTML='x';previous_button.onclick=function(){remove_cell(previous_button)}}function remove_cell(element){let times_container=document.getElementById('times-container');times_container.removeChild(element.parentNode);let count=0;for(let i=0;i<times_container.children.length;i++){times_container.children[i].firstChild.innerText=`Horário ${++count}:`}time_count=count}function sync_clk(form){let date=new Date();let input_hours=document.createElement('input');input_hours.type='text';input_hours.name='hours';input_hours.value=date.getHours();input_hours.hidden=!0;form.appendChild(input_hours);let input_minutes=document.createElement('input');input_minutes.type='text';input_minutes.name='minutes';input_minutes.value=date.getMinutes();input_minutes.hidden=!0;form.appendChild(input_minutes);let input_seconds=document.createElement('input');input_seconds.type='text';input_seconds.name='seconds';input_seconds.value=date.getSeconds();input_seconds.hidden=!0;form.appendChild(input_seconds)}function verify_request(new_url){if(location.search!=''){window.location.replace(new_url)}}</script> <script>window.onload = function () { verify_request('config'); };</script> </html>";

  // Se foram enviados argumentos... Trata-os
  if(server.args() > 0) {
    byte j = 0;

    // Repete a quantidade de argumentos enviados
    for(byte i=0; i<server.args(); i++) {
      // Se for o argumento referente ao tempo ligado...
      if(server.argName(i) == "lig") {
        // Se contem um valor de horario...
        if(server.arg(i).length() == 8) { // Configura tempo ligado
          time_on.set(server.arg(i).substring(0, 2).toInt(), 
                      server.arg(i).substring(3, 5).toInt(),
                      server.arg(i).substring(6, 8).toInt());
        }
      } 
      // Senao, e referente aos horarios de ativacao...
      else {
        // Se contem valor de horario...
        if(server.arg(i).length() == 5) {  // Configura horarios
          times = (!j) ? (Time*)malloc((j + 1) * sizeof(Time)) : (Time*)realloc(times, (j + 1) * sizeof(Time));
          times[j].set(server.arg(i).substring(0, 2).toInt(), 
                       server.arg(i).substring(3, 5).toInt());
          j++;
        }        
      }
    }

    times_count = j;  // Atualiza numero de horarios
    timeSort();  // Ordena horarios
    EEPROM_write();  // Salva alteracoes na EEPROM
  }

  // Limpa variável de armazenamento do ultimo horário de acionamento
  last_time = 255;

  // Manda a pagina para o usuario
  server.send(200, "text/html", html);
}
