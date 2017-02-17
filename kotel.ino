#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
//#include <Adafruit_GFX.h>    // LCD Graphics library
//#include <gfxfont.h>
#include "Adafruit_ST7735.h" // LCD
#include <SPI.h>                
#include <OneWire.h>

/*
 * ESP8266-12        HY-1.8 SPI
 * RESET to VCC
 * GPIO2             Pin 07 (A0)
 * GPIO13 (HSPID)    Pin 08 (SDA)   MOSI, DS
 * GPIO14 (HSPICLK)  Pin 09 (SCK)   SH, CLK
 * GPIO0  (HSPICS)   Pin 10 (CS)    TFT_CS
  */

#define TFT_CS      0          //чипселект дисплея
#define HC_CS       16         //чипселект 74hc959
#define DC          2          //данные дисплея
#define ENC_C       15         //кнопка энкодера
#define ENC_A       4          //A - контакт энкодера
#define ENC_B       5          //B - контакт энкодера
#define YELLOW_LED  7          //Контакт желтого светодиода 74HC
#define RED_LED     5          //Контакт красного светодиода 74HC
#define NASOS2      4          //Контакт насоса
#define NASOS1      3          //Контакт насоса
#define KOTEL3      2          //Контакт котла
#define KOTEL2      1          //Контакт котла
#define KOTEL1      0          //Контакт котла

//Пин подключен к DS входу 74HC595

#define AP_SSID                  "DV"
#define AP_PASS                  "DVbynthytn"
#define TIMEZONE 9

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  DC);


// Объявление глобальных переменных
//1. NTP переменные
unsigned int  localPort = 2390;      // local port to listen for UDP packets
unsigned long ntp_time = 0;
long  t_correct        = 0;
unsigned long cur_ms   = 0;
unsigned long ms1      = 0;
unsigned long ms2      = 10000000UL;
unsigned long t_cur    = 0;
bool          points   = true;
unsigned int err_count = 0;

IPAddress timeServerIP; 
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; 
byte packetBuffer[ NTP_PACKET_SIZE]; 
WiFiUDP udp;

//2. Переменная подключения
bool conn = false;
//3. Пины
int hc74;
//4. Температуры
float thatamin, tkotelmin, tkotel, ttt;
//5. Энкодер
unsigned long currentTime;
unsigned long loopTime;
unsigned char encoder_A;
unsigned char encoder_B;
unsigned char encoder_A_prev=0;



//___________________________________Хардварные функции__________________________
//Датчики OneWire
OneWire  ds(3); 
float getTemp(int pin){
  byte addr[8]; 
  byte data[12];  

  if (!ds.search(addr)) {
   ds.reset_search();
   delay(250);
   return 90;
  }
  if (OneWire::crc8(addr, 7) != addr[7]) {
     return 50;
  }
  ds.reset();            
  ds.select(addr);        
  ds.write(0x44);      
  delay(1000);   
  ds.reset();
  ds.select(addr);    
  ds.write(0xBE);          
  for (int i = 0; i < 9; i++) {           
    data[i] = ds.read();  
  }

  int raw = (data[1] << 8) | data[0]; 
  if (data[7] == 0x10) raw = (raw & 0xFFF0) + 12 - data[6];  
  ds.reset_search();
  return round(raw / 1.6)/10;
} 

// Соединение с WiFi
bool ConnectWiFi(const char *ssid, const char *pass) {
// Три попытки соединения по WiFi
   for( int i=0; i<3; i++){
     // Serial.print("\nConnecting to: "); 
     // Serial.println(ssid); 
      WiFi.begin(ssid,pass);
      delay(1000);
// Максиммум 12 раз проверка соединения       
      for( int j=0; j<12; j++ ){
          if (WiFi.status() == WL_CONNECTED) { 
       //       Serial.print("\nWiFi connect true: ");
       //       Serial.print(WiFi.localIP());
       //       Serial.print("/");
       //       Serial.print(WiFi.subnetMask());
       //       Serial.print("/");
       //       Serial.println(WiFi.gatewayIP());
              return true; 
          } 
          delay(1000);
        //  Serial.print(WiFi.status()); 
      }
   }   
   // Serial.println("\nConnect WiFi failed ...");
   return false;
} 

