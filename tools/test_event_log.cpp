// Host test of the event log: UTF-8 trimming and the Russian event texts.
#include <cassert>
#include <cstdio>
#include <string>
#include "event_log.h"
static void cut(char* out,size_t size,const char* text){memcpy(out,text,size-1);out[size-1]='\0';utf8Trim(out);}
static std::string text(LogRecord e){char s[256];eventText(e,s,sizeof s);return s;}
static void expect(const std::string& got,const char* want){
  if(got!=want){fprintf(stderr,"got  «%s»\nwant «%s»\n",got.c_str(),want);assert(false);}
}
static LogRecord ev(uint8_t kind,int code,int32_t value=0,int32_t extra=0){return {LOG_VALID_TIME+5,value,extra,(int16_t)code,kind,1};}
int main(){
  char a[32];cut(a,20,"Wi-Fi пропал");assert(!strcmp(a,"Wi-Fi пропал"));   // 19 bytes fit: unchanged
  char b[8];cut(b,8,"АКБ 15%");assert(!strcmp(b,"АКБ "));                   // 7 bytes: whole letters
  char c[6];cut(c,6,"Экран");assert(!strcmp(c,"Эк"));                       // half of «р» dropped
  char d[5];cut(d,5,"Эк!");assert(!strcmp(d,"Эк"));                         // exactly two letters
  char e[3];cut(e,3,"€x");assert(!strcmp(e,""));                            // 3-byte letter cut after 2
  assert(!strcmp(eventKindName(EV_WIFI_DOWN),"wifi_down")&&!strcmp(eventKindName(200),"other"));
  for(int k=1;k<EV_KIND_END;k++)assert(strcmp(eventKindName(k),"other"));
  assert(versionNumber("0.4.0")==400&&versionNumber("1.12.3-recovery")==11203);

  expect(text(ev(EV_BOOT,9,400)),"Запуск 0.4.0 · просадка питания (brownout)");
  expect(text(ev(EV_BEFORE_RESET,3712,7384,(SNAP_RENDERING|SNAP_WIFI)<<24|46000)),
    "Перед сбросом: работала 2 ч 3 мин, АКБ 3,71 В, мин. память 44 КБ, шло обновление экрана");
  expect(text(ev(EV_BEFORE_RESET,0,59,SNAP_SLEEPING<<24|80000)),"Перед сбросом: работала 59 с, мин. память 78 КБ, спала (экономия заряда)");
  expect(text(ev(EV_BEFORE_RESET,0,61,90000)),"Перед сбросом: работала 1 мин 1 с, мин. память 87 КБ, Wi-Fi не на связи");
  expect(text(ev(EV_RENDER_ABORT,5,9)),"Обновление экрана прервано сбросом на этапе «перерисовка» (просадка питания (brownout)). Автоповтор выключен");
  expect(text(ev(EV_WIFI_DOWN,200,-78,201)),"Wi-Fi пропал: пропал сигнал роутера (код 200), повторные попытки: сеть не найдена (код 201), сигнал был -78 дБм");
  expect(text(ev(EV_WIFI_DOWN,8,0,8)),"Wi-Fi пропал: соединение разорвано (код 8)");
  expect(text(ev(EV_WIFI_DOWN,201,1,201)),"Wi-Fi не подключился за минуту после запуска: сеть не найдена (код 201)");
  expect(text(ev(EV_WIFI_UP,-61,7,1)),"Подключилась к Wi-Fi через 7 с после запуска, сигнал -61 дБм");
  expect(text(ev(EV_WIFI_UP,-70,1900,2)),"Wi-Fi вернулся через 31 мин 40 с (с учётом сна), сигнал -70 дБм");
  expect(text(ev(EV_ACCESS_POINT,1,AP_NETWORK_LOST)),"Точка доступа включена: офисная сеть пропала больше минуты");
  expect(text(ev(EV_ACCESS_POINT,0,AP_NETWORK_BACK)),"Точка доступа выключена: офисная сеть на связи");
  expect(text(ev(EV_CRM_ERROR,-1,-0x7F00)),"CRM недоступна: не удалось соединиться (-1), HTTPS: не хватило памяти для HTTPS (-0x7F00)");
  expect(text(ev(EV_CRM_ERROR,401)),"CRM ответила 401 — неверный токен");
  expect(text(ev(EV_CRM_ERROR,200,0,CRM_FAIL_ANSWER)),"CRM прислала непонятный ответ (код 200)");
  expect(text(ev(EV_CRM_OK,0,95000)),"Связь с CRM восстановлена, перерыв 1 дн 2 ч");
  expect(text(ev(EV_REFRESH,0,24830,3905)),"Экран обновлён: «Открыто» за 24,8 с, АКБ 3,90 В");
  expect(text(ev(EV_REFRESH_FAIL,2,40050)),"Экран не обновился: «Скоро буду», 40,0 с");
  expect(text(ev(EV_OTA,OTA_INSTALLED,400,302)),"Установлена прошивка 0.4.0 (была 0.3.2)");
  expect(text(ev(EV_OTA,OTA_RECEIVED,1195781)),"Получена прошивка, 1167 КБ — перезапуск");
  expect(text(ev(EV_OTA,OTA_RECEIVED,1238448,1)),"Скачана прошивка из CRM, 1209 КБ — перезапуск");
  expect(text(ev(EV_OTA,OTA_REVERTED,403,1)),"Прошивка 0.4.3 не вышла на связь с CRM за 10 минут — возврат к прежней");
  expect(text(ev(EV_OTA,OTA_LAYOUT,0x1A0000)),"Память перераспределена: разделы прошивки по 1664 КБ — перезапуск");
  expect(text(ev(EV_OTA,OTA_LAYOUT_FAIL,0,1)),"Перераспределить память не удалось — осталась прежняя раскладка");
  expect(text(ev(EV_OTA,OTA_LAYOUT_LATER)),"Перераспределение памяти отложено: заряд меньше 30 % и нет USB");
  expect(text(ev(EV_OTA,OTA_FAILED,0)),"Прошивка из CRM не скачалась после 3 попыток");
  expect(text(ev(EV_BATTERY,12,3480)),"Низкий заряд: 12 %, АКБ 3,48 В");
  expect(text(ev(EV_POWER,1,1)),"Экономия заряда включена (из CRM)");
  expect(text(ev(EV_POWER,0,2)),"Экономия заряда выключена (автоматически: питание от USB)");
  expect(text(ev(EV_POWER,1,2)),"Экономия заряда включена (автоматически: работа от аккумулятора)");
  expect(text(ev(EV_CHARGE,2,3970)),"Идёт зарядка от USB, АКБ 3,97 В");
  expect(text(ev(EV_CHARGE,3,4250,7860)),"Аккумулятор заряжен за 2 ч 11 мин, АКБ 4,25 В");
  expect(text(ev(EV_CHARGE,1,4180)),"USB отключён, работа от аккумулятора, АКБ 4,18 В");
  expect(text(ev(EV_ACCESS,ACCESS_SET,1)),"Логин и пароль панели заданы (из CRM)");
  expect(text(ev(EV_ACCESS,ACCESS_RESET)),"Пароль панели сброшен кнопкой на корпусе");
  expect(text(ev(EV_DROPPED,0,17)),"Пропущено событий: 17 — их было слишком много подряд");
  // A short buffer still ends on a whole letter.
  char small[40];eventText(ev(EV_RENDER_ABORT,4,9),small,sizeof small);
  assert(strlen(small)<sizeof small);{std::string s=small;char t[64];strcpy(t,small);utf8Trim(t);assert(s==t);}
  puts("Event log: PASS");
}
