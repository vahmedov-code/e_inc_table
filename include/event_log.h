#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Device event log: the last LOG_CAPACITY events in NVS, so they survive resets and power
// loss and can be read later in the panel and the CRM. Only transitions are logged (Wi-Fi
// lost/back, CRM error/ok, refresh results, resets), never routine repeats. An event is a
// 16-byte record of numbers; the Russian text is made only when someone reads it.
enum EventKind : uint8_t {
  EV_BOOT=1,EV_BEFORE_RESET,EV_RENDER_ABORT,EV_WIFI_DOWN,EV_WIFI_UP,EV_ACCESS_POINT,EV_CRM_ERROR,EV_CRM_OK,
  EV_REFRESH,EV_REFRESH_FAIL,EV_OTA,EV_BATTERY,EV_POWER,EV_DROPPED,EV_CHARGE,EV_ACCESS,EV_KIND_END
};
constexpr const char* eventKindName(uint8_t k){
  return k==EV_BOOT?"boot":k==EV_BEFORE_RESET?"before_reset":k==EV_RENDER_ABORT?"render_abort":k==EV_WIFI_DOWN?"wifi_down":
    k==EV_WIFI_UP?"wifi_up":k==EV_ACCESS_POINT?"access_point":k==EV_CRM_ERROR?"crm_error":k==EV_CRM_OK?"crm_ok":
    k==EV_REFRESH?"refresh":k==EV_REFRESH_FAIL?"refresh_fail":k==EV_OTA?"ota":k==EV_BATTERY?"battery":
    k==EV_POWER?"power":k==EV_DROPPED?"dropped":k==EV_CHARGE?"charge":k==EV_ACCESS?"access":"other";
}
// Stored in NVS as LOG_CHUNKS blobs of LOG_CHUNK records: one event rewrites 128 bytes.
constexpr int LOG_CAPACITY=64,LOG_CHUNK=8,LOG_CHUNKS=LOG_CAPACITY/LOG_CHUNK;
constexpr uint32_t LOG_VALID_TIME=1700000000; // below this, LogRecord::time counts from boot
struct LogRecord {
  uint32_t time;   // unix seconds, or seconds since boot while the clock was not set
  int32_t value;   // the meaning of value, extra and code depends on the kind: see eventText()
  int32_t extra;
  int16_t code;
  uint8_t kind;    // EventKind; 0 = empty slot
  uint8_t boot;    // low byte of the boot counter: groups the events of one run
};
static_assert(sizeof(LogRecord)==16,"stable NVS record size");

// EV_BEFORE_RESET flags (top byte of extra): what the sign was doing at the last snapshot.
enum : uint8_t {SNAP_RENDERING=1,SNAP_WIFI=2,SNAP_SLEEPING=4,SNAP_POWER_SAVE=8};
// EV_ACCESS_POINT reasons (value).
enum : uint8_t {AP_NO_NETWORK=0,AP_NETWORK_LOST,AP_BUTTON,AP_NETWORK_BACK};
// EV_CRM_ERROR kinds (extra): a failed HTTP exchange, an unreadable answer, a bad address.
enum : uint8_t {CRM_FAIL_HTTP=0,CRM_FAIL_ANSWER,CRM_FAIL_ADDRESS};
// EV_OTA steps (code).
// EV_ACCESS steps (code); for ACCESS_SET value is the source: 0 panel, 1 CRM, 2 first setup.
enum : uint8_t {ACCESS_SET=1,ACCESS_RESET,ACCESS_LOCKED};
enum : uint8_t {OTA_RECEIVED=0,OTA_INSTALLED,OTA_ROLLED_BACK,OTA_CONFIRMED,OTA_FAILED,OTA_REVERTED};

// "0.4.0" -> 400, so a version fits a record field.
inline int32_t versionNumber(const char* v){int a=0,b=0,c=0;sscanf(v,"%d.%d.%d",&a,&b,&c);return a*10000+b*100+c;}