//_______________________________________Софтварные функции___________________________
String utf8rus(String source)
{
  int i,k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length(); i = 0;

  while (i < k) {
    n = source[i]; i++;

    if (n >= 0xC0) {
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x30;
          break;
        }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB8; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x70;
          break;
        }
      }
    }
    m[0] = n; target = target + String(m);
  }
return target;
}

// Посылаем и парсим запрос к NTP серверу
bool GetNTP(void) {
  WiFi.hostByName(ntpServerName, timeServerIP); 
  sendNTPpacket(timeServerIP); 
  delay(1000);
  
  int cb = udp.parsePacket();
  if (!cb) {
    // Serial.println("No packet yet");
    return false;
  }
  else {
    // Serial.print("packet received, length=");
    // Serial.println(cb);
// Читаем пакет в буфер    
    udp.read(packetBuffer, NTP_PACKET_SIZE); 
// 4 байта начиная с 40-го сождержат таймстамп времени - число секунд 
// от 01.01.1900   
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
// Конвертируем два слова в переменную long
    unsigned long secsSince1900 = highWord << 16 | lowWord;
// Конвертируем в UNIX-таймстамп (число секунд от 01.01.1970
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears;
// Делаем поправку на местную тайм-зону
    ntp_time = epoch + TIMEZONE*3600;    
    // Serial.print("Unix time = ");
    // Serial.println(ntp_time);
  }
  return true;
}

// Посылаем запрос NTP серверу на заданный адрес
unsigned long sendNTPpacket(IPAddress& address)
{
  // Serial.println("sending NTP packet...");
// Очистка буфера в 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
// Формируем строку зыпроса NTP сервера
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
// Посылаем запрос на NTP сервер (123 порт)
  udp.beginPacket(address, 123); 
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}



 
void setup() {
  thatamin=15;
  tkotel=60;
  tkotelmin=70;
  hc74=0;
  SPI.begin();
  digitalWrite(TFT_CS, HIGH);
  pinMode(ENC_C, INPUT);
  pinMode(ENC_A, INPUT);
  pinMode(ENC_B, INPUT);
  currentTime = millis();
  loopTime = currentTime;
  pinMode(HC_CS, OUTPUT);
  digitalWrite(HC_CS, LOW);   //Обнуляем сдвиговый регистр!
  SPI.transfer(hc74);            //
  digitalWrite(HC_CS, HIGH);  //
  digitalWrite(TFT_CS, LOW);
  tft.cp437(true);
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(ST7735_BLACK);
  DiagMsg("HELLO!\nInitalizing... ", ST7735_WHITE, 1, 0);
  if( !ConnectWiFi(AP_SSID,AP_PASS) ){
      conn=false;
      DiagMsg("NET WIFI!", ST7735_RED, 1, 0);
      delay(500);  
  }
  else {
      conn=true;
      DiagMsg("WIFI EST\nIP:"+String(WiFi.localIP()[0])+"."+String(WiFi.localIP()[1])+"."+String(WiFi.localIP()[2])+"."+String(WiFi.localIP()[3]), ST7735_WHITE, 1, 0);
      udp.begin(localPort);
      delay(500);
  }    
  DiagMsg("КОТЕЛ", ST7735_WHITE, 1, 51);
  DiagMsg("ДОМ", ST7735_WHITE, 64, 51);
  DiagMsg("КОТЕЛ", ST7735_WHITE, 1, 91);
  DiagMsg("ЦЕНТРОБЕЖКА", ST7735_WHITE, 1, 131);
  digitalWrite(TFT_CS, HIGH);
}
 
