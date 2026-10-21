#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "runtime_mode.h"
#include "board.h"
#if !AVIOR_RECOVERY
#include <U8g2_for_Adafruit_GFX.h>
#include "panel.h"
#endif
#include <atomic>
#include <time.h>
#include "schedule.h"
#include "web_ui.h"

WebServer web(80);
Preferences prefs;
String token,apPassword,ssid,wifiPassword;
const char* names[]={"Открыто","Закрыто","Скоро буду","Изображение"};
const char* keys[]={"open","closed","soon","image"};
Event events[32];int eventCount=0,utcOffset=180,desired=1,displayed=-1;
bool automatic=false,confirmed=false,fsReady=false;
String displayError="",source="manual";
uint32_t lastRefresh=0,lastTick=0;
std::atomic<bool> rendering(false);
QueueHandle_t jobs,results;
struct Result {int status;bool ok;};
File uploadFile;size_t uploadBytes=0;bool uploadOK=false;String uploadPath;
uint32_t lastHealth=0;
esp_reset_reason_t bootReason=ESP_RST_UNKNOWN;
bool scanActive=false;
uint32_t scanStarted=0;
const char* resetName(esp_reset_reason_t r){
  switch(r){
    case ESP_RST_POWERON:return "power_on";
    case ESP_RST_EXT:return "external_reset";
    case ESP_RST_SW:return "software_reset";
    case ESP_RST_PANIC:return "panic";
    case ESP_RST_INT_WDT:return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:return "task_watchdog";
    case ESP_RST_WDT:return "other_watchdog";
    case ESP_RST_DEEPSLEEP:return "deep_sleep";
    case ESP_RST_BROWNOUT:return "brownout";
    case ESP_RST_SDIO:return "sdio_reset";
    default:return "unknown";
  }
}