// Cuts a UTF-8 string so it does not end in the middle of a multi-byte character.
inline void utf8Trim(char* s){
  size_t n=strlen(s),i=n;
  while(i>0&&((uint8_t)s[i-1]&0xC0)==0x80)i--;              // back over continuation bytes
  if(i==0)return;
  uint8_t lead=(uint8_t)s[i-1];
  size_t need=lead>=0xF0?4:lead>=0xE0?3:lead>=0xC0?2:1;
  if((n-(i-1))<need)s[i-1]='\0';
}
inline void appendf(char* out,size_t size,const char* fmt,...){
  size_t n=strlen(out);if(n+1>=size)return;
  va_list a;va_start(a,fmt);vsnprintf(out+n,size-n,fmt,a);va_end(a);
}
inline void durationText(uint32_t s,char* out,size_t size){
  if(s<60)snprintf(out,size,"%u с",(unsigned)s);
  else if(s<3600)snprintf(out,size,"%u мин %u с",(unsigned)(s/60),(unsigned)(s%60));
  else if(s<86400)snprintf(out,size,"%u ч %u мин",(unsigned)(s/3600),(unsigned)(s/60%60));
  else snprintf(out,size,"%u дн %u ч",(unsigned)(s/86400),(unsigned)(s/3600%24));
}
inline const char* statusText(int s){return s==0?"Открыто":s==1?"Закрыто":s==2?"Скоро буду":s==3?"Своё изображение":"?";}
// esp_reset_reason_t. On ESP32 the EN button and opening the COM port (DTR/RTS) also read as power-on.
inline const char* resetText(int r){
  switch(r){
    case 1:return "включение питания, кнопка EN или открытие COM-порта";
    case 2:return "внешний сброс";
    case 3:return "программный перезапуск";
    case 4:return "сбой программы (panic)";
    case 5:return "зависание (interrupt watchdog)";
    case 6:return "зависание задачи (task watchdog)";
    case 7:return "сторожевой таймер";
    case 8:return "выход из глубокого сна";
    case 9:return "просадка питания (brownout)";
    case 10:return "сброс SDIO";
    default:return "причина неизвестна";
  }
}
// Reset reasons that mean a fault rather than the owner's action.
constexpr bool faultReset(int r){return r==4||r==5||r==6||r==7||r==9;}
inline const char* phaseText(int p){
  switch(p){
    case 1:return "начало";case 2:return "сброс контроллера экрана";case 3:return "включение питания экрана";
    case 4:return "передача картинки";case 5:return "перерисовка";case 6:return "выключение питания экрана";
    case 7:return "сон экрана";default:return "неизвестный";
  }
}
// wifi_err_reason_t.
inline const char* wifiReasonText(int r){
  switch(r){
    case 0:return "причина не сообщена";
    case 2:return "истекла авторизация";
    case 3:case 8:return "соединение разорвано";
    case 4:return "роутер отключил неактивную вывеску";
    case 5:return "у роутера слишком много клиентов";
    case 6:case 7:return "роутер не признал подключение";
    case 15:case 204:return "сбой рукопожатия — проверьте пароль";
    case 200:return "пропал сигнал роутера";
    case 201:return "сеть не найдена";
    case 202:return "ошибка авторизации";
    case 203:return "роутер отказал в подключении";
    case 205:return "не удалось подключиться";
    default:return "другая причина";
  }
}
// HTTPClient error codes (negative).
inline const char* httpErrorText(int c){
  switch(c){
    case -1:return "не удалось соединиться";
    case -2:case -3:return "не отправился запрос";
    case -4:return "нет соединения";
    case -5:return "соединение оборвалось";
    case -7:return "сервер ответил не по HTTP";
    case -8:return "не хватило памяти";
    case -11:return "сервер не ответил вовремя";
    default:return "ошибка связи";
  }
}
// mbedTLS error codes seen from WiFiClientSecure::lastError().
inline const char* tlsErrorText(int32_t c){
  switch(c){
    case -0x7F00:return "не хватило памяти для HTTPS";
    case -0x2700:return "сертификат CRM не прошёл проверку";
    case -0x7780:return "сервер прервал HTTPS";
    case -0x7880:return "сервер закрыл соединение";
    case -0x6800:return "истекло время HTTPS";
    case -0x0044:return "не удалось соединиться";
    case -0x0050:return "соединение сброшено";
    case -0x0052:return "адрес CRM не найден (DNS)";
    case -0x004C:case -0x004E:return "ошибка передачи";
    default:return "ошибка HTTPS";
  }
}