void loop() {
    delay(1000);
    ttt=getTemp(3);
    digitalWrite(TFT_CS, LOW);
    ShPar(ttt,1);
    digitalWrite(TFT_CS, HIGH);
    
currentTime = millis();
if(currentTime >= (loopTime + 5)){ // проверяем каждые 5мс (200 Гц)
encoder_A = digitalRead(pin_A); // считываем состояние выхода А энкодера
encoder_B = digitalRead(pin_B); // считываем состояние выхода А энкодера
if((!encoder_A) && (encoder_A_prev)){ // если состояние изменилось с положительного к нулю
if(encoder_B) { // выход В в полож. сост., значит вращение по часовой стрелке
// увеличиваем яркость, не более чем до 255
if(brightness + fadeAmount <= 255) brightness += fadeAmount; }
else { // выход В в 0 сост., значит вращение против часовой стрелки
// уменьшаем яркость, но не ниже 0
if(brightness - fadeAmount >= 0) brightness -= fadeAmount; }
}
encoder_A_prev = encoder_A; // сохраняем значение А для следующего цикла
analogWrite(9, brightness); // устанавливаем яркость на 9 ножку
loopTime = currentTime; }

/*    
    cur_ms       = millis();
    t_cur        = cur_ms/1000;
    // Каждые 60 секунд считываем время в интернете 
    if( cur_ms < ms2 || (cur_ms - ms2) > 60000 ){
       err_count++;
       // Делаем три  попытки синхронизации с интернетом
       if( GetNTP() ){
          ms2       = cur_ms;
          err_count = 0;
          t_correct = ntp_time - t_cur;
       }
    }
// Каждые 0.5 секунды выдаем время
   if( cur_ms < ms1 || (cur_ms - ms1) > 500 ){
       ms1 = cur_ms;
       ntp_time    = t_cur + t_correct;       
       int m = ( ntp_time/60 )%60;
       int h = ( ntp_time/3600 )%24;
       int s = ntp_time%60;
       DisplayTime(h, m, s);
       tkotel=getTemp();
       ShPar(tkotel,1);

// Если нет соединения с интернетом, перезагружаемся
   if( err_count > 10 ){
       DiagMsg("NTP not Connected\nReseting", ST7735_RED,1, 17);
       delay(2000);
      // ESP.reset();
    
   }
*/

}






void DisplayTime(int h, int m, int s){
   DiagMsg("BPEM9I:"+String(h)+":"+String(m)+":"+String(s), ST7735_GREEN, 1,17);
}




void testdrawtext(char *text, uint16_t color) {
  tft.setCursor(0, 0);
  tft.setTextColor(color);
  tft.setTextWrap(true);
  tft.print(utf8rus(text));
}


void DiagMsg(String text, uint16_t color, int xpos, int ypos) {
  tft.setTextWrap(false);
  tft.setCursor(xpos, ypos);
  tft.setTextColor(color, ST7735_BLACK);
  tft.setTextSize(1);
  tft.print(utf8rus(text));
}
void ShPar( float val, int par) {
  int xpos,ypos;
  switch (par) {
    case 1:
      xpos=1;
      ypos=60;
      break;
    case 2:
      xpos=64;
      ypos=60;
      break;
    case 3:
      xpos=1;
      ypos=100;
      break;  
    case 4:
      xpos=1;
      ypos=140;
      break;  
  }
  tft.setTextWrap(false);
  tft.setCursor(xpos, ypos);
  tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
  tft.setTextSize(2);
  tft.print(val);
}

void tftPrintTest() {
  tft.setTextWrap(false);
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 30);
  tft.setTextColor(ST7735_RED);
  tft.setTextSize(1);
  tft.println("Hello World!");
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(2);
  tft.println("Hello World!");
  tft.setTextColor(ST7735_GREEN);
  tft.setTextSize(3);
  tft.println("Hello World!");
  tft.setTextColor(ST7735_BLUE);
  tft.setTextSize(4);
  tft.print(1234.567);
  delay(1500);
  tft.setCursor(0, 0);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(0);
  tft.println("Hello World!");
  tft.setTextSize(1);
  tft.setTextColor(ST7735_GREEN);
  tft.print(3, 6);
  tft.println(" Want pi?");
  tft.println(" ");
  tft.print(8675309, HEX); // print 8,675,309 out in HEX!
  tft.println(" Print HEX!");
  tft.println(" ");
  tft.setTextColor(ST7735_WHITE);
  tft.println("Sketch has been");
  tft.println("running for: ");
  tft.setTextColor(ST7735_MAGENTA);
  tft.print(millis() / 1000);
  tft.setTextColor(ST7735_WHITE);
  tft.print(" seconds.");
}


