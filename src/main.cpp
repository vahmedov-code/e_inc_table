#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <Update.h>
#include <qrcode.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_sntp.h>
#include <nvs.h>
#include <algorithm>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include "wifi_state.h"
#include "runtime_mode.h"
#include "board.h"
#include "render_trace.h"
#if !AVIOR_RECOVERY
#include <U8g2_for_Adafruit_GFX.h>
#include "panel.h"
#include "compose.h"
#endif
#include <atomic>
#include <time.h>
#include "schedule.h"
#include "schedule_json.h"
#include "battery.h"
#include "qr_matrix.h"
#include "crm_roots.h"
#include "event_log.h"
#include "web_ui.h"

WebServer web(80);
Preferences prefs;
String token,apPassword,ssid,wifiPassword;
const char* names[]={"Открыто","Закрыто","Скоро буду","Изображение"};
const char* keys[]={"open","closed","soon","image"};
Event events[32];int eventCount=0,utcOffset=180,desired=1,displayed=-1;
bool automatic=false,confirmed=false,fsReady=false;
uint32_t manualAt=0; // unix time of a manual status: it holds until the next schedule event
String displayError="",source="manual";
uint32_t lastRefresh=0,lastTick=0;
std::atomic<bool> rendering(false);
QueueHandle_t jobs,results;
struct Result {int status;bool ok;};
File uploadFile;size_t uploadBytes=0;bool uploadOK=false;String uploadPath;
uint32_t lastHealth=0;
esp_reset_reason_t bootReason=ESP_RST_UNKNOWN;
bool scanActive=false;
uint8_t interruptedPhase=PHASE_IDLE;uint32_t renderAborts=0;
std::atomic<uint32_t> workerStackFree(0);
// Device event log (include/event_log.h): a ring of LOG_CAPACITY records in NVS, read in the
// panel (GET /api/v1/events) and copied to the CRM. Recovery builds only read it.
Preferences logPrefs;
LogRecord logRing[LOG_CAPACITY];
uint32_t logNext=1,bootCount=0,logBootFirst=1; // seqs start at 1; logBootFirst: first seq of this run
bool logReady=false,logClockFixed=false;
// At most LOG_BURST events per 10 minutes: a flapping network must neither wear the flash
// nor push the resets out of the log. The skipped ones are counted.
constexpr uint32_t LOG_WINDOW_MS=600000;constexpr int LOG_BURST=20;
uint32_t logWindowAt=0;int logInWindow=0,logDropped=0;
uint32_t logNow(){time_t t=time(nullptr);return t>(time_t)LOG_VALID_TIME?(uint32_t)t:millis()/1000;}
uint32_t logFirst(){return logNext>LOG_CAPACITY?logNext-LOG_CAPACITY:1;}
void logSaveChunk(int c){char k[4];snprintf(k,sizeof k,"c%d",c);logPrefs.putBytes(k,&logRing[c*LOG_CHUNK],LOG_CHUNK*sizeof(LogRecord));}
void logStore(const LogRecord& e){
  logRing[logNext%LOG_CAPACITY]=e;logSaveChunk(logNext%LOG_CAPACITY/LOG_CHUNK);logPrefs.putUInt("next",++logNext);logInWindow++;
}
// Settings come first: the log stops writing while NVS is low on free entries.
bool logRoom(){nvs_stats_t st;return nvs_get_stats(nullptr,&st)==ESP_OK&&st.free_entries>=48;}
void logRecord(const LogRecord& e){
  char text[200];eventText(e,text,sizeof text);Serial.printf("[event] %s\n",text);
  if(!logReady)return;
  if(logInWindow>=LOG_BURST||!logRoom()){logDropped++;return;}
  logStore(e);
}
void logEvent(EventKind kind,int code=0,int32_t value=0,int32_t extra=0){logRecord({logNow(),value,extra,(int16_t)code,kind,(uint8_t)bootCount});}
void logBegin(){
  if(!logPrefs.begin("avior-log",RECOVERY_MODE))return;
  logNext=std::max(1u,logPrefs.getUInt("next",1));
  for(int c=0;c<LOG_CHUNKS;c++){
    char k[4];snprintf(k,sizeof k,"c%d",c);LogRecord* chunk=&logRing[c*LOG_CHUNK];
    if(!logPrefs.isKey(k)||logPrefs.getBytes(k,chunk,LOG_CHUNK*sizeof(LogRecord))!=LOG_CHUNK*sizeof(LogRecord))memset(chunk,0,LOG_CHUNK*sizeof(LogRecord));
  }
  bootCount=logPrefs.getUInt("boots",0);logBootFirst=logNext;
  if(RECOVERY_MODE)return;
  logPrefs.putUInt("boots",++bootCount);logReady=true;
}
// Every second: opens the next burst window, and once the clock is set gives the events
// of this run logged before it (time = seconds since boot) their real time.
void logTick(){
  if(!logReady)return;
  if(millis()-logWindowAt>=LOG_WINDOW_MS){
    logWindowAt=millis();logInWindow=0;
    if(logDropped&&logRoom()){logStore({logNow(),logDropped,0,0,EV_DROPPED,(uint8_t)bootCount});logDropped=0;}
  }
  if(logClockFixed||time(nullptr)<=(time_t)LOG_VALID_TIME)return;
  logClockFixed=true;uint32_t now=time(nullptr),up=millis()/1000;bool dirty[LOG_CHUNKS]={};
  for(uint32_t s=std::max(logBootFirst,logFirst());s<logNext;s++){
    LogRecord& e=logRing[s%LOG_CAPACITY];
    if(e.kind&&e.boot==(uint8_t)bootCount&&e.time<LOG_VALID_TIME&&e.time<=up){e.time=now-(up-e.time);dirty[s%LOG_CAPACITY/LOG_CHUNK]=true;}
  }
  for(int c=0;c<LOG_CHUNKS;c++)if(dirty[c])logSaveChunk(c);
}
bool trial=false,trialCrm=false; // running a new image that is not confirmed yet (see TRIAL_CRM_MS)
void startTrial(bool fromCrm){
  const esp_partition_t* running=esp_ota_get_running_partition();const esp_partition_t* next=esp_ota_get_boot_partition();
  if(!logReady||!running||!next||next==running)return;
  logPrefs.putUInt("trialPrev",running->address);logPrefs.putUChar("trialBoots",0);logPrefs.putBool("trialCrm",fromCrm);
  logPrefs.putUInt("otaTo",next->address);logPrefs.putUInt("otaFrom",versionNumber(FW_VERSION));
}
void endTrial(){trial=false;logPrefs.remove("trialPrev");logPrefs.remove("trialBoots");logPrefs.remove("trialCrm");}
// Boots the previous image; its boot logs the failed update (otaTo still names this one).
void revertTrial(bool crm){
  const esp_partition_t* prev=esp_ota_get_next_update_partition(nullptr);
  logEvent(EV_OTA,OTA_REVERTED,versionNumber(FW_VERSION),crm);
  if(prev&&prev->address==logPrefs.getUInt("trialPrev",0)&&esp_ota_set_boot_partition(prev)==ESP_OK){endTrial();Serial.flush();ESP.restart();}
  Serial.println("[ota] previous image not bootable, keeping this one");endTrial();
}
void eventJson(JsonObject o,uint32_t seq,const LogRecord& e){
  char text[200];eventText(e,text,sizeof text);
  o["seq"]=seq;o["time"]=e.time;o["timeValid"]=e.time>=LOG_VALID_TIME;o["boot"]=e.boot;o["kind"]=eventKindName(e.kind);
  o["code"]=e.code;o["value"]=e.value;o["extra"]=e.extra;o["text"]=String(text);
}
// Lets the panel finish its first refresh after AP/STA radio start-up, not during it.
constexpr uint32_t AUTO_RENDER_DELAY_MS=20000;
// The panel refresh together with the AP+STA radio browns out the board's supply
// (owner logs 0.1.4/0.1.5), so the radio stays off while the panel refreshes. The
// grace period lets the HTTP reply that requested the refresh leave first.
constexpr bool RADIO_OFF_DURING_REFRESH=true;
constexpr uint32_t RADIO_OFF_GRACE_MS=1500;
int pendingJob=-1;uint32_t pendingSince=0,refreshStartedAt=0;int refreshBatteryMv=0;bool radioPaused=false;
// Battery mode: radio off and light sleep between short wakes (next schedule event, every
// POWER_WAKE_MINUTES for the CRM, or the board button). Light sleep, not deep sleep: the
// only button is GPIO0, a boot strap, and a deep-sleep wake while it is held would start
// the ROM download mode instead of the firmware.
constexpr uint32_t POWER_WAKE_MINUTES=15,BUTTON_AWAKE_MS=180000,TIMER_AWAKE_MS=60000,REQUEST_AWAKE_MS=600000;
// Battery mode is either set by hand (powerManual) or follows the power source (powerAuto):
// off on USB or while the source is not known yet, on when running from the battery.
bool powerSave=false,powerManual=false,powerAuto=true,sessionWake=true,crmDoneThisWake=false;uint32_t awakeUntil=0;const char* wakeReason="boot";
uint32_t sleepTestAt=0,sleepTestSeconds=0,wokeAt=0;bool timeSyncWanted=false;
volatile uint32_t ntpSyncedAt=0;
void onTimeSync(struct timeval*){ntpSyncedAt=millis();}
void stayAwake(uint32_t ms){if((int32_t)(millis()+ms-awakeUntil)>0)awakeUntil=millis()+ms;}
// The access point is for setup only: on with no saved network, after the office network
// has been gone for a minute (not in battery mode), or for 10 minutes after holding the
// board button for a second. It is never dropped while a phone is still connected to it.
constexpr uint32_t AP_BUTTON_MS=600000,AP_FALLBACK_MS=60000;
uint32_t apUntil=0,staLostAt=0,buttonDownAt=0;
// A freshly updated image stays pending until it has run for OTA_CONFIRM_MS; a reset
// before that makes the bootloader return to the previous image.
extern "C" bool verifyRollbackLater(){return true;}
constexpr uint32_t OTA_CONFIRM_MS=60000;
bool otaPending=false,otaOK=false;std::atomic<bool> otaBusy(false);String otaError;size_t otaBytes=0;uint32_t restartAt=0;
extern "C" const char FW_MARKER[]=AVIOR_FW_MARKER; // printed at boot, so the linker keeps it in the image
// Trial of a new image, on top of the bootloader's rollback (which the factory bootloader
// may lack): an image that resets three times before it is confirmed, or a CRM-installed
// image that cannot reach the CRM in 10 minutes, switches back to the previous one itself.
constexpr uint32_t TRIAL_CRM_MS=600000;
const char* DEFAULT_QR_URL="https://avior.moscow/na-svyazi/";
bool qrEnabled=true;String qrUrl=DEFAULT_QR_URL;QrMatrix qr;
QrMatrix* qrOut=nullptr;
void qrCapture(esp_qrcode_handle_t h){
  int n=esp_qrcode_get_size(h);if(!qrOut||n<=0||n>QR_MAX_SIZE)return;
  qrOut->size=n;for(int y=0;y<n;y++)for(int x=0;x<n;x++)if(esp_qrcode_get_module(h,x,y))qrOut->set(x,y);
}
// Runs in the loop task only and never during a refresh, so the worker reads a stable matrix.
bool makeQr(const String& text){
  QrMatrix m;qrOut=&m;esp_qrcode_config_t c{qrCapture,10,ESP_QRCODE_ECC_MED};
  bool ok=esp_qrcode_generate(&c,text.c_str())==ESP_OK&&m.size>0;qrOut=nullptr;
  if(ok)qr=m;return ok;
}
// Avior CRM: the sign is behind the office NAT, so it polls the CRM, reports its state
// and applies items whose CRM seq is newer than the one it already applied.
const char* DEFAULT_CRM_URL="https://cms.avior.moscow";
bool crmEnabled=false;String crmUrl=DEFAULT_CRM_URL,crmToken,crmError;
uint32_t crmLastTry=0,crmLastOk=0,crmIntervalMs=10000;bool crmEverOk=false;
const char* CRM_ITEMS[]={"status","schedule","qr","open","closed","soon","image","power","wake","battery","firmware","panel"};
constexpr int CRM_ITEM_COUNT=12;
uint32_t crmApplied[CRM_ITEM_COUNT]={0};
uint32_t crmEventsAck=0; // last event-log seq the CRM confirmed it stored
Preferences crmPrefs;
const char* crmWaitReason();
int adcMv[ADC_PROBE_COUNT]={0};int batteryPin=-1,batteryMv=0,batteryRawMv=0;
// Battery calibration from the CRM (battery.h): voltage correction and the sign's own curve.
int batteryScale=1000;int16_t batteryCurve[CURVE_POINTS];bool batteryCurveSet=false;
int percentNow(){return batteryCurveSet?curvePercent(batteryCurve,batteryRawMv):batteryPercent(batteryMv);}
// Skipped during a refresh: the panel load would read as a discharged battery.
void sampleBattery(){
  if(rendering)return;
  int best=0;batteryPin=-1;
  for(int i=0;i<ADC_PROBE_COUNT;i++){
    uint32_t sum=0;for(int k=0;k<8;k++)sum+=analogReadMilliVolts(ADC_PROBE_PINS[i]);adcMv[i]=sum/8;
    if(plausibleBattery(adcMv[i])&&adcMv[i]>best){best=adcMv[i];batteryPin=ADC_PROBE_PINS[i];}
  }
  batteryRawMv=best*BATTERY_DIVIDER;batteryMv=batteryRawMv*batteryScale/1000;
}
// Low battery for the event log: going down past 15 % and 5 %, and back above 25 %.
uint8_t batteryLevel=0;
void trackBattery(){
  if(batteryPin<0||rendering)return;
  int p=percentNow();uint8_t level=p<=5?2:p<=15?1:0;
  if(level>batteryLevel||(batteryLevel&&p>=25)){batteryLevel=level;logEvent(EV_BATTERY,p,batteryMv);}
}
// Refreshed every 5 s in RTC memory, which keeps its content through a reset but not
// through a power loss: what the sign was doing right before an unexpected reset.
constexpr uint32_t SNAPSHOT_MAGIC=0xA51A0042;
struct ResetSnapshot {uint32_t magic,time,uptime,minHeap;int16_t batteryMv;uint8_t flags,boot;};
RTC_NOINIT_ATTR ResetSnapshot snapshot;
void takeSnapshot(uint8_t flags=0){
  if(RECOVERY_MODE)return;
  if(rendering)flags|=SNAP_RENDERING;
  if(WiFi.status()==WL_CONNECTED)flags|=SNAP_WIFI;
  if(powerSave)flags|=SNAP_POWER_SAVE;
  snapshot={SNAPSHOT_MAGIC,logNow(),millis()/1000,esp_get_minimum_free_heap_size(),(int16_t)batteryMv,flags,(uint8_t)bootCount};
}
// Wi-Fi drops for the event log. The disconnect reasons come from the Wi-Fi event task: the
// first one after a working connection (why it dropped) and the latest (why it cannot return).
volatile uint8_t wifiFirstReason=0,wifiLastReason=0;
bool netUp=false,netEverUp=false,netBootMissed=false;uint32_t netDownSince=0,netLostAt=0,radioOnAt=0;int netRssi=0;
void onWifiDisconnect(WiFiEvent_t,WiFiEventInfo_t info){
  uint8_t r=info.wifi_sta_disconnected.reason;if(!wifiFirstReason)wifiFirstReason=r;wifiLastReason=r;
}
// Every second. A drop counts after 15 s without the network while the radio is meant to be
// on: the planned radio-off during a refresh or a battery-mode sleep is not a drop.
void trackWifi(){
  wifi_mode_t mode=WiFi.getMode();
  if(RECOVERY_MODE||radioPaused||ssid.isEmpty()||(mode!=WIFI_STA&&mode!=WIFI_AP_STA)){netDownSince=0;return;}
  if(WiFi.status()==WL_CONNECTED){
    netRssi=WiFi.RSSI();netDownSince=0;wifiFirstReason=0;
    if(!netUp){
      if(!netEverUp)logEvent(EV_WIFI_UP,netRssi,millis()/1000,1);
      else logEvent(EV_WIFI_UP,netRssi,(millis()-netLostAt)/1000,powerSave?2:0);
      netUp=netEverUp=true;
    }
    return;
  }
  if(!netDownSince)netDownSince=millis()|1;
  if(millis()-netDownSince<15000||millis()-radioOnAt<20000)return;
  if(netUp){netUp=false;netLostAt=netDownSince;logEvent(EV_WIFI_DOWN,wifiFirstReason,netRssi,wifiLastReason);}
  else if(!netEverUp&&!netBootMissed&&millis()>=60000){netBootMissed=true;logEvent(EV_WIFI_DOWN,wifiFirstReason,1,wifiLastReason);}
}
// Charging for the panel, the CRM and the log (inferred from the voltage, battery.h). Samples
// near a refresh, a wake or a radio start are skipped: the load change would look like USB.
ChargeTracker charge;uint32_t chargeSince=0;int chargeSamples=0; // trusted samples since the last wake
void applyPower(int source);
void chargeChanged(ChargeState before);
void trackCharge(){
  if(batteryPin<0||rendering||radioPaused)return;
  if((lastRefresh&&millis()-lastRefresh<30000)||millis()-wokeAt<20000||millis()-radioOnAt<20000)return;
  ChargeState before=charge.state;charge.update(millis(),batteryRawMv);chargeSamples++;chargeChanged(before);
}
void chargeChanged(ChargeState before){
  ChargeState now=charge.state;
  if(now==before)return;
  if(now==CHARGE_ON)chargeSince=millis();
  if(before!=CHARGE_UNKNOWN||now!=CHARGE_OFF) // the first estimate after boot is no event
    logEvent(EV_CHARGE,now,batteryMv,now==CHARGE_FULL&&chargeSince?(millis()-chargeSince)/1000:0);
  if(now==CHARGE_OFF)chargeSince=0;
  applyPower(2);
}
uint32_t scanStarted=0;
struct WifiSnapshot {
  WifiEvidence evidence{};
  IPAddress address;
  esp_err_t modeError=ESP_OK,associationError=ESP_OK,ipError=ESP_OK;
  StationState state()const{return stationState(evidence);}
};
WifiSnapshot wifiSnapshot(){
  WifiSnapshot s;wifi_mode_t mode=WIFI_MODE_NULL;
  s.evidence.arduinoStatus=(int)WiFi.status();
  s.modeError=esp_wifi_get_mode(&mode);s.evidence.modeKnown=s.modeError==ESP_OK;
  s.evidence.stationEnabled=s.evidence.modeKnown&&(mode==WIFI_MODE_STA||mode==WIFI_MODE_APSTA);
  if(s.evidence.stationEnabled){
    wifi_ap_record_t ap{};
    s.associationError=esp_wifi_sta_get_ap_info(&ap);
    s.evidence.associationKnown=s.associationError==ESP_OK||s.associationError==ESP_ERR_WIFI_NOT_CONNECT;
    s.evidence.associated=s.associationError==ESP_OK;
    esp_netif_t* netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    s.evidence.interfaceUp=netif&&esp_netif_is_netif_up(netif);
    s.ipError=netif?esp_netif_get_ip_info(netif,&ip):ESP_ERR_NOT_FOUND;
    s.evidence.hasAddress=s.ipError==ESP_OK&&ip.ip.addr!=0;
    if(s.evidence.hasAddress)s.address=IPAddress(ip.ip.addr);
  }
  return s;
}
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
// Panel login (0.4.4): a login and password set at first setup or from the CRM. Only the
// PBKDF2-SHA256 of the password is stored (the CRM computes the same). A session is a cookie
// signed with sessionKey: no session storage, survives reboots, ends on a password change.
// The API key (token) still works as a Bearer header for scripts.
String panelUser,panelSalt,panelHash;uint8_t sessionKey[32];
constexpr int PBKDF2_ROUNDS=2000;constexpr uint32_t SESSION_SECONDS=30UL*86400;
int loginFails=0;uint32_t loginLockUntil=0;bool buttonResetDone=false;
String hexOf(const uint8_t* b,size_t n){String s;char h[3];for(size_t i=0;i<n;i++){snprintf(h,3,"%02x",b[i]);s+=h;}return s;}
bool isHex(const String& s,size_t n){if(s.length()!=n)return false;for(char c:s)if(!isxdigit((unsigned char)c))return false;return true;}
String passwordHash(const String& password,const String& saltHex){
  uint8_t salt[16],out[32];for(int i=0;i<16;i++)salt[i]=strtoul(saltHex.substring(2*i,2*i+2).c_str(),nullptr,16);
  mbedtls_md_context_t c;mbedtls_md_init(&c);mbedtls_md_setup(&c,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);
  mbedtls_pkcs5_pbkdf2_hmac(&c,(const uint8_t*)password.c_str(),password.length(),salt,16,PBKDF2_ROUNDS,32,out);mbedtls_md_free(&c);
  return hexOf(out,32);
}
bool passwordSet(){return isHex(panelHash,64)&&isHex(panelSalt,32)&&panelUser.length();}
bool validLogin(const String& user,const String& password){return user.length()>=1&&user.length()<=32&&password.length()>=8&&password.length()<=64;}
void newSessionKey(){esp_fill_random(sessionKey,sizeof sessionKey);prefs.putBytes("sessKey",sessionKey,sizeof sessionKey);}
void savePanelLogin(const String& user,const String& salt,const String& hash,int source){
  panelUser=user;panelSalt=salt;panelHash=hash;prefs.putString("pUser",user);prefs.putString("pSalt",salt);prefs.putString("pHash",hash);
  newSessionKey();logEvent(EV_ACCESS,ACCESS_SET,source);
}
void setPanelPassword(const String& user,const String& password,int source){
  uint8_t salt[16];esp_fill_random(salt,16);String s=hexOf(salt,16);savePanelLogin(user,s,passwordHash(password,s),source);
}
// expiry 0: the clock was not set at login, so the session lasts until the next reboot.
String sessionValue(uint32_t expiry){
  String msg=String(expiry)+"|"+panelUser+(expiry?"":"|"+String(bootCount));uint8_t mac[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),sessionKey,sizeof sessionKey,(const uint8_t*)msg.c_str(),msg.length(),mac);
  return String(expiry)+"."+hexOf(mac,16);
}
bool sessionValid(){
  if(!passwordSet())return false;
  String c=web.header("Cookie");int at=c.indexOf("avior_session=");if(at<0)return false;
  int end=c.indexOf(';',at);String v=c.substring(at+14,end<0?c.length():end);v.trim();
  int dot=v.indexOf('.');if(dot<1)return false;
  uint32_t expiry=strtoul(v.substring(0,dot).c_str(),nullptr,10);time_t now=time(nullptr);
  if(expiry&&now>1700000000&&now>(time_t)expiry)return false;
  return sessionValue(expiry)==v;
}
void sendSession(bool on){
  time_t now=time(nullptr);uint32_t expiry=now>1700000000?(uint32_t)now+SESSION_SECONDS:0;
  web.sendHeader("Set-Cookie",on?"avior_session="+sessionValue(expiry)+"; Path=/; HttpOnly; SameSite=Strict"+(expiry?"; Max-Age="+String(SESSION_SECONDS):""):
    String("avior_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict"));
}
bool keyAuthorized(){return web.header("Authorization")=="Bearer "+token;}
bool authorized(){return keyAuthorized()||sessionValid();}
// The first password needs proof of presence: a phone on the sign's own access point (which
// the board button turns on), the API key, or the CRM.
bool onAccessPoint(){return web.client().localIP()==WiFi.softAPIP();}
// Only changes keep a battery-mode sign awake: an open panel polls state every 5 s.
bool auth(){if(authorized()){if(powerSave&&web.method()!=HTTP_GET)stayAwake(60000);return true;}error(401,"Войдите в панель: логин и пароль");return false;}
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
Preferences tracePrefs;
void clearRenderBlock(){
  if(RECOVERY_MODE||interruptedPhase==PHASE_IDLE)return;
  interruptedPhase=PHASE_IDLE;tracePrefs.putUChar("blocked",PHASE_IDLE);
}
// A manual status keeps the schedule on and holds until its next event; without a set clock
// that moment is unknown, so the schedule is turned off as before.
void setManual(int s,const char* from){
  desired=s;source=from;prefs.putInt("desired",s);displayError="";clearRenderBlock();
  time_t now=time(nullptr);
  if(now>1700000000)manualAt=now;else{automatic=false;manualAt=0;}
  prefs.putUInt("manualAt",manualAt);saveSchedule();
}
#if !AVIOR_RECOVERY
void renderPhase(uint8_t p){
  tracePrefs.putUChar("phase",p);
  Serial.printf("[display] phase=%s t_ms=%lu heap=%u stack_free=%u\n",phaseName(p),(unsigned long)millis(),ESP.getFreeHeap(),(unsigned)uxTaskGetStackHighWaterMark(nullptr));
}
void worker(void*) {
  uint8_t* band=(uint8_t*)malloc(BAND_BYTES);
  workerStackFree=uxTaskGetStackHighWaterMark(nullptr);
  int s;
  for(;;){
    xQueueReceive(jobs,&s,portMAX_DELAY);bool ok=band!=nullptr;
    File asset;bool loaded=false,readOk=true;
    if(ok&&fsReady){asset=LittleFS.open(assetPath(s),"r");loaded=asset&&asset.size()==FRAME_BYTES;}
    if(!loaded && s==3)ok=false;
    if(ok){
      Serial.printf("[display] start status=%s\n",keys[s]);renderPhase(PHASE_START);
      Panel panel;panel.phase=renderPhase;
      ComposeInput in{s,qrEnabled?&qr:nullptr,batteryPin>=0&&percentNow()<=15};
      ok=panel.show(band,[&](uint8_t* buf,int b){readOk&=composeRows(buf,b*BAND_ROWS,BAND_ROWS,in,loaded?&asset:(File*)nullptr);})&&readOk;
      renderPhase(PHASE_IDLE);Serial.printf("[display] complete ok=%d\n",ok);
    }
    workerStackFree=uxTaskGetStackHighWaterMark(nullptr);
    Result r{s,ok};xQueueSend(results,&r,portMAX_DELAY);
  }
}
#endif
void requestDisplay(){
  if(RECOVERY_MODE)return;
  if(!confirmed || rendering || !fsReady && desired==3 || desired==displayed)return;
  if(lastRefresh && millis()-lastRefresh<60000)return;
  if(millis()<AUTO_RENDER_DELAY_MS||otaBusy||restartAt)return;
  rendering=true;pendingJob=desired;pendingSince=millis();
}
bool accessPointWanted(){
  if(RECOVERY_MODE||ssid.isEmpty()||(int32_t)(apUntil-millis())>0)return true;
  return !powerSave&&staLostAt&&millis()-staLostAt>=AP_FALLBACK_MS;
}
String apName(){return "AviorSign-"+String((uint32_t)ESP.getEfuseMac(),HEX);}
bool startRadio(){
  bool ap=accessPointWanted();radioOnAt=millis();wifiFirstReason=0;
  WiFi.mode(RECOVERY_MODE?WIFI_AP:ap?WIFI_AP_STA:WIFI_STA);WiFi.setHostname("avior");
  bool apOK=ap&&WiFi.softAP(apName().c_str(),apPassword.c_str());
  if(RECOVERY_MODE)WiFi.setTxPower(WIFI_POWER_8_5dBm);
  // Wi-Fi recovery deliberately waits for a panel request, even with saved credentials.
  // This preserves an AP-only baseline until the owner starts the next test.
  if(!RECOVERY_MODE&&ssid.length())WiFi.begin(ssid.c_str(),wifiPassword.c_str());
  return apOK;
}
void manageAccessPoint(){
  wifi_mode_t mode=WiFi.getMode();
  if(RECOVERY_MODE||radioPaused||mode==WIFI_OFF)return;
  if(WiFi.status()==WL_CONNECTED)staLostAt=0;else if(!staLostAt)staLostAt=millis()|1;
  bool on=mode==WIFI_AP_STA,want=accessPointWanted();
  if(want&&!on){
    bool ok=WiFi.mode(WIFI_AP_STA)&&WiFi.softAP(apName().c_str(),apPassword.c_str());Serial.printf("[radio] access point on: %s\n",ok?"ready":"failed");
    logEvent(EV_ACCESS_POINT,ok?1:-1,ssid.isEmpty()?AP_NO_NETWORK:(int32_t)(apUntil-millis())>0?AP_BUTTON:AP_NETWORK_LOST);
  }
  else if(!want&&on&&WiFi.softAPgetStationNum()==0){WiFi.enableAP(false);Serial.println("[radio] access point off: office network is up");logEvent(EV_ACCESS_POINT,0,AP_NETWORK_BACK);}
}
void pollButton(){
  if(digitalRead(0)!=LOW){buttonDownAt=0;buttonResetDone=false;return;}
  if(!buttonDownAt){buttonDownAt=millis()|1;return;}
  // Held 10 s: forget the panel password; the access point (on after 1 s) lets you set a new one.
  if(millis()-buttonDownAt>=10000&&!buttonResetDone&&passwordSet()){
    buttonResetDone=true;panelHash="";prefs.remove("pHash");newSessionKey();logEvent(EV_ACCESS,ACCESS_RESET);
  }
  if(millis()-buttonDownAt<1000||(int32_t)(apUntil-millis())>0)return;
  apUntil=millis()+AP_BUTTON_MS;if(powerSave){sessionWake=true;stayAwake(AP_BUTTON_MS);}
  Serial.println("[radio] button held: access point for 10 minutes");
}
void resumeRadio(){
  if(!radioPaused)return;
  radioPaused=false;bool apOK=startRadio();Serial.printf("[radio] on access_point=%s\n",apOK?"ready":"failed");
}
void state(){
  WifiSnapshot wifi=wifiSnapshot();
  JsonDocument d;d["version"]=FW_VERSION;d["recovery"]=RECOVERY_MODE;d["mode"]=MODE_NAME;d["wifiSetupAllowed"]=canMutate(true);d["resetReason"]=resetName(bootReason);d["resetReasonCode"]=(int)bootReason;d["board"]="ESP32-M075 V3.0";d["panel"]="GDEM075F53 / JD79665CA";
  d["desired"]=keys[desired];d["displayed"]=displayed<0?"unknown":keys[displayed];d["busy"]=rendering.load();
  d["displayError"]=displayError;d["hardwareConfirmed"]=confirmed;d["storageReady"]=fsReady;
  d["automatic"]=automatic;d["manualUntilEvent"]=automatic&&manualAt>0;d["source"]=source;d["ip"]=wifi.state()==StationState::Connected?wifi.address.toString():"0.0.0.0";d["apIP"]=WiFi.softAPIP().toString();
  d["wifiConnected"]=wifi.state()==StationState::Connected;d["wifiStatus"]=stationStateName(wifi.state());
  d["wifiArduinoStatus"]=wifi.evidence.arduinoStatus;d["wifiAssociated"]=wifi.evidence.associated;
  d["wifiNetifUp"]=wifi.evidence.interfaceUp;d["wifiHasAddress"]=wifi.evidence.hasAddress;
  d["wifiModeError"]=wifi.modeError;d["wifiAssociationError"]=wifi.associationError;d["wifiIpError"]=wifi.ipError;
  d["timeValid"]=time(nullptr)>1700000000;
  d["unixTime"]=(int64_t)time(nullptr);d["heap"]=ESP.getFreeHeap();d["uptimeSeconds"]=millis()/1000;
  d["otaPending"]=otaPending;d["qrEnabled"]=qrEnabled;d["qrUrl"]=qrUrl;
  d["accessPoint"]=WiFi.getMode()==WIFI_AP_STA||WiFi.getMode()==WIFI_AP;d["apName"]=apName();
  d["panelUser"]=passwordSet()?panelUser:"";
  d["batterySave"]=powerSave;d["powerAuto"]=powerAuto;d["powerManual"]=powerManual;d["wakeMinutes"]=POWER_WAKE_MINUTES;d["wakeReason"]=wakeReason;
  if(powerSave)d["awakeSeconds"]=(int32_t)(awakeUntil-millis())>0?(awakeUntil-millis())/1000:0;
  {auto c=d["crm"].to<JsonObject>();c["enabled"]=crmEnabled;c["url"]=crmUrl;c["configured"]=crmToken.length()>0;c["error"]=crmError;
   if(const char* wait=crmWaitReason())c["waiting"]=wait;
   if(crmEverOk)c["lastOkSeconds"]=(millis()-crmLastOk)/1000;}
  d["minHeap"]=esp_get_minimum_free_heap_size();d["workerStackFree"]=workerStackFree.load();
  {auto l=d["log"].to<JsonObject>();l["next"]=logNext;l["boot"]=bootCount;nvs_stats_t st;
   if(nvs_get_stats(nullptr,&st)==ESP_OK){l["nvsUsed"]=st.used_entries;l["nvsFree"]=st.free_entries;}}
  d["interruptedPhase"]=phaseName(interruptedPhase);d["renderAborts"]=renderAborts;
  if(batteryPin>=0){auto b=d["battery"].to<JsonObject>();b["pin"]=batteryPin;b["mv"]=batteryMv;b["raw"]=batteryRawMv;b["percent"]=percentNow();b["divider"]=BATTERY_DIVIDER;b["charge"]=chargeName(charge.state);
    b["scale"]=batteryScale;b["curve"]=batteryCurveSet;}
  auto probe=d["adcProbe"].to<JsonObject>();for(int i=0;i<ADC_PROBE_COUNT;i++)probe[String(ADC_PROBE_PINS[i])]=adcMv[i];
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
void crmResetApplied(){for(int i=0;i<CRM_ITEM_COUNT;i++){crmApplied[i]=0;crmPrefs.putUInt(CRM_ITEMS[i],0);}crmEventsAck=0;crmPrefs.putUInt("evAck",0);}
void crmMarkApplied(int item,uint32_t seq){crmApplied[item]=seq;crmPrefs.putUInt(CRM_ITEMS[item],seq);}
String crmReport(){
  JsonDocument d;WifiSnapshot wifi=wifiSnapshot();
  d["version"]=FW_VERSION;d["mode"]=MODE_NAME;d["desired"]=keys[desired];d["displayed"]=displayed<0?"unknown":keys[displayed];
  d["busy"]=rendering.load();d["displayError"]=displayError;d["automatic"]=automatic;d["manualUntilEvent"]=automatic&&manualAt>0;d["source"]=source;
  d["uptimeSeconds"]=millis()/1000;d["resetReason"]=resetName(bootReason);d["heap"]=ESP.getFreeHeap();d["otaPending"]=otaPending;
  d["ip"]=wifi.state()==StationState::Connected?wifi.address.toString():"";d["rssi"]=WiFi.RSSI();
  d["interruptedPhase"]=phaseName(interruptedPhase);d["storageReady"]=fsReady;
  d["powerSave"]=powerSave;d["powerAuto"]=powerAuto;d["trial"]=trial;d["wakeMinutes"]=POWER_WAKE_MINUTES;d["wakeReason"]=wakeReason;
  if(powerSave)d["awakeSeconds"]=(int32_t)(awakeUntil-millis())>0?(awakeUntil-millis())/1000:0;
  if(batteryPin>=0){auto b=d["battery"].to<JsonObject>();b["pin"]=batteryPin;b["mv"]=batteryMv;b["raw"]=batteryRawMv;b["percent"]=percentNow();b["charge"]=chargeName(charge.state);b["scale"]=batteryScale;b["curve"]=batteryCurveSet;}
  auto q=d["qr"].to<JsonObject>();q["enabled"]=qrEnabled;q["url"]=qrUrl;
  d["localKey"]=token; // the API key and the access point password, shown in the CRM to owner and admins only
  d["apKey"]=apPassword;d["panelUser"]=passwordSet()?panelUser:"";
  JsonDocument sched;scheduleJson(sched);d["schedule"]=sched;
  auto a=d["assets"].to<JsonObject>();for(int i=0;i<4;i++)a[keys[i]]=fsReady&&LittleFS.exists(assetPath(i));
  auto ap=d["applied"].to<JsonObject>();for(int i=0;i<CRM_ITEM_COUNT;i++)ap[CRM_ITEMS[i]]=crmApplied[i];
  // Event log entries the CRM has not confirmed yet, oldest first, a few per exchange.
  auto lg=d["log"].to<JsonObject>();lg["next"]=logNext;lg["boot"]=bootCount;auto evs=lg["events"].to<JsonArray>();
  for(uint32_t s=std::max(crmEventsAck+1,logFirst()),n=0;s<logNext&&n<8;s++)if(logRing[s%LOG_CAPACITY].kind){eventJson(evs.add<JsonObject>(),s,logRing[s%LOG_CAPACITY]);n++;}
  String out;serializeJson(d,out);return out;
}
// One HTTPS request to the CRM with the sign token; plain http only for a test server.
int crmRequest(HTTPClient& http,WiFiClientSecure& tls,WiFiClient& plain,const String& url){
  if(url.startsWith("https://")){tls.setCACert(CRM_ROOT_CAS);if(!http.begin(tls,url))return -1;}
  else if(!http.begin(plain,url))return -1;
  http.setTimeout(8000);http.addHeader("Authorization","Bearer "+crmToken);http.addHeader("X-Sign-Token",crmToken);
  return 0;
}
String crmFail(HTTPClient& http,int code){
  if(code<0)return String("Нет связи с CRM: ")+HTTPClient::errorToString(code);
  JsonDocument e;String body=http.getString();
  return "CRM ответила "+String(code)+(deserializeJson(e,body)||!e["error"].is<const char*>()?String():String(": ")+e["error"].as<const char*>());
}
// Downloads one 96000-byte frame into LittleFS through a temporary file.
bool crmDownload(int slot){
  if(!fsReady){crmError="Хранилище картинок не подготовлено — картинка из CRM ждёт";return false;}
  HTTPClient http;WiFiClientSecure tls;WiFiClient plain;
  if(crmRequest(http,tls,plain,crmUrl+"/api/sign/asset.php?slot="+keys[slot])<0){crmError="Неверный адрес CRM";return false;}
  int code=http.GET(),size=code==200?http.getSize():0;
  if(code!=200||(size!=-1&&size!=(int)FRAME_BYTES)){crmError=crmFail(http,code);http.end();return false;}
  File f=LittleFS.open("/crm.tmp","w");int written=f?http.writeToStream(&f):-1;if(f)f.close();http.end();
  if(written!=(int)FRAME_BYTES||!LittleFS.rename("/crm.tmp",assetPath(slot))){LittleFS.remove("/crm.tmp");crmError="Картинка из CRM не сохранилась";return false;}
  return true;
}
// source: 0 panel, 1 CRM, 2 automatic (power source changed).
void applyPower(int source){
  bool on=!RECOVERY_MODE&&(powerAuto?charge.state==CHARGE_OFF:powerManual);
  if(on==powerSave)return;
  powerSave=on;sessionWake=true;stayAwake(60000);
  logEvent(EV_POWER,on,source);
}
void setPowerMode(bool autoMode,bool manualOn,int source){
  powerAuto=autoMode;powerManual=manualOn;prefs.putBool("powerAuto",autoMode);prefs.putBool("power",manualOn);
  applyPower(source);
}
bool setBatteryCalibration(int scale,const int16_t* curve,bool persist=true){
  if(!validScale(scale)||(curve&&!validCurve(curve)))return false;
  batteryScale=scale;batteryCurveSet=curve!=nullptr;if(curve)memcpy(batteryCurve,curve,sizeof batteryCurve);
  if(persist){prefs.putInt("batScale",scale);if(curve)prefs.putBytes("batCurve",batteryCurve,sizeof batteryCurve);else prefs.remove("batCurve");}
  batteryMv=batteryRawMv*batteryScale/1000;
  Serial.printf("[battery] calibration scale=%d curve=%d\n",scale,batteryCurveSet);
  return true;
}
// Firmware uploaded in the CRM: streamed into the inactive OTA slot, checked against its
// SHA-256 and by the image check of Update before it may boot.
int crmFwFails=0;
bool crmFirmware(uint32_t seq,JsonObjectConst f){
  uint32_t size=f["size"]|0;String sha=f["sha256"]|"";
  if(size<100000||size>0x140000||sha.length()!=64){crmError="Прошивка в CRM не прошла проверку";return false;}
  Serial.printf("[ota] downloading %s from the CRM, %u bytes\n",(const char*)(f["version"]|"?"),(unsigned)size);
  otaBusy=true;bool ok=false;
  HTTPClient http;WiFiClientSecure tls;WiFiClient plain;
  if(crmRequest(http,tls,plain,crmUrl+"/api/sign/firmware.php?seq="+String(seq))==0){
    http.setTimeout(15000);int code=http.GET();
    if(code!=200||http.getSize()!=(int)size)crmError="Прошивка из CRM: "+crmFail(http,code);
    else if(!Update.begin(size,U_FLASH))crmError=String("Прошивка из CRM: ")+Update.errorString();
    else{
      WiFiClient* in=http.getStreamPtr();mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_starts_ret(&c,0);
      static uint8_t buf[2048];uint32_t got=0,last=millis();
      while(got<size&&millis()-last<20000){
        size_t n=in->available();
        if(!n){if(!http.connected())break;delay(2);continue;}
        n=in->readBytes(buf,std::min<size_t>(std::min<size_t>(n,sizeof buf),size-got));
        if(!n)continue;
        if(Update.write(buf,n)!=n)break;
        mbedtls_sha256_update_ret(&c,buf,n);got+=n;last=millis();
      }
      uint8_t d[32];char hex[65];mbedtls_sha256_finish_ret(&c,d);mbedtls_sha256_free(&c);
      for(int i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",d[i]);
      if(got!=size)crmError="Прошивка из CRM скачалась не полностью";
      else if(!sha.equalsIgnoreCase(hex))crmError="Прошивка из CRM повреждена: контрольная сумма не совпала";
      else if(!Update.end(true))crmError=String("Прошивка из CRM не прошла проверку: ")+Update.errorString();
      else ok=true;
      if(!ok&&Update.isRunning())Update.abort();
    }
  }else crmError="Неверный адрес CRM";
  http.end();otaBusy=false;
  Serial.printf("[ota] CRM download %s\n",ok?"verified":crmError.c_str());
  return ok;
}
// Applies what the CRM changed since the last applied seq; returns true when something changed.
bool crmApply(JsonDocument& r){
  bool changed=false;
  for(int slot=0;slot<4;slot++){
    JsonObject a=r["assets"][keys[slot]];uint32_t seq=a["seq"]|0;
    if(!seq||seq<=crmApplied[3+slot])continue;
    bool ok=a["present"].as<bool>()?crmDownload(slot):(!fsReady||!LittleFS.exists(assetPath(slot))||LittleFS.remove(assetPath(slot)));
    if(!ok)continue;
    crmMarkApplied(3+slot,seq);changed=true;if(slot==desired)displayed=-1;
    Serial.printf("[crm] picture %s %s\n",keys[slot],a["present"].as<bool>()?"downloaded":"removed");
  }
  uint32_t seq=r["qr"]["seq"]|0;
  if(seq>crmApplied[2]){
    String url=r["qr"]["url"]|"";
    if(makeQr(url)){qrEnabled=r["qr"]["enabled"]|true;qrUrl=url;prefs.putBool("qrOn",qrEnabled);prefs.putString("qrUrl",qrUrl);
      if(desired<3&&!(fsReady&&LittleFS.exists(assetPath(desired))))displayed=-1;changed=true;}
    crmMarkApplied(2,seq);
  }
  // A manual status turns the schedule off and a saved schedule turns it on: apply both in CRM order.
  uint32_t statusSeq=r["status"]["seq"]|0,scheduleSeq=r["schedule"]["seq"]|0;
  auto applySchedule=[&]{
    if(scheduleSeq<=crmApplied[1])return;
    JsonDocument sd;sd.set(r["schedule"]["value"]);Event next[32];int n,offset;
    if(parseSchedule(sd,next,n,offset)){memcpy(events,next,n*sizeof(Event));eventCount=n;utcOffset=offset;automatic=sd["enabled"];if(automatic)source="schedule";manualAt=0;prefs.putUInt("manualAt",0);saveSchedule();changed=true;}
    else crmError="Расписание из CRM не прошло проверку";
    crmMarkApplied(1,scheduleSeq);
  };
  auto applyStatus=[&]{
    if(statusSeq<=crmApplied[0])return;
    int s=statusIndex(r["status"]["value"]|"");
    if(s==3&&!(fsReady&&LittleFS.exists(assetPath(3))))crmError="В CRM выбрано «Своё изображение», но на вывеске его нет";
    else if(s>=0){setManual(s,"crm");changed=true;}
    crmMarkApplied(0,statusSeq);
  };
  if(statusSeq<scheduleSeq){applyStatus();applySchedule();}else{applySchedule();applyStatus();}
  seq=r["power"]["seq"]|0;
  if(seq>crmApplied[7]){setPowerMode(r["power"]["auto"]|false,r["power"]["enabled"]|false,1);crmMarkApplied(7,seq);}
  // Battery calibration measured in the CRM: voltage correction and the discharge curve.
  seq=r["battery"]["seq"]|0;
  if(seq>crmApplied[9]){
    int16_t curve[CURVE_POINTS];JsonArrayConst a=r["battery"]["curve"].as<JsonArrayConst>();bool hasCurve=a.size()==CURVE_POINTS;
    for(int i=0;hasCurve&&i<CURVE_POINTS;i++)curve[i]=a[i]|0;
    if(!setBatteryCalibration(r["battery"]["scale"]|1000,hasCurve?curve:nullptr))crmError="Калибровка аккумулятора из CRM не прошла проверку";
    crmMarkApplied(9,seq);
  }
  // Panel login set in the CRM: the CRM sends the salt and PBKDF2 hash, never the password.
  seq=r["panel"]["seq"]|0;
  if(seq>crmApplied[11]){
    String user=r["panel"]["user"]|"",salt=r["panel"]["salt"]|"",hash=r["panel"]["hash"]|"";
    if(user.length()&&user.length()<=32&&isHex(salt,32)&&isHex(hash,64))savePanelLogin(user,salt,hash,1);
    else crmError="Логин панели из CRM не прошёл проверку";
    crmMarkApplied(11,seq);
  }
  // Firmware from the CRM, last: on success the sign restarts into it. A recovery build would
  // cut the CRM link, so the CRM never offers one; the version check is a second guard.
  seq=r["firmware"]["seq"]|0;
  if(seq>crmApplied[10]){
    const char* v=r["firmware"]["version"]|"";
    if(!strcmp(v,FW_VERSION)||strstr(v,"recovery")||RECOVERY_MODE)crmMarkApplied(10,seq);
    else if(!rendering&&!radioPaused&&!restartAt&&!otaBusy){
      if(crmFirmware(seq,r["firmware"])){
        crmMarkApplied(10,seq);crmFwFails=0;startTrial(true);
        logEvent(EV_OTA,OTA_RECEIVED,r["firmware"]["size"]|0,1);restartAt=millis()+1500;
      }else if(++crmFwFails>=3){crmMarkApplied(10,seq);crmFwFails=0;logEvent(EV_OTA,OTA_FAILED);}
    }
  }
  // «Разбудить» from the CRM: stay reachable for 10 minutes from this check on.
  seq=r["wake"]["seq"]|0;
  if(seq>crmApplied[8]){
    if(powerSave){sessionWake=true;stayAwake(REQUEST_AWAKE_MS);wakeReason="crm";}
    crmMarkApplied(8,seq);Serial.println("[power] awake request from CRM");
  }
  return changed;
}
// Why the CRM exchange is not running right now (nullptr: it may run), shown in the panel
// and the health log instead of skipping silently.
const char* crmWaitReason(){
  if(!crmEnabled)return "связь с CRM выключена";
  if(!crmToken.length())return "не введён токен вывески";
  if(rendering||radioPaused)return "обновляется экран";
  if(otaBusy||restartAt)return "идёт обновление прошивки";
  if(WiFi.status()!=WL_CONNECTED)return "нет подключения к офисному Wi-Fi";
  if(time(nullptr)<=1700000000)return "ещё нет точного времени из интернета (нужно для HTTPS)";
  // TLS needs about 40 KB in several blocks.
  if(ESP.getFreeHeap()<50000||ESP.getMaxAllocHeap()<20000)return "мало свободной памяти для HTTPS";
  return nullptr;
}
// The event log gets the first CRM failure, a different one at most every 10 minutes,
// and the recovery, not every failed poll.
bool crmFailing=false;uint32_t crmFailSince=0,crmFailLoggedAt=0;int crmFailCode=0,crmFailKind=0;int32_t crmFailTls=0;
void crmLogFailure(int code,int32_t tls,int kind){
  if(!crmFailing){crmFailing=true;crmFailSince=millis();}
  else if((code==crmFailCode&&tls==crmFailTls&&kind==crmFailKind)||millis()-crmFailLoggedAt<600000)return;
  crmFailCode=code;crmFailTls=tls;crmFailKind=kind;crmFailLoggedAt=millis();logEvent(EV_CRM_ERROR,code,tls,kind);
}
void crmSync(){
  crmLastTry=millis();
  HTTPClient http;WiFiClientSecure tls;WiFiClient plain;
  if(crmRequest(http,tls,plain,crmUrl+"/api/sign/sync.php")<0){crmError="Неверный адрес CRM";crmLogFailure(0,0,CRM_FAIL_ADDRESS);return;}
  http.addHeader("Content-Type","application/json");
  int code=http.POST(crmReport());
  crmDoneThisWake=true; // a failing CRM must not keep a battery-powered sign awake
  if(code!=200){
    crmError=crmFail(http,code);char tlsError[96];int32_t tlsCode=code<0?tls.lastError(tlsError,sizeof tlsError):0;
    if(tlsCode)crmError+=String(" (TLS: ")+tlsError+")";
    http.end();Serial.printf("[crm] %s\n",crmError.c_str());crmLogFailure(code,tlsCode,CRM_FAIL_HTTP);return;
  }
  JsonDocument r;DeserializationError err=deserializeJson(r,http.getString());http.end();
  if(err||!r["ok"].as<bool>()){crmError="Непонятный ответ CRM";crmLogFailure(code,0,CRM_FAIL_ANSWER);return;}
  if(!crmEverOk)Serial.println("[crm] first exchange with the CRM succeeded");
  if(crmFailing){crmFailing=false;logEvent(EV_CRM_OK,0,(millis()-crmFailSince)/1000);}
  crmError="";crmLastOk=millis();crmEverOk=true;
  crmIntervalMs=constrain((int)(r["pollSeconds"]|10),5,300)*1000UL;
  // The CRM confirms the log entries it stored; a backlog goes out in the next exchanges.
  uint32_t ack=r["eventsAck"]|0u;
  if(ack>crmEventsAck&&ack<logNext){
    crmEventsAck=ack;crmPrefs.putUInt("evAck",ack);
    if(ack+1<logNext)crmLastTry=millis()-crmIntervalMs+1500;
  }
  if(crmApply(r)){
    crmDoneThisWake=false; // report the applied change before sleeping
    requestDisplay();
    crmLastTry=millis()-crmIntervalMs+1500; // report the result right away
  }
}
// Light sleep until `seconds` pass or the board button (GPIO0, active low) is pressed.
bool lightSleep(uint32_t seconds){
  Serial.printf("[power] sleep %us battery_mv=%d\n",(unsigned)seconds,batteryMv);Serial.flush();
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds*1000000ULL);
  gpio_wakeup_enable(GPIO_NUM_0,GPIO_INTR_LOW_LEVEL);esp_sleep_enable_gpio_wakeup();
  takeSnapshot(SNAP_SLEEPING);esp_light_sleep_start();takeSnapshot();
  bool button=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO;
  gpio_wakeup_disable(GPIO_NUM_0);esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  return button;
}
// Seconds until the next wake: POWER_WAKE_MINUTES, or earlier for the next schedule event.
uint32_t secondsToWake(){
  uint32_t seconds=POWER_WAKE_MINUTES*60;time_t now=time(nullptr);
  if(automatic&&now>1700000000){
    now+=utcOffset*60;tm t;gmtime_r(&now,&t);int m=minutesToNextEvent(events,eventCount,t.tm_wday,t.tm_hour*60+t.tm_min);
    if(m>0&&(uint32_t)(m*60-t.tm_sec+2)<seconds)seconds=m*60-t.tm_sec+2;
  }
  return seconds<20?20:seconds;
}
// The radio comes on only when needed: the button, the CRM check, or an hourly time sync
// that keeps the schedule accurate while the sleep clock drifts.
void afterWake(bool button,const char* reason){
  wokeAt=millis();chargeSamples=0;wakeReason=button?"button":reason;sessionWake=button;crmLastTry=0;
  awakeUntil=millis()+(button?BUTTON_AWAKE_MS:TIMER_AWAKE_MS);
  bool crmOn=crmEnabled&&crmToken.length();
  timeSyncWanted=!ntpSyncedAt||millis()-ntpSyncedAt>3600000UL;crmDoneThisWake=!crmOn;
  // One quiet battery sample before the radio starts: the same load at every wake.
  sampleBattery();if(batteryPin>=0){ChargeState before=charge.state;charge.update(millis(),batteryRawMv,true);chargeChanged(before);}
  if(button||crmOn||timeSyncWanted){startRadio();if(timeSyncWanted)sntp_restart();}
  Serial.printf("[power] wake %s radio=%d battery_mv=%d\n",wakeReason,WiFi.getMode()!=WIFI_OFF,batteryMv);
}
bool refreshDue(){
  return confirmed&&desired!=displayed&&displayError.isEmpty()&&!blocksAutoRender(interruptedPhase)&&!(desired==3&&!fsReady);
}
// Sleeps once the wake's work is done: a timer wake as soon as the CRM exchange and any
// refresh finish, a button or CRM wake only after its window.
void powerSaveStep(){
  bool window=(int32_t)(millis()-awakeUntil)<0;
  bool busy=rendering||pendingJob>=0||otaBusy||restartAt||scanActive||radioPaused;
  bool netPending=!crmDoneThisWake||(timeSyncWanted&&(int32_t)(ntpSyncedAt-wokeAt)<0);
  if(busy)return;
  if(window&&(sessionWake||netPending||refreshDue()))return;
  if(!window&&refreshDue()&&(uint32_t)(millis()-awakeUntil)<120000)return; // the 60 s refresh cooldown
  afterWake(lightSleep(secondsToWake()),"timer");
}
void firmwareUpload(){
  auto& u=web.upload();
  if(u.status==UPLOAD_FILE_START){
    otaOK=false;otaError="";otaBytes=0;
    if(!authorized()){otaError="Требуется ключ доступа";return;}
    if(rendering||radioPaused){otaError="Дождитесь окончания обновления экрана";return;}
    if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){otaError=String("Не удалось начать обновление: ")+Update.errorString();return;}
    otaBusy=true;otaOK=true;Serial.println("[ota] upload started");
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(!otaOK)return;
    if(otaBytes==0&&u.currentSize&&u.buf[0]!=0xE9){otaError="Это не файл прошивки ESP32 (нужен firmware.bin)";otaOK=false;Update.abort();return;}
    if(Update.write(u.buf,u.currentSize)!=u.currentSize){otaError=String("Ошибка записи: ")+Update.errorString();otaOK=false;Update.abort();return;}
    otaBytes+=u.currentSize;
  }else if(u.status==UPLOAD_FILE_END){
    if(otaOK&&!Update.end(true)){otaError=String("Образ не прошёл проверку: ")+Update.errorString();otaOK=false;}
  }else if(u.status==UPLOAD_FILE_ABORTED){
    if(otaOK)Update.abort();otaOK=false;otaError="Загрузка прервана";
  }
}
void routes(){
  const char* headers[]={"Authorization","Cookie"};web.collectHeaders(headers,2);
  web.on("/api/v1/auth",HTTP_GET,[]{
    JsonDocument d;d["setup"]=!passwordSet();d["loggedIn"]=authorized();d["user"]=passwordSet()?panelUser:"";
    d["setupAllowed"]=!passwordSet()&&(onAccessPoint()||keyAuthorized());reply(200,d);
  });
  web.on("/api/v1/auth/setup",HTTP_POST,[]{
    JsonDocument d;if(!body(d))return;
    if(passwordSet()){error(409,"Пароль уже задан — войдите или сбросьте его кнопкой на плате (держать 10 секунд)");return;}
    if(!onAccessPoint()&&!keyAuthorized()){error(403,"Первый пароль задаётся из CRM или с телефона, подключённого к точке доступа вывески");return;}
    String user=d["user"]|"",password=d["password"]|"";user.trim();
    if(!validLogin(user,password)){error(400,"Логин до 32 символов, пароль от 8 до 64");return;}
    setPanelPassword(user,password,2);sendSession(true);JsonDocument r;r["ok"]=true;reply(200,r);
  });
  web.on("/api/v1/auth/login",HTTP_POST,[]{
    JsonDocument d;if(!body(d))return;
    if((int32_t)(loginLockUntil-millis())>0){error(429,"Слишком много неверных попыток — подождите минуту");return;}
    String user=d["user"]|"",password=d["password"]|"";user.trim();
    if(!passwordSet()||user!=panelUser||passwordHash(password,panelSalt)!=panelHash){
      if(++loginFails>=5){loginFails=0;loginLockUntil=millis()+60000;logEvent(EV_ACCESS,ACCESS_LOCKED);}
      error(401,"Неверный логин или пароль");return;
    }
    loginFails=0;sendSession(true);JsonDocument r;r["ok"]=true;reply(200,r);
  });
  web.on("/api/v1/auth/logout",HTTP_POST,[]{sendSession(false);JsonDocument r;r["ok"]=true;reply(200,r);});
  web.on("/api/v1/auth/password",HTTP_PUT,[]{
    if(!auth())return;JsonDocument d;if(!body(d))return;
    String user=d["user"]|"",password=d["password"]|"",current=d["current"]|"";user.trim();
    if(passwordSet()&&!keyAuthorized()&&passwordHash(current,panelSalt)!=panelHash){error(403,"Текущий пароль не подходит");return;}
    if(!validLogin(user,password)){error(400,"Логин до 32 символов, пароль от 8 до 64");return;}
    setPanelPassword(user,password,0);sendSession(true);JsonDocument r;r["ok"]=true;reply(200,r);
  });
  web.on("/",HTTP_GET,[]{web.sendHeader("Content-Encoding","gzip");web.sendHeader("Cache-Control","no-cache");web.sendHeader("X-Content-Type-Options","nosniff");web.send_P(200,"text/html; charset=utf-8",(const char*)WEB_UI,sizeof(WEB_UI));});
  web.on("/api/v1/state",HTTP_GET,[]{if(auth())state();});
  web.on("/api/v1/status",HTTP_POST,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    int s=statusIndex(d["status"].as<String>());if(s<0){error(400,"Неизвестный статус");return;}
    if(s==3&&(!fsReady||!LittleFS.exists(assetPath(3)))){error(409,"Сначала загрузите изображение");return;}
    setManual(s,"manual");requestDisplay();state();
  });
  web.on("/api/v1/schedule",HTTP_GET,[]{if(!auth())return;JsonDocument d;scheduleJson(d);reply(200,d);});
  web.on("/api/v1/schedule",HTTP_PUT,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;Event next[32];int n,offset;
    if(!parseSchedule(d,next,n,offset)){error(400,"Проверьте дни, время, статусы и отсутствие одинаковых событий");return;}
    memcpy(events,next,n*sizeof(Event));eventCount=n;utcOffset=offset;automatic=d["enabled"];manualAt=0;prefs.putUInt("manualAt",0);saveSchedule();reply(200,d);
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
    if(rendering){error(409,"Дождитесь окончания обновления экрана");return;}
    if(scanActive&&WiFi.scanComplete()==WIFI_SCAN_RUNNING){error(409,"Дождитесь окончания поиска сетей");return;}
    if(!d["ssid"].is<const char*>()||!d["password"].is<const char*>()){error(400,"Требуются ssid и password");return;}
    String s=d["ssid"],p=d["password"];if(s.length()<1||s.length()>32||p.length()>63||(p.length()>0&&p.length()<8)){error(400,"Проверьте имя сети и пароль");return;}
    ssid=s;wifiPassword=p;prefs.putString("ssid",s);prefs.putString("wifi",p);web.send(202,"application/json","{\"accepted\":true}");WiFi.begin(s.c_str(),p.c_str());
  });
  web.on("/api/v1/wifi/scan",HTTP_POST,[]{
    if(!auth()||!writable(true))return;
    if(rendering){error(409,"Дождитесь окончания обновления экрана");return;}
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
  web.on("/api/v1/asset",HTTP_DELETE,[]{
    if(!auth()||!writable())return;int s=statusIndex(web.arg("slot"));
    if(!fsReady||s<0){error(404,"Нет изображения");return;}
    if(rendering){error(409,"Дождитесь окончания обновления экрана");return;}
    if(s==3&&desired==3){error(409,"Сначала выберите другой статус");return;}
    if(LittleFS.exists(assetPath(s))&&!LittleFS.remove(assetPath(s))){error(500,"Не удалось удалить изображение");return;}
    if(s==desired)displayed=-1;requestDisplay();state();
  });
  web.on("/api/v1/asset",HTTP_GET,[]{
    if(!auth())return;int s=statusIndex(web.arg("slot"));if(!fsReady||s<0){error(404,"Нет изображения");return;}
    File f=LittleFS.open(assetPath(s),"r");if(!f){error(404,"Нет изображения");return;}
    web.streamFile(f,"application/octet-stream");f.close();
  });
  // Firmware update is allowed in recovery too: it is how a recovery build is left without USB.
  web.on("/api/v1/firmware",HTTP_POST,[]{
    if(!otaOK)otaBusy=false;
    if(!auth())return;
    if(!otaOK){error(400,otaError.length()?otaError.c_str():"Нужен файл firmware.bin");return;}
    Serial.printf("[ota] %u bytes written; restarting into the new image\n",(unsigned)otaBytes);
    startTrial(false);logEvent(EV_OTA,OTA_RECEIVED,otaBytes);
    JsonDocument d;d["accepted"]=true;d["bytes"]=otaBytes;reply(200,d);restartAt=millis()+1500;
  },firmwareUpload);
  web.on("/api/v1/settings",HTTP_PUT,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    if(rendering){error(409,"Дождитесь окончания обновления экрана");return;}
    if(!d["qrEnabled"].is<bool>()||!d["qrUrl"].is<const char*>()){error(400,"Требуются qrEnabled и qrUrl");return;}
    String url=d["qrUrl"].as<const char*>();url.trim();
    if(url.length()>160||!(url.startsWith("https://")||url.startsWith("http://"))){error(400,"Ссылка должна начинаться с https:// и быть не длиннее 160 символов");return;}
    if(!makeQr(url)){error(400,"Не удалось построить QR-код для этой ссылки");return;}
    qrEnabled=d["qrEnabled"];qrUrl=url;prefs.putBool("qrOn",qrEnabled);prefs.putString("qrUrl",qrUrl);
    // Built-in frames carry the QR, so the current one is redrawn; saved pictures are not.
    if(desired<3&&!(fsReady&&LittleFS.exists(assetPath(desired))))displayed=-1;
    requestDisplay();state();
  });
  web.on("/api/v1/crm",HTTP_PUT,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    if(!d["enabled"].is<bool>()||!d["url"].is<const char*>()){error(400,"Требуются enabled и url");return;}
    String url=d["url"].as<const char*>();url.trim();while(url.endsWith("/"))url.remove(url.length()-1);
    String token=d["token"].is<const char*>()?d["token"].as<const char*>():"";token.trim();
    if(url.length()>120||!(url.startsWith("https://")||url.startsWith("http://"))){error(400,"Адрес CRM должен начинаться с https://");return;}
    if(token.length()>128){error(400,"Слишком длинный токен");return;}
    if((token.length()&&token!=crmToken)||url!=crmUrl)crmResetApplied(); // another CRM or sign: start from scratch
    if(token.length()){crmToken=token;crmPrefs.putString("token",crmToken);}
    if(d["enabled"].as<bool>()&&!crmToken.length()){error(400,"Введите токен вывески из CRM");return;}
    crmUrl=url;crmEnabled=d["enabled"];crmPrefs.putString("url",crmUrl);crmPrefs.putBool("on",crmEnabled);
    crmError="";crmLastTry=0;state();
  });
  web.on("/api/v1/power",HTTP_PUT,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;
    if(!d["batterySave"].is<bool>()){error(400,"Требуется batterySave");return;}
    setPowerMode(d["auto"]|false,d["batterySave"],0);state();
  });
  web.on("/api/v1/awake",HTTP_POST,[]{
    if(!auth())return;stayAwake(REQUEST_AWAKE_MS);state();
  });
  // Measures the board's sleep current: radio off and light sleep for the given time,
  // as in battery mode; the board button ends it early.
  web.on("/api/v1/sleeptest",HTTP_POST,[]{
    if(!auth()||!writable())return;JsonDocument d;if(!body(d))return;int sec=d["seconds"]|0;
    if(sec<30||sec>600){error(400,"Длительность от 30 до 600 секунд");return;}
    if(rendering||otaBusy){error(409,"Дождитесь окончания обновления");return;}
    sleepTestSeconds=sec;sleepTestAt=millis()+1500;state();
  });