String randomHex(int bytes) {String s;for(int i=0;i<bytes;i++){char h[3];snprintf(h,3,"%02x",esp_random()&255);s+=h;}return s;}
int statusIndex(const String& value){for(int i=0;i<4;i++)if(value==keys[i])return i;return -1;}
String assetPath(int s){return String("/")+keys[s]+".frame";}
void reply(int code,const JsonDocument& d){String s;serializeJson(d,s);web.sendHeader("Cache-Control","no-store");web.send(code,"application/json",s);}
void error(int code,const char* msg){JsonDocument d;d["error"]=msg;reply(code,d);}
bool writable(bool wifi=false){if(canMutate(wifi))return true;error(423,"Диагностический режим: это действие отключено");return false;}
bool authorized(){return web.header("Authorization")=="Bearer "+token;}
bool auth(){if(authorized())return true;error(401,"Требуется ключ доступа из Serial Monitor");return false;}
bool body(JsonDocument& d){
  if(web.arg("plain").length()>8192 || deserializeJson(d,web.arg("plain"))){error(400,"Некорректный JSON или превышен лимит 8192 байта");return false;}
  if(!d.is<JsonObject>()){error(400,"Ожидается JSON-объект");return false;}return true;
}
void scheduleJson(JsonDocument& d){
  d["enabled"]=automatic;d["utcOffsetMinutes"]=utcOffset;
  JsonArray a=d["events"].to<JsonArray>();
  for(int i=0;i<eventCount;i++){auto e=a.add<JsonObject>();e["day"]=events[i].day;e["minute"]=events[i].minute;e["status"]=keys[events[i].status];}
}
void saveSchedule(){JsonDocument d;scheduleJson(d);String s;serializeJson(d,s);prefs.putString("schedule",s);}
bool parseSchedule(const JsonDocument& d,Event* out,int& count,int& offset){
  if(!d["enabled"].is<bool>() || !d["utcOffsetMinutes"].is<int>() || !d["events"].is<JsonArray>())return false;
  offset=d["utcOffsetMinutes"];if(offset < -720 || offset>840)return false;
  auto a=d["events"].as<JsonArrayConst>();if(a.size()>32)return false;count=0;
  for(auto e:a){
    if(!e["day"].is<int>() || !e["minute"].is<int>() || !e["status"].is<const char*>())return false;
    int day=e["day"],minute=e["minute"],s=statusIndex(e["status"].as<String>());
    if(day<0||day>6||minute<0||minute>1439||s<0||s>2)return false;
    for(int i=0;i<count;i++)if(out[i].day==day&&out[i].minute==minute)return false;
    out[count++]={(uint8_t)day,(uint16_t)minute,(uint8_t)s};
  }
  return !d["enabled"].as<bool>() || count>0;
}
#if !AVIOR_RECOVERY
void worker(void*) {
  uint8_t* data=(uint8_t*)malloc(FRAME_BYTES);
  int s;
  for(;;){
    xQueueReceive(jobs,&s,portMAX_DELAY);bool ok=data!=nullptr;
    if(ok){
      bool loaded=false;
      if(fsReady){File f=LittleFS.open(assetPath(s),"r");if(f&&f.size()==FRAME_BYTES)loaded=f.read(data,FRAME_BYTES)==FRAME_BYTES;f.close();}
      if(!loaded && s==3)ok=false;
      if(ok&&!loaded){
        memset(data,0x55,FRAME_BYTES);Frame f(data);
        f.fillRect(40,40,720,8,s==1?3:s==2?2:0);
        f.setTextColor(0);f.setTextSize(5);f.setCursor(44,85);f.print("AVIOR");
        ScaledText text(f);U8G2_FOR_ADAFRUIT_GFX font;font.begin(text);font.setFont(u8g2_font_unifont_t_cyrillic);
        font.setForegroundColor(0);font.setBackgroundColor(1);font.setCursor(11,66);font.print(names[s]);
        f.setTextSize(2);f.setCursor(44,410);f.print("avior.local");
      }
      if(ok){Serial.printf("[display] start status=%s\n",keys[s]);Panel panel;ok=panel.show(data);Serial.printf("[display] complete ok=%d\n",ok);}
    }
    Result r{s,ok};xQueueSend(results,&r,portMAX_DELAY);
  }
}
#endif
void requestDisplay(){
  if(RECOVERY_MODE)return;
  if(!confirmed || rendering || !fsReady && desired==3 || desired==displayed)return;
  if(lastRefresh && millis()-lastRefresh<60000)return;
  rendering=true;if(xQueueSend(jobs,&desired,0)!=pdTRUE)rendering=false;
}
void state(){
  JsonDocument d;d["version"]=FW_VERSION;d["recovery"]=RECOVERY_MODE;d["mode"]=MODE_NAME;d["wifiSetupAllowed"]=canMutate(true);d["resetReason"]=resetName(bootReason);d["resetReasonCode"]=(int)bootReason;d["board"]="ESP32-M075 V3.0";d["panel"]="GDEM075F53 / JD79665CA";
  d["desired"]=keys[desired];d["displayed"]=displayed<0?"unknown":keys[displayed];d["busy"]=rendering.load();
  d["displayError"]=displayError;d["hardwareConfirmed"]=confirmed;d["storageReady"]=fsReady;
  d["automatic"]=automatic;d["source"]=source;d["ip"]=WiFi.localIP().toString();d["apIP"]=WiFi.softAPIP().toString();
  d["wifiConnected"]=WiFi.status()==WL_CONNECTED;d["timeValid"]=time(nullptr)>1700000000;
  d["unixTime"]=(int64_t)time(nullptr);d["heap"]=ESP.getFreeHeap();d["uptimeSeconds"]=millis()/1000;
  d["cooldownSeconds"]=lastRefresh&&millis()-lastRefresh<60000?(60000-(millis()-lastRefresh))/1000:0;
  auto assets=d["assets"].to<JsonObject>();for(int i=0;i<4;i++)assets[keys[i]]=fsReady&&LittleFS.exists(assetPath(i));reply(200,d);
}
void upload(){
  auto& u=web.upload();
  if(u.status==UPLOAD_FILE_START){
    uploadOK=false;uploadBytes=0;uploadPath="";
    if(RECOVERY_MODE || !authorized() || rendering || !fsReady)return;
    int s=statusIndex(web.arg("slot"));if(s<0)return;
    uploadPath=assetPath(s);uploadFile=LittleFS.open("/upload.tmp","w");uploadOK=bool(uploadFile);
  }else if(u.status==UPLOAD_FILE_WRITE){
    uploadBytes+=u.currentSize;
    if(uploadOK && (uploadBytes>FRAME_BYTES||uploadFile.write(u.buf,u.currentSize)!=u.currentSize))uploadOK=false;
  }else if(u.status==UPLOAD_FILE_END){
    if(uploadFile)uploadFile.close();uploadOK=uploadOK&&uploadBytes==FRAME_BYTES;
  }else if(u.status==UPLOAD_FILE_ABORTED){if(uploadFile)uploadFile.close();uploadOK=false;if(fsReady)LittleFS.remove("/upload.tmp");}
}
void routes(){
  const char* headers[]={"Authorization"};web.collectHeaders(headers,1);
  web.on("/",HTTP_GET,[]{web.sendHeader("Content-Encoding","gzip");web.sendHeader("Cache-Control","no-cache");web.sendHeader("X-Content-Type-Options","nosniff");web.send_P(200,"text/html; charset=utf-8",(const char*)WEB_UI,sizeof(WEB_UI));});
  web.on("/api/v1/state",HTTP_GET,[]{if(auth())state();});
  web.on("/api/v1/status",HTTP_POST,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    int s=statusIndex(d["status"].as<String>());if(s<0){error(400,"Неизвестный статус");return;}
    if(s==3&&(!fsReady||!LittleFS.exists(assetPath(3)))){error(409,"Сначала загрузите изображение");return;}
    desired=s;automatic=false;source="manual";prefs.putInt("desired",s);saveSchedule();displayError="";requestDisplay();state();
  });
  web.on("/api/v1/schedule",HTTP_GET,[]{if(!auth())return;JsonDocument d;scheduleJson(d);reply(200,d);});
  web.on("/api/v1/schedule",HTTP_PUT,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;Event next[32];int n,offset;
    if(!parseSchedule(d,next,n,offset)){error(400,"Проверьте дни, время, статусы и отсутствие одинаковых событий");return;}
    memcpy(events,next,n*sizeof(Event));eventCount=n;utcOffset=offset;automatic=d["enabled"];saveSchedule();reply(200,d);
  });
  web.on("/api/v1/clock",HTTP_POST,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    if(!d["unixTime"].is<int64_t>()){error(400,"Нужно целое unixTime");return;}
    int64_t t=d["unixTime"];if(t<1700000000LL||t>4102444800LL){error(400,"Недопустимое время");return;}
    timeval tv{(time_t)t,0};settimeofday(&tv,nullptr);state();
  });
  web.on("/api/v1/hardware",HTTP_POST,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    if(d["confirmation"]!="ESP32-M075-V3.0:F53:18,23,27,14,12,13"){error(400,"Требуется подтверждение платы, F53 и GPIO по README");return;}
    confirmed=true;prefs.putBool("hardware",true);requestDisplay();state();
  });
  web.on("/api/v1/storage/init",HTTP_POST,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    if(rendering||fsReady||d["confirmation"]!="FORMAT_AVIOR_STORAGE"){error(409,"Форматирование запрещено: проверьте состояние и подтверждение");return;}
    fsReady=LittleFS.format()&&LittleFS.begin(false,"/littlefs",5,"littlefs");if(!fsReady){error(500,"Не удалось подготовить хранилище");return;}state();
  });
  web.on("/api/v1/wifi",HTTP_PUT,[]{
    if(!auth()||!writable(true))return;JsonDocument d;if(!body(d))return;
    if(scanActive&&WiFi.scanComplete()==WIFI_SCAN_RUNNING){error(409,"Дождитесь окончания поиска сетей");return;}
    if(!d["ssid"].is<const char*>()||!d["password"].is<const char*>()){error(400,"Требуются ssid и password");return;}
    String s=d["ssid"],p=d["password"];if(s.length()<1||s.length()>32||p.length()>63||(p.length()>0&&p.length()<8)){error(400,"Проверьте имя сети и пароль");return;}
    ssid=s;wifiPassword=p;prefs.putString("ssid",s);prefs.putString("wifi",p);web.send(202,"application/json","{\"accepted\":true}");WiFi.begin(s.c_str(),p.c_str());
  });
  web.on("/api/v1/wifi/scan",HTTP_POST,[]{
    if(!auth()||!writable(true))return;
    if(scanActive&&WiFi.scanComplete()==WIFI_SCAN_RUNNING){JsonDocument d;d["scanning"]=true;reply(202,d);return;}
    WiFi.scanDelete();
    int n=WiFi.scanNetworks(true,false);
    if(n==WIFI_SCAN_FAILED){scanActive=false;error(503,"Не удалось начать поиск. Повторите через несколько секунд");return;}
    scanActive=true;scanStarted=millis();JsonDocument d;d["scanning"]=n==WIFI_SCAN_RUNNING;reply(202,d);
  });
  web.on("/api/v1/wifi/scan",HTTP_GET,[]{
    if(!auth())return;
    if(!canMutate(true)){error(423,"Поиск сетей недоступен в AP-only recovery");return;}
    if(!scanActive){error(409,"Сначала запустите поиск сетей");return;}
    int n=WiFi.scanComplete();JsonDocument d;d["scanning"]=n==WIFI_SCAN_RUNNING;
    if(n==WIFI_SCAN_RUNNING){reply(202,d);return;}
    if(n==WIFI_SCAN_FAILED){scanActive=false;WiFi.scanDelete();error(503,"Поиск сетей не завершился. Повторите попытку");return;}
    JsonArray networks=d["networks"].to<JsonArray>();String seen[20];int count=0;
    // WiFi.SSID takes uint8_t indices in the pinned framework.
    for(int i=0;i<n&&i<256&&count<20;i++){
      String name=WiFi.SSID(i);if(name.isEmpty())continue;
      bool duplicate=false;for(int j=0;j<count;j++)if(seen[j]==name){duplicate=true;break;}
      if(duplicate)continue;
      seen[count++]=name;auto network=networks.add<JsonObject>();
      network["ssid"]=name;network["rssi"]=WiFi.RSSI(i);network["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();scanActive=false;reply(200,d);
  });
  web.on("/api/v1/assets",HTTP_POST,[]{
    if(!auth()||!writable())return;
    if(!uploadOK||uploadPath.isEmpty()){error(400,"Нужен файл ровно 96000 байт, доступное хранилище и свободный дисплей");return;}
    if(!LittleFS.rename("/upload.tmp",uploadPath)){error(500,"Не удалось сохранить изображение");return;}
    if(statusIndex(web.arg("slot"))==desired)displayed=-1;uploadOK=false;requestDisplay();state();
  },upload);
  web.on("/api/v1/asset",HTTP_GET,[]{
    if(!auth())return;int s=statusIndex(web.arg("slot"));if(!fsReady||s<0){error(404,"Нет изображения");return;}
    File f=LittleFS.open(assetPath(s),"r");if(!f){error(404,"Нет изображения");return;}
    web.streamFile(f,"application/octet-stream");f.close();
  });
  web.onNotFound([]{error(404,"Маршрут не найден");});web.begin();
}
void setup(){
  Serial.begin(115200);bootReason=esp_reset_reason();
  Serial.printf("\n[boot] AviorSign %s reset=%s(%d) heap=%u\n",FW_VERSION,resetName(bootReason),(int)bootReason,ESP.getFreeHeap());
  Serial.println("[boot] phase=nvs");Serial.flush();prefs.begin("avior-sign",false);
  token=prefs.getString("token","");apPassword=prefs.getString("ap","");
  if(token.isEmpty()){token=randomHex(24);prefs.putString("token",token);}
  if(apPassword.isEmpty()){apPassword=randomHex(8);prefs.putString("ap",apPassword);}
  confirmed=!RECOVERY_MODE&&prefs.getBool("hardware",false);desired=constrain(prefs.getInt("desired",1),0,3);
  JsonDocument d;String stored=prefs.getString("schedule","");int n,offset;
  if(!deserializeJson(d,stored)&&parseSchedule(d,events,n,offset)){eventCount=n;utcOffset=offset;automatic=d["enabled"];}
  if(RECOVERY_MODE){
    automatic=false;source=MODE_NAME;fsReady=false;
    Serial.println("[boot] recovery: display task, SPI, framebuffer and filesystem disabled");
    Serial.printf("[boot] station Wi-Fi setup=%s\n",RECOVERY_WIFI?"enabled; connect from panel":"disabled");
  }else{
  Serial.println("[boot] phase=filesystem");Serial.flush();
  fsReady=LittleFS.begin(false,"/littlefs",5,"littlefs"); // Never auto-format the factory filesystem.
#if !AVIOR_RECOVERY
  jobs=xQueueCreate(1,sizeof(int));results=xQueueCreate(1,sizeof(Result));
  if(!jobs||!results||xTaskCreatePinnedToCore(worker,"epaper",8192,nullptr,1,nullptr,1)!=pdPASS){Serial.println("Worker allocation failed");while(true)delay(1000);}
#endif
  }
  Serial.println("[boot] phase=wifi");Serial.flush();
  WiFi.mode(RECOVERY_MODE?WIFI_AP:WIFI_AP_STA);WiFi.setHostname("avior");
  bool apOK=WiFi.softAP(("AviorSign-"+String((uint32_t)ESP.getEfuseMac(),HEX)).c_str(),apPassword.c_str());
  if(RECOVERY_MODE)WiFi.setTxPower(WIFI_POWER_8_5dBm);
  Serial.printf("[boot] access_point=%s\n",apOK?"ready":"failed");
  // Wi-Fi recovery deliberately waits for a panel request, even with saved credentials.
  // This preserves an AP-only baseline until the owner starts the next test.
  if(!RECOVERY_MODE){
    ssid=prefs.getString("ssid","");wifiPassword=prefs.getString("wifi","");if(ssid.length())WiFi.begin(ssid.c_str(),wifiPassword.c_str());
    configTime(0,0,"pool.ntp.org","time.google.com");MDNS.begin("avior");MDNS.addService("http","tcp",80);
  }
  Serial.printf("\nAviorSign setup\nAP password: %s\nAPI token: %s\nOpen http://192.168.4.1\n",apPassword.c_str(),token.c_str());
  routes();Serial.println("[boot] ready; health log every 5 seconds");
}
void loop(){
  web.handleClient();Result r;
  if(scanActive&&millis()-scanStarted>30000&&WiFi.scanComplete()!=WIFI_SCAN_RUNNING){WiFi.scanDelete();scanActive=false;}
  if(results&&xQueueReceive(results,&r,0)==pdTRUE){rendering=false;lastRefresh=millis();if(r.ok){displayed=r.status;displayError="";}else {displayError="Ошибка обновления: проверьте BUSY, питание, F53 и GPIO. Автоповтор отключён.";}}
  if(millis()-lastTick>1000){
    lastTick=millis();time_t now=time(nullptr);
    if(automatic&&now>1700000000){source="schedule";now+=utcOffset*60;tm t;gmtime_r(&now,&t);int s=scheduledStatus(events,eventCount,t.tm_wday,t.tm_hour*60+t.tm_min);if(s>=0&&desired!=s){desired=s;displayError="";}}
    if(displayError.isEmpty())requestDisplay();
  }
  if(millis()-lastHealth>=5000){
    lastHealth=millis();
    Serial.printf("[health] mode=%s uptime_s=%lu heap=%u min_heap=%u clients=%u wifi=%d reset=%s(%d)\n",MODE_NAME,(unsigned long)(millis()/1000),ESP.getFreeHeap(),esp_get_minimum_free_heap_size(),WiFi.softAPgetStationNum(),(int)WiFi.status(),resetName(bootReason),(int)bootReason);
  }
  delay(2);
}