// Russian text of an event, for the panel, the CRM and the serial log.
inline void eventText(const LogRecord& e,char* out,size_t size){
  char a[32],b[32];out[0]='\0';
  auto volts=[&](int mv){if(mv>0)appendf(out,size,", АКБ %d,%02d В",mv/1000,mv%1000/10);};
  auto version=[](int32_t v,char* s,size_t n){snprintf(s,n,"%d.%d.%d",(int)(v/10000),(int)(v/100%100),(int)(v%100));};
  switch(e.kind){
    case EV_BOOT:
      version(e.value,a,sizeof a);appendf(out,size,"Запуск %s · %s",a,resetText(e.code));break;
    case EV_BEFORE_RESET:{
      uint8_t f=(uint32_t)e.extra>>24;durationText(e.value,a,sizeof a);
      appendf(out,size,"Перед сбросом: работала %s",a);volts(e.code);
      appendf(out,size,", мин. память %d КБ",(int)((e.extra&0xFFFFFF)/1024));
      if(f&SNAP_RENDERING)appendf(out,size,", шло обновление экрана");
      if(f&SNAP_SLEEPING)appendf(out,size,", спала (экономия заряда)");
      else if(!(f&SNAP_WIFI))appendf(out,size,", Wi-Fi не на связи");
      break;}
    case EV_RENDER_ABORT:
      appendf(out,size,"Обновление экрана прервано сбросом на этапе «%s» (%s). Автоповтор выключен",phaseText(e.code),resetText(e.value));break;
    case EV_WIFI_DOWN: // value: the last signal (dBm), or 1 when it never connected after boot
      appendf(out,size,e.value==1?"Wi-Fi не подключился за минуту после запуска: %s (код %d)":"Wi-Fi пропал: %s (код %d)",wifiReasonText(e.code),e.code);
      if(e.extra&&e.extra!=e.code)appendf(out,size,", повторные попытки: %s (код %d)",wifiReasonText(e.extra),(int)e.extra);
      if(e.value<0)appendf(out,size,", сигнал был %d дБм",(int)e.value);
      break;
    case EV_WIFI_UP:
      durationText(e.value,a,sizeof a);
      if(e.extra==1)appendf(out,size,"Подключилась к Wi-Fi через %s после запуска",a);
      else appendf(out,size,e.extra==2?"Wi-Fi вернулся через %s (с учётом сна)":"Wi-Fi вернулся через %s",a);
      if(e.code<0)appendf(out,size,", сигнал %d дБм",e.code);
      break;
    case EV_ACCESS_POINT:
      if(e.code<0)appendf(out,size,"Точка доступа не включилась");
      else if(e.code==0)appendf(out,size,"Точка доступа выключена: офисная сеть на связи");
      else appendf(out,size,"Точка доступа включена: %s",e.value==AP_NO_NETWORK?"нет сохранённой сети":e.value==AP_BUTTON?"кнопка на корпусе":"офисная сеть пропала больше минуты");
      break;
    case EV_CRM_ERROR:
      if(e.extra==CRM_FAIL_ANSWER)appendf(out,size,"CRM прислала непонятный ответ (код %d)",e.code);
      else if(e.extra==CRM_FAIL_ADDRESS)appendf(out,size,"Неверный адрес CRM");
      else if(e.code<0)appendf(out,size,"CRM недоступна: %s (%d)",httpErrorText(e.code),e.code);
      else appendf(out,size,"CRM ответила %d%s",e.code,e.code==401?" — неверный токен":e.code==503?" — модуль вывески выключен":"");
      if(e.value)appendf(out,size,", HTTPS: %s (-0x%04X)",tlsErrorText(e.value),(unsigned)(-e.value));
      break;
    case EV_CRM_OK:
      durationText(e.value,a,sizeof a);appendf(out,size,"Связь с CRM восстановлена, перерыв %s",a);break;
    case EV_REFRESH:case EV_REFRESH_FAIL:
      appendf(out,size,e.kind==EV_REFRESH?"Экран обновлён: «%s» за %d,%d с":"Экран не обновился: «%s», %d,%d с",statusText(e.code),(int)(e.value/1000),(int)(e.value%1000/100));
      volts(e.extra);
      break;
    case EV_OTA:
      version(e.value,a,sizeof a);version(e.extra,b,sizeof b);
      if(e.code==OTA_RECEIVED)appendf(out,size,e.extra==1?"Скачана прошивка из CRM, %d КБ — перезапуск":"Получена прошивка, %d КБ — перезапуск",(int)(e.value/1024));
      else if(e.code==OTA_FAILED)appendf(out,size,"Прошивка из CRM не скачалась после 3 попыток");
      else if(e.code==OTA_REVERTED)appendf(out,size,e.extra==1?"Прошивка %s не вышла на связь с CRM за 10 минут — возврат к прежней":"Прошивка %s перезапускалась без остановки — возврат к прежней",a);
      else if(e.code==OTA_INSTALLED)appendf(out,size,"Установлена прошивка %s (была %s)",a,b);
      else if(e.code==OTA_ROLLED_BACK)appendf(out,size,"Новая прошивка не запустилась — работает прежняя %s",a);
      else appendf(out,size,"Прошивка %s проработала минуту и закреплена",a);
      break;
    case EV_BATTERY:
      appendf(out,size,e.code<=15?"Низкий заряд: %d %%":"Заряд восстановился: %d %%",e.code);volts(e.value);break;
    case EV_POWER:
      appendf(out,size,"Экономия заряда %s (%s)",e.code?"включена":"выключена",e.value==2?(e.code?"автоматически: работа от аккумулятора":"автоматически: питание от USB"):e.value?"из CRM":"из панели");break;
    case EV_CHARGE: // code: ChargeState (battery.h), value: mV, extra: seconds of charging
      if(e.code==2)appendf(out,size,"Идёт зарядка от USB");
      else if(e.code==3){appendf(out,size,"Аккумулятор заряжен");if(e.extra>0){durationText(e.extra,a,sizeof a);appendf(out,size," за %s",a);}}
      else appendf(out,size,"USB отключён, работа от аккумулятора");
      volts(e.value);
      break;
    case EV_ACCESS:
      if(e.code==ACCESS_SET)appendf(out,size,"Логин и пароль панели заданы (%s)",e.value==1?"из CRM":e.value==2?"первая настройка":"в панели");
      else if(e.code==ACCESS_RESET)appendf(out,size,"Пароль панели сброшен кнопкой на корпусе");
      else appendf(out,size,"5 неверных паролей подряд — вход в панель закрыт на минуту");
      break;
    case EV_DROPPED:
      appendf(out,size,"Пропущено событий: %d — их было слишком много подряд",(int)e.value);break;
    default:
      appendf(out,size,"Событие %d",e.kind);
  }
  utf8Trim(out);
}