#if !AVIOR_RECOVERY
  // Built-in template of a status, for the panel preview: decoded from flash in chunks.
  web.on("/api/v1/builtin",HTTP_GET,[]{
    if(!auth())return;int s=statusIndex(web.arg("slot"));
    if(s<0||s>2){error(404,"Нет стандартной картинки");return;}
    web.sendHeader("Cache-Control","no-store");web.setContentLength(FRAME_BYTES);web.send(200,"application/octet-stream","");
    uint8_t chunk[1200];
    for(size_t off=0;off<FRAME_BYTES;off+=sizeof chunk){unpackTemplate(TEMPLATES[s],TEMPLATE_SIZES[s],off,sizeof chunk,chunk);web.sendContent((const char*)chunk,sizeof chunk);}
  });
#endif
  // Event log, newest first; streamed so 64 texts never sit in memory at once.
  web.on("/api/v1/events",HTTP_GET,[]{
    if(!auth())return;
    web.sendHeader("Cache-Control","no-store");web.setContentLength(CONTENT_LENGTH_UNKNOWN);web.send(200,"application/json","");
    char head[128];snprintf(head,sizeof head,"{\"next\":%u,\"boot\":%u,\"now\":%u,\"uptime\":%u,\"capacity\":%d,\"events\":[",
      (unsigned)logNext,(unsigned)bootCount,(unsigned)logNow(),(unsigned)(millis()/1000),LOG_CAPACITY);
    web.sendContent(head);bool first=true;
    for(uint32_t s=logNext-1;s>=logFirst();s--){
      const LogRecord& e=logRing[s%LOG_CAPACITY];if(!e.kind)continue;
      JsonDocument d;eventJson(d.to<JsonObject>(),s,e);String item;serializeJson(d,item);
      web.sendContent(first?item:","+item);first=false;
    }
    web.sendContent("]}");web.sendContent("");
  });
  web.on("/api/v1/qr",HTTP_GET,[]{
    if(!auth())return;JsonDocument d;d["enabled"]=qrEnabled;d["url"]=qrUrl;d["size"]=qr.size;
    auto rows=d["rows"].to<JsonArray>();
    for(int y=0;y<qr.size;y++){String row;row.reserve(qr.size);for(int x=0;x<qr.size;x++)row+=qr.get(x,y)?'1':'0';rows.add(row);}
    reply(200,d);
  });
  web.onNotFound([]{error(404,"Маршрут не найден");});web.begin();
}
void setup(){
  Serial.begin(115200);bootReason=esp_reset_reason();
  Serial.printf("\n[boot] AviorSign %s reset=%s(%d) heap=%u %s\n",FW_VERSION,resetName(bootReason),(int)bootReason,ESP.getFreeHeap(),FW_MARKER);
  logBegin();
  // What the previous run was doing before an unexpected reset (not after a power loss,
  // which clears RTC memory, nor after the planned restart for a firmware update).
  if(!RECOVERY_MODE){
    if(snapshot.magic==SNAPSHOT_MAGIC&&bootReason!=ESP_RST_POWERON&&bootReason!=ESP_RST_SW)
      logRecord({snapshot.time,(int32_t)snapshot.uptime,(int32_t)(std::min(snapshot.minHeap,0xFFFFFFu)|(uint32_t)snapshot.flags<<24),snapshot.batteryMv,EV_BEFORE_RESET,snapshot.boot});
    snapshot.magic=0;
  }
  logEvent(EV_BOOT,bootReason,versionNumber(FW_VERSION));
  Serial.println("[boot] phase=nvs");Serial.flush();prefs.begin("avior-sign",false);
  token=prefs.getString("token","");apPassword=prefs.getString("ap","");
  if(token.isEmpty()){token=randomHex(24);prefs.putString("token",token);}
  if(apPassword.isEmpty()){apPassword=randomHex(8);prefs.putString("ap",apPassword);}
  panelUser=prefs.getString("pUser","");panelSalt=prefs.getString("pSalt","");panelHash=prefs.getString("pHash","");
  if(prefs.getBytes("sessKey",sessionKey,sizeof sessionKey)!=sizeof sessionKey)newSessionKey();
  {
    const esp_partition_t* running=esp_ota_get_running_partition();esp_ota_img_states_t ota;
    otaPending=running&&esp_ota_get_state_partition(running,&ota)==ESP_OK&&ota==ESP_OTA_IMG_PENDING_VERIFY;
    Serial.printf("[boot] app=%s ota_pending=%d\n",running?running->label:"?",otaPending);
    if(logReady&&logPrefs.isKey("otaTo")){
      bool installed=running&&running->address==logPrefs.getUInt("otaTo",0);
      logEvent(EV_OTA,installed?OTA_INSTALLED:OTA_ROLLED_BACK,versionNumber(FW_VERSION),logPrefs.getUInt("otaFrom",0));
      logPrefs.remove("otaTo");logPrefs.remove("otaFrom");
    }
    if(logReady&&logPrefs.isKey("trialPrev")){
      if(!running||running->address==logPrefs.getUInt("trialPrev",0))endTrial(); // back on the previous image
      else{
        trial=true;trialCrm=logPrefs.getBool("trialCrm",false);uint8_t boots=logPrefs.getUChar("trialBoots",0)+1;logPrefs.putUChar("trialBoots",boots);
        Serial.printf("[ota] trial boot %u of the new image, confirmed by %s\n",boots,trialCrm?"the CRM":"one minute of work");
        if(boots>3)revertTrial(false);
      }
    }
  }
  crmPrefs.begin("avior-crm",RECOVERY_MODE);
  crmEnabled=!RECOVERY_MODE&&crmPrefs.getBool("on",false);crmUrl=crmPrefs.getString("url",DEFAULT_CRM_URL);crmToken=crmPrefs.getString("token","");
  for(int i=0;i<CRM_ITEM_COUNT;i++)crmApplied[i]=crmPrefs.getUInt(CRM_ITEMS[i],0);
  crmEventsAck=crmPrefs.getUInt("evAck",0);if(crmEventsAck>=logNext)crmEventsAck=0; // the log was cleared
  qrEnabled=prefs.getBool("qrOn",true);qrUrl=prefs.getString("qrUrl",DEFAULT_QR_URL);
  if(!makeQr(qrUrl)){qrUrl=DEFAULT_QR_URL;makeQr(qrUrl);}
  confirmed=!RECOVERY_MODE&&prefs.getBool("hardware",false);desired=constrain(prefs.getInt("desired",1),0,3);
  JsonDocument d;String stored=prefs.getString("schedule","");int n,offset;
  if(!deserializeJson(d,stored)&&parseSchedule(d,events,n,offset)){eventCount=n;utcOffset=offset;automatic=d["enabled"];}
  manualAt=prefs.getUInt("manualAt",0);
  {
    // Recovery only reads the trace, so the evidence survives until normal firmware returns.
    Preferences trace;trace.begin("avior-trace",RECOVERY_MODE);
    uint8_t last=trace.getUChar("phase",PHASE_IDLE);if(last>=PHASE_COUNT)last=PHASE_START;
    renderAborts=trace.getUInt("aborts",0);
    // The block persists in NVS: the panel can keep refreshing by itself after the
    // MCU reset and cause further resets, which must not re-enable automatic retry.
    interruptedPhase=last!=PHASE_IDLE?last:trace.getUChar("blocked",PHASE_IDLE);
    if(interruptedPhase>=PHASE_COUNT)interruptedPhase=PHASE_START;
    int abortReset=last!=PHASE_IDLE?(int)bootReason:trace.getInt("abortReset",0);
    if(last!=PHASE_IDLE&&!RECOVERY_MODE){
      trace.putUInt("aborts",++renderAborts);trace.putUChar("blocked",last);trace.putInt("abortReset",abortReset);trace.putUChar("phase",PHASE_IDLE);
      logEvent(EV_RENDER_ABORT,last,abortReset);
    }
    if(blocksAutoRender(interruptedPhase)){
      const char* why=resetName((esp_reset_reason_t)abortReset);
      Serial.printf("[boot] render_blocked phase=%s reset_at_abort=%s aborts=%u new_abort=%d\n",phaseName(interruptedPhase),why,(unsigned)renderAborts,last!=PHASE_IDLE);
      if(!RECOVERY_MODE)displayError=String("Прошлое обновление прервано сбросом: этап ")+phaseName(interruptedPhase)+", причина "+why+". Автоповтор отключён, запустите вручную.";
    }
    trace.end();
  }
  if(RECOVERY_MODE){
    automatic=false;source=MODE_NAME;fsReady=false;
    Serial.println("[boot] recovery: display task, SPI, framebuffer and filesystem disabled");
    Serial.printf("[boot] station Wi-Fi setup=%s\n",RECOVERY_WIFI?"enabled; connect from panel":"disabled");
  }else{
  Serial.println("[boot] phase=filesystem");Serial.flush();
  fsReady=LittleFS.begin(false,"/littlefs",5,"littlefs"); // Never auto-format the factory filesystem.
#if !AVIOR_RECOVERY
  tracePrefs.begin("avior-trace",false);
  jobs=xQueueCreate(1,sizeof(int));results=xQueueCreate(1,sizeof(Result));
  if(!jobs||!results||xTaskCreatePinnedToCore(worker,"epaper",8192,nullptr,1,nullptr,1)!=pdPASS){Serial.println("Worker allocation failed");while(true)delay(1000);}
#endif
  }
  Serial.println("[boot] phase=wifi");Serial.flush();
  if(!RECOVERY_MODE){ssid=prefs.getString("ssid","");wifiPassword=prefs.getString("wifi","");}
  // After power-on or reset the panel stays reachable for a while even in battery mode.
  powerManual=prefs.getBool("power",false);powerAuto=prefs.getBool("powerAuto",true);
  powerSave=!RECOVERY_MODE&&!powerAuto&&powerManual;sessionWake=true;
  {int scale=prefs.getInt("batScale",1000);int16_t curve[CURVE_POINTS];
   bool hasCurve=prefs.isKey("batCurve")&&prefs.getBytes("batCurve",curve,sizeof curve)==sizeof curve;
   if(!setBatteryCalibration(scale,hasCurve?curve:nullptr,false))setBatteryCalibration(1000,nullptr,false);}
  awakeUntil=millis()+BUTTON_AWAKE_MS;
  pinMode(0,INPUT_PULLUP); // board button: BOOT strap at reset, setup button afterwards
  WiFi.onEvent(onWifiDisconnect,ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  bool apOK=startRadio();
  Serial.printf("[boot] access_point=%s\n",apOK?"ready":"failed");
  if(!RECOVERY_MODE){
    configTime(0,0,"pool.ntp.org","time.google.com");sntp_set_time_sync_notification_cb(onTimeSync);MDNS.begin("avior");MDNS.addService("http","tcp",80);
  }
  Serial.printf("\nAviorSign setup\nAP password: %s\nAPI token: %s\nOpen http://192.168.4.1\n",apPassword.c_str(),token.c_str());
  sampleBattery();routes();Serial.println("[boot] ready; health log every 5 seconds");
}
void loop(){
  web.handleClient();Result r;
  if((otaPending||trial)&&(trialCrm?crmEverOk:millis()>=OTA_CONFIRM_MS)){
    if(otaPending)esp_ota_mark_app_valid_cancel_rollback();
    otaPending=false;endTrial();logEvent(EV_OTA,OTA_CONFIRMED,versionNumber(FW_VERSION));
  }
  if(trial&&trialCrm&&millis()>=TRIAL_CRM_MS)revertTrial(true);
  if(restartAt&&(int32_t)(millis()-restartAt)>=0){Serial.flush();ESP.restart();}
  if(sleepTestAt&&(int32_t)(millis()-sleepTestAt)>=0){
    sleepTestAt=0;sampleBattery();bool button=lightSleep(sleepTestSeconds);
    startRadio();sampleBattery();
    Serial.printf("[power] sleep test done%s battery_mv=%d\n",button?" (button)":"",batteryMv);
  }
  pollButton();
  if(powerSave&&!RECOVERY_MODE)powerSaveStep();
  if(scanActive&&millis()-scanStarted>30000&&WiFi.scanComplete()!=WIFI_SCAN_RUNNING){WiFi.scanDelete();scanActive=false;}
  if(pendingJob>=0&&millis()-pendingSince>=RADIO_OFF_GRACE_MS){
    int s=pendingJob;pendingJob=-1;
    // A battery-mode wake may have left the radio off on purpose; then it stays off afterwards.
    if(RADIO_OFF_DURING_REFRESH&&WiFi.getMode()!=WIFI_OFF){Serial.println("[radio] off for refresh");WiFi.scanDelete();scanActive=false;WiFi.mode(WIFI_OFF);radioPaused=true;}
    if(xQueueSend(jobs,&s,0)!=pdTRUE){rendering=false;resumeRadio();}
    else{refreshStartedAt=millis();refreshBatteryMv=batteryMv;takeSnapshot();}
  }
  if(results&&xQueueReceive(results,&r,0)==pdTRUE){
    rendering=false;lastRefresh=millis();resumeRadio();
    logEvent(r.ok?EV_REFRESH:EV_REFRESH_FAIL,r.status,millis()-refreshStartedAt,refreshBatteryMv);
    if(r.ok){displayed=r.status;displayError="";clearRenderBlock();}else {displayError="Ошибка обновления: проверьте BUSY, питание, F53 и GPIO. Автоповтор отключён.";}}
  if(millis()-lastTick>1000){
    lastTick=millis();manageAccessPoint();trackWifi();logTick();time_t now=time(nullptr);
    if(automatic&&now>1700000000){
      time_t local=now+utcOffset*60;tm t;gmtime_r(&local,&t);int minute=t.tm_hour*60+t.tm_min;
      int s=scheduledStatus(events,eventCount,t.tm_wday,minute),age=minutesSinceEvent(events,eventCount,t.tm_wday,minute);
      if(!manualHolds(manualAt,now,age*60+t.tm_sec)){
        if(manualAt){manualAt=0;prefs.putUInt("manualAt",0);}
        source="schedule";if(s>=0&&desired!=s){desired=s;displayError="";}
      }
    }
    if(displayError.isEmpty()&&!blocksAutoRender(interruptedPhase))requestDisplay();
  }
  if(!crmWaitReason()&&millis()-crmLastTry>=crmIntervalMs)crmSync();
  if(millis()-lastHealth>=5000){
    lastHealth=millis();sampleBattery();trackBattery();trackCharge();takeSnapshot();
    Serial.printf("[battery] pin=%d mv=%d percent=%d adc_mv",batteryPin,batteryMv,batteryPin>=0?percentNow():-1);
    for(int i=0;i<ADC_PROBE_COUNT;i++)Serial.printf(" %d:%d",ADC_PROBE_PINS[i],adcMv[i]);Serial.println();
    WifiSnapshot wifi=wifiSnapshot();
    Serial.printf("[health] mode=%s uptime_s=%lu heap=%u min_heap=%u clients=%u wifi=%s arduino_wifi=%d ip=%s associated=%d netif_up=%d reset=%s(%d)\n",MODE_NAME,(unsigned long)(millis()/1000),ESP.getFreeHeap(),esp_get_minimum_free_heap_size(),WiFi.softAPgetStationNum(),stationStateName(wifi.state()),wifi.evidence.arduinoStatus,wifi.state()==StationState::Connected?wifi.address.toString().c_str():"0.0.0.0",wifi.evidence.associated,wifi.evidence.interfaceUp,resetName(bootReason),(int)bootReason);
    const char* crmWait=crmWaitReason();
    if(!crmEnabled){}
    else if(crmWait)Serial.printf("[crm] waiting: %s\n",crmWait);
    else if(crmError.length())Serial.printf("[crm] error: %s\n",crmError.c_str());
    else if(crmEverOk)Serial.printf("[crm] ok, last exchange %lus ago\n",(unsigned long)((millis()-crmLastOk)/1000));
  }
  delay(2);
}
