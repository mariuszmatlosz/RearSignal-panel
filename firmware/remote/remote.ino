#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_CST8XX.h>
#include <Adafruit_PCF8574.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <memory>
#include <lvgl.h>
#include "assets.h"
LV_FONT_DECLARE(roboto16);
constexpr int kBacklight=6;

constexpr char kService[]="000000fa-0000-1000-8000-00805f9b34fb";
constexpr char kWrite[]="0000fa02-0000-1000-8000-00805f9b34fb";
constexpr char kNotify[]="0000fa03-0000-1000-8000-00805f9b34fb";
Arduino_DataBus *displayBus=new Arduino_HWSPI(3,2,5,1,-1,&SPI,true);
Arduino_GFX *display=new Arduino_ST7789(displayBus,-1,0,false,240,320);
Adafruit_CST8XX touch; Adafruit_PCF8574 expander;
Preferences preferences;
lv_color_t pixels[240*30]; lv_disp_draw_buf_t drawBuffer; lv_disp_drv_t displayDriver; lv_indev_drv_t touchDriver;
enum class Connection {Scanning,Connecting,Ready,Failed,Selecting,NeedsPassword,Incompatible};
enum class Playback {Idle,Uploading,Playing,Error};
enum class Action {Off,On,Play};
struct Command {Action action; int index; uint32_t version;};
QueueHandle_t commands;
std::atomic<Connection> connection{Connection::Scanning};
std::atomic<Playback> playback{Playback::Idle};
std::atomic<int> activeIndex{-1}, ledBrightness{40}, durationSeconds{8}, ack{-1};
std::atomic<bool> startupOff{true}, brightnessDirty{false};
std::atomic<uint32_t> requestVersion{0}, startedAt{0}, playDuration{8000};
lv_obj_t *mainPage,*settingsPage,*infoPage,*btIcon,*searchSpinner,*powerButton,*gearButton;
lv_obj_t *rows[16],*previews[16],*rowSpinners[16],*progressBars[16],*pages[4],*tabView;
lv_obj_t *blePage,*peerList,*peerStatus;
lv_obj_t *timeoutInput,*keyboard,*timeError,*lcdValue,*ledValue;
lv_img_dsc_t previewDescriptors[16][4],fordDescriptor;
struct Peer {char address[18]; char name[32]; uint8_t type; int rssi;};
Peer selectedPeer={}, discoveredPeers[8]={};int discoveredCount=0;
portMUX_TYPE peerMux=portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> discoveryRevision{0},selectionRevision{0};
std::atomic<bool> scanRequested{false},browseOnly{true};
std::atomic<int> passwordFlag{-1},deviceType{-1};
bool bleOpen=false;
Peer getSelectedPeer(){portENTER_CRITICAL(&peerMux);Peer peer=selectedPeer;portEXIT_CRITICAL(&peerMux);return peer;}

bool settingsOpen=false; int lcdBrightness=80;

void flushDisplay(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *colors){
 display->draw16bitRGBBitmap(area->x1,area->y1,reinterpret_cast<uint16_t*>(colors),area->x2-area->x1+1,area->y2-area->y1+1); lv_disp_flush_ready(driver);
}
void readTouch(lv_indev_drv_t*,lv_indev_data_t *data){
 data->state=LV_INDEV_STATE_RELEASED; if(!touch.touched())return; CST_TS_Point p=touch.getPoint();
 if(p.x>=240||p.y>=320)return; data->point.x=p.x; data->point.y=p.y; data->state=LV_INDEV_STATE_PRESSED;
}
void request(Action action,int index=-1){
 if(connection.load()!=Connection::Ready)return;
 Command command{action,index,requestVersion.fetch_add(1)+1}; xQueueOverwrite(commands,&command);
}
void setHidden(lv_obj_t *object,bool hidden){if(hidden)lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(object,LV_OBJ_FLAG_HIDDEN);}
lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y){auto o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);return o;}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h){auto o=lv_obj_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_bg_color(o,lv_color_black(),0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *user=nullptr,bool symbol=false){
 auto o=lv_btn_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x16191f),0);lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,user);
 if(text&&*text){auto l=label(o,text,0,0);if(symbol)lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);lv_obj_center(l);}return o;
}
void saveTimeout(){
 const char *text=lv_textarea_get_text(timeoutInput);int value=atoi(text);
 if(!*text||value<1||value>60){lv_label_set_text(timeError,"Wpisz od 1 do 60 s");return;}
 durationSeconds.store(value);preferences.putUChar("seconds",value);setHidden(keyboard,true);lv_label_set_text(timeError,"");lv_obj_clear_state(timeoutInput,LV_STATE_FOCUSED);
}
void timeoutEvent(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_FOCUSED){lv_obj_move_foreground(keyboard);setHidden(keyboard,false);lv_keyboard_set_textarea(keyboard,timeoutInput);}}
void keyboardEvent(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_READY)saveTimeout();if(lv_event_get_code(e)==LV_EVENT_CANCEL){char s[4];snprintf(s,sizeof(s),"%d",durationSeconds.load());lv_textarea_set_text(timeoutInput,s);setHidden(keyboard,true);lv_label_set_text(timeError,"");}}
void toggleSettings(lv_event_t*){bleOpen=false;setHidden(blePage,true);settingsOpen=!settingsOpen;setHidden(mainPage,settingsOpen);setHidden(settingsPage,!settingsOpen);setHidden(infoPage,true);setHidden(keyboard,true);}
void powerPressed(lv_event_t*){request(Action::Off);}
void rowPressed(lv_event_t *e){request(Action::Play,int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));}
void startupChanged(lv_event_t *e){bool value=lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED);startupOff.store(value);preferences.putBool("startupOff",value);}
void brightnessEvent(lv_event_t *e){
 auto slider=lv_event_get_target(e);bool lcd=lv_event_get_user_data(e)!=nullptr;int value=lv_slider_get_value(slider);
 if(lcd){lcdBrightness=value;ledcWrite(kBacklight,value*255/100);lv_label_set_text_fmt(lcdValue,"%d%%",value);}else{ledBrightness.store(value);lv_label_set_text_fmt(ledValue,"%d%%",value);}
 if(lv_event_get_code(e)==LV_EVENT_RELEASED){preferences.putUChar(lcd?"lcd":"led",value);if(!lcd)brightnessDirty.store(true);}
}
void showInfo(lv_event_t*){setHidden(settingsPage,true);setHidden(infoPage,false);}
void hideInfo(lv_event_t*){setHidden(infoPage,true);setHidden(settingsPage,false);}
void initImage(lv_img_dsc_t &d,int w,int h,const uint16_t *data){d={};d.header.cf=LV_IMG_CF_TRUE_COLOR;d.header.w=w;d.header.h=h;d.data_size=w*h*2;d.data=reinterpret_cast<const uint8_t*>(data);}
void selectPeer(const Peer &peer){
 portENTER_CRITICAL(&peerMux);selectedPeer=peer;portEXIT_CRITICAL(&peerMux);
 preferences.putString("panelAddr",peer.address);preferences.putString("panelName",peer.name);preferences.putUChar("panelType",peer.type);
 passwordFlag.store(-1);deviceType.store(-1);browseOnly.store(false);scanRequested.store(false);selectionRevision.fetch_add(1);requestVersion.fetch_add(1);
 Serial.printf("SELECTED %s %s type=%u\n",peer.name,peer.address,peer.type);
}
void choosePeer(lv_event_t *e){
 int index=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));Peer peer={};bool valid=false;
 portENTER_CRITICAL(&peerMux);if(index>=0&&index<discoveredCount){peer=discoveredPeers[index];valid=true;}portEXIT_CRITICAL(&peerMux);
 if(valid)selectPeer(peer);
}
void scanPeers(lv_event_t*){browseOnly.store(true);scanRequested.store(true);selectionRevision.fetch_add(1);requestVersion.fetch_add(1);}
void forgetPeer(lv_event_t*){
 Peer empty={};portENTER_CRITICAL(&peerMux);selectedPeer=empty;portEXIT_CRITICAL(&peerMux);
 preferences.remove("panelAddr");preferences.remove("panelName");preferences.remove("panelType");passwordFlag.store(-1);deviceType.store(-1);scanPeers(nullptr);
}
void showBle(lv_event_t*){bleOpen=true;setHidden(mainPage,true);setHidden(settingsPage,true);setHidden(infoPage,true);setHidden(keyboard,true);setHidden(blePage,false);}
void closeBle(lv_event_t*){bleOpen=false;setHidden(blePage,true);setHidden(mainPage,settingsOpen);setHidden(settingsPage,!settingsOpen);if(getSelectedPeer().address[0])browseOnly.store(false);}
void refreshPeerList(){
 static uint32_t previous=UINT32_MAX;uint32_t revision=discoveryRevision.load();if(revision==previous)return;previous=revision;
 Peer peers[8];int count;portENTER_CRITICAL(&peerMux);count=discoveredCount;memcpy(peers,discoveredPeers,sizeof(peers));portEXIT_CRITICAL(&peerMux);
 lv_obj_clean(peerList);
 for(int i=0;i<count;i++){
  char caption[90];snprintf(caption,sizeof(caption),"%s\n%s  %d dBm",peers[i].name,peers[i].address,peers[i].rssi);
  auto item=button(peerList,caption,0,0,208,54,choosePeer,reinterpret_cast<void*>(intptr_t(i)));
  lv_obj_set_style_shadow_width(item,0,0);
 }
 if(!count){auto l=label(peerList,"Brak paneli iPixel.\nWłącz panel i szukaj.",0,0);lv_obj_set_width(l,204);}
}
void updateUi(lv_timer_t*){
 bool ready=connection.load()==Connection::Ready;
 lv_obj_set_style_text_color(btIcon,lv_color_hex(ready?0x329bff:0x707780),0);setHidden(searchSpinner,connection.load()!=Connection::Scanning&&connection.load()!=Connection::Connecting);
 refreshPeerList();
 Peer selected=getSelectedPeer();const char *statusText="Wybierz panel z listy";
 switch(connection.load()){case Connection::Scanning:statusText="Szukanie paneli...";break;case Connection::Connecting:statusText="Łączenie...";break;case Connection::Ready:statusText=passwordFlag.load()==0||passwordFlag.load()==255?"Połączono; brak hasła":"Połączono; ochrona nieznana";break;case Connection::NeedsPassword:statusText="Wymaga hasła w iPixel";break;case Connection::Incompatible:statusText="Obsługiwane: iPixel 96x16";break;case Connection::Failed:statusText="Panel niedostępny; ponawiam";break;default:break;}
 lv_label_set_text_fmt(peerStatus,"%s\n%s",selected.address[0]?selected.name:"Nie wybrano panelu",statusText);
 if(ready)lv_obj_clear_state(powerButton,LV_STATE_DISABLED);else lv_obj_add_state(powerButton,LV_STATE_DISABLED);
 Playback state=playback.load();int active=activeIndex.load();uint32_t elapsed=millis()-startedAt.load();
 for(int i=0;i<16;i++){
  bool running=ready&&i==active&&(state==Playback::Playing||state==Playback::Uploading);
  if(ready)lv_obj_clear_state(rows[i],LV_STATE_DISABLED);else lv_obj_add_state(rows[i],LV_STATE_DISABLED);
  setHidden(rowSpinners[i],!running);setHidden(progressBars[i],!running||state!=Playback::Playing||animations[i].unlimited);
  lv_obj_set_style_border_width(rows[i],ready&&i==active&&state==Playback::Error?2:1,0);
  lv_obj_set_style_border_color(rows[i],lv_color_hex(ready&&i==active&&state==Playback::Error?0xff4545:0x24282d),0);
  int frame=(millis()/animations[i].previewStepMs)%4;lv_img_set_src(previews[i],&previewDescriptors[i][frame]);
  if(running&&state==Playback::Playing)lv_bar_set_value(progressBars[i],min(uint32_t(100),elapsed*100/max(uint32_t(1),playDuration.load())),LV_ANIM_OFF);
 }
}
void createUi(){
 auto screen=lv_scr_act();lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(screen,lv_color_black(),0);lv_obj_set_style_text_color(screen,lv_color_white(),0);lv_obj_set_style_text_font(screen,&roboto16,0);
 blePage=box(screen,0,44,240,276);mainPage=box(screen,0,44,240,276);settingsPage=box(screen,0,44,240,276);infoPage=box(screen,0,44,240,276);
 tabView=lv_tabview_create(mainPage,LV_DIR_TOP,32);lv_obj_set_size(tabView,240,276);lv_obj_set_style_bg_color(tabView,lv_color_black(),0);lv_obj_set_style_border_width(tabView,0,0);
 auto tabContent=lv_tabview_get_content(tabView);lv_obj_set_scroll_dir(tabContent,LV_DIR_HOR);lv_obj_set_scrollbar_mode(tabContent,LV_SCROLLBAR_MODE_OFF);
 auto tabBar=lv_tabview_get_tab_btns(tabView);lv_obj_set_style_text_color(tabBar,lv_color_white(),LV_PART_ITEMS);lv_obj_set_style_text_color(tabBar,lv_color_white(),LV_PART_ITEMS|LV_STATE_CHECKED);
 for(int t=0;t<4;t++){
  char title[4];snprintf(title,sizeof(title),"%d",t+1);pages[t]=lv_tabview_add_tab(tabView,title);
  lv_obj_set_style_pad_all(pages[t],0,0);lv_obj_set_style_bg_color(pages[t],lv_color_black(),0);lv_obj_clear_flag(pages[t],LV_OBJ_FLAG_SCROLLABLE);
  for(int r=0;r<4;r++){
   int i=t*4+r;rows[i]=button(pages[t],"",8,r*58+2,224,54,rowPressed,reinterpret_cast<void*>(intptr_t(i)));
   lv_obj_set_style_bg_color(rows[i],lv_color_black(),0);lv_obj_set_style_shadow_width(rows[i],0,0);
   for(int f=0;f<4;f++)initImage(previewDescriptors[i][f],192,32,animations[i].frames[f]);
   previews[i]=lv_img_create(rows[i]);lv_img_set_src(previews[i],&previewDescriptors[i][0]);lv_obj_set_pos(previews[i],4,10);
   rowSpinners[i]=lv_spinner_create(rows[i],850,70);lv_obj_set_size(rowSpinners[i],20,20);lv_obj_set_pos(rowSpinners[i],201,16);lv_obj_set_style_arc_width(rowSpinners[i],2,LV_PART_MAIN);lv_obj_set_style_arc_width(rowSpinners[i],2,LV_PART_INDICATOR);
   progressBars[i]=lv_bar_create(rows[i]);lv_obj_set_size(progressBars[i],216,3);lv_obj_set_pos(progressBars[i],4,49);
  }
 }
 lv_tabview_set_act(tabView,0,LV_ANIM_OFF);
 initImage(fordDescriptor,100,40,fordPixels);auto logo=lv_img_create(settingsPage);lv_img_set_src(logo,&fordDescriptor);lv_obj_set_pos(logo,70,0);
 auto powerLabel=label(settingsPage,LV_SYMBOL_POWER,12,50);lv_obj_set_style_text_font(powerLabel,&lv_font_montserrat_14,0);label(settingsPage,"Wygaś na starcie",36,48);
 auto toggle=lv_switch_create(settingsPage);lv_obj_set_pos(toggle,180,44);lv_obj_set_size(toggle,48,26);if(startupOff.load())lv_obj_add_state(toggle,LV_STATE_CHECKED);lv_obj_add_event_cb(toggle,startupChanged,LV_EVENT_VALUE_CHANGED,nullptr);
 label(settingsPage,"Czas animacji (1-60 s)",12,85);
 timeoutInput=lv_textarea_create(settingsPage);lv_obj_set_pos(timeoutInput,164,109);lv_obj_set_size(timeoutInput,64,35);lv_textarea_set_one_line(timeoutInput,true);lv_textarea_set_accepted_chars(timeoutInput,"0123456789");lv_textarea_set_max_length(timeoutInput,2);char s[4];snprintf(s,sizeof(s),"%d",durationSeconds.load());lv_textarea_set_text(timeoutInput,s);lv_obj_add_event_cb(timeoutInput,timeoutEvent,LV_EVENT_FOCUSED,nullptr);
 label(settingsPage,"Sekundy",12,118);timeError=label(settingsPage,"",12,145);
 label(settingsPage,"Ekran",12,169);lcdValue=label(settingsPage,"",180,169);lv_label_set_text_fmt(lcdValue,"%d%%",lcdBrightness);
 label(settingsPage,"Listwa",12,217);ledValue=label(settingsPage,"",180,217);lv_label_set_text_fmt(ledValue,"%d%%",ledBrightness.load());
 for(int i=0;i<2;i++){auto slider=lv_slider_create(settingsPage);lv_obj_set_pos(slider,20,i?249:201);lv_obj_set_size(slider,200,8);lv_slider_set_range(slider, i?1:5,100);lv_slider_set_value(slider,i?ledBrightness.load():lcdBrightness,LV_ANIM_OFF);lv_obj_add_event_cb(slider,brightnessEvent,LV_EVENT_VALUE_CHANGED,i?nullptr:reinterpret_cast<void*>(1));lv_obj_add_event_cb(slider,brightnessEvent,LV_EVENT_RELEASED,i?nullptr:reinterpret_cast<void*>(1));}
 button(settingsPage,"i",202,3,30,30,showInfo);button(settingsPage,LV_SYMBOL_BLUETOOTH,8,3,30,30,showBle,nullptr,true);
 label(infoPage,"Projekt RearSignal",20,48);label(infoPage,"Autor:",20,85);label(infoPage,"Mariusz Matłosz",20,109);label(infoPage,"Licencja MIT",20,150);button(infoPage,LV_SYMBOL_LEFT,88,205,64,40,hideInfo,nullptr,true);
 label(blePage,"Panel Bluetooth",8,4);button(blePage,LV_SYMBOL_LEFT,202,0,30,30,closeBle,nullptr,true);
 peerList=lv_obj_create(blePage);lv_obj_set_pos(peerList,8,34);lv_obj_set_size(peerList,224,162);lv_obj_set_style_pad_all(peerList,2,0);lv_obj_set_flex_flow(peerList,LV_FLEX_FLOW_COLUMN);lv_obj_set_scroll_dir(peerList,LV_DIR_VER);lv_obj_set_style_border_width(peerList,0,0);
 peerStatus=label(blePage,"",8,199);lv_obj_set_width(peerStatus,224);lv_label_set_long_mode(peerStatus,LV_LABEL_LONG_WRAP);
 button(blePage,"Szukaj",8,246,108,30,scanPeers);button(blePage,"Zapomnij",124,246,108,30,forgetPeer);
 keyboard=lv_keyboard_create(screen);lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_NUMBER);lv_obj_set_size(keyboard,240,154);lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_event_cb(keyboard,keyboardEvent,LV_EVENT_ALL,nullptr);setHidden(keyboard,true);
 // Toolbar is outside the pages so OFF remains reachable in settings and Info.
 gearButton=button(screen,LV_SYMBOL_SETTINGS,4,2,40,38,toggleSettings,nullptr,true);
 powerButton=button(screen,LV_SYMBOL_POWER,99,2,42,38,powerPressed,nullptr,true);lv_obj_set_style_bg_color(powerButton,lv_color_hex(0x82232b),0);
 auto btButton=button(screen,"",211,2,27,38,showBle);btIcon=label(btButton,LV_SYMBOL_BLUETOOTH,7,11);lv_obj_set_style_text_font(btIcon,&lv_font_montserrat_14,0);
 searchSpinner=lv_spinner_create(screen,1000,70);lv_obj_set_size(searchSpinner,18,18);lv_obj_set_pos(searchSpinner,192,11);lv_obj_set_style_arc_width(searchSpinner,2,LV_PART_MAIN);lv_obj_set_style_arc_width(searchSpinner,2,LV_PART_INDICATOR);
 setHidden(settingsPage,true);setHidden(infoPage,true);setHidden(blePage,true);updateUi(nullptr);lv_timer_create(updateUi,90,nullptr);
}

bool writePower(NimBLERemoteCharacteristic *c,bool enabled){const uint8_t packet[]={5,0,7,1,uint8_t(enabled)};bool ok=c->writeValue(packet,sizeof(packet),true);Serial.printf("POWER %s: %s\n",enabled?"ON":"OFF",ok?"OK":"FAILED");return ok;}
bool writeBrightness(NimBLERemoteCharacteristic *c){const uint8_t packet[]={5,0,4,0x80,uint8_t(ledBrightness.load())};bool ok=c->writeValue(packet,sizeof(packet),true);Serial.printf("BRIGHTNESS %u: %s\n",packet[4],ok?"OK":"FAILED");return ok;}
uint32_t crc32(const uint8_t *data,size_t size){uint32_t crc=0xffffffff;for(size_t i=0;i<size;i++){crc^=data[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;}
void notify(NimBLERemoteCharacteristic*,uint8_t *data,size_t length,bool){
 Serial.print("NOTIFY ");for(size_t i=0;i<length;i++)Serial.printf("%02x",data[i]);Serial.println();
 if(length==5&&data[0]==5&&data[2]==3&&data[3]==0)ack.store(data[4]);
 if(length>=5&&data[2]==1&&data[3]==0x80){deviceType.store(data[4]);passwordFlag.store(length>=11?data[10]:-1);}
}
void put32(uint8_t *p,uint32_t value){for(int i=0;i<4;i++)p[i]=value>>(8*i);}
bool sendGif(NimBLEClient *client,NimBLERemoteCharacteristic *c,const Command &command){
 const auto &asset=animations[command.index];uint32_t crc=crc32(asset.gif,asset.length);std::unique_ptr<uint8_t[]> buffer(new(std::nothrow) uint8_t[12303]);if(!buffer)return false;uint8_t *packet=buffer.get();
 for(size_t offset=0;offset<asset.length;offset+=12288){
  size_t count=min(size_t(12288),asset.length-offset),length=count+15;
  packet[0]=length;packet[1]=length>>8;packet[2]=3;packet[3]=0;packet[4]=offset?2:0;put32(packet+5,asset.length);put32(packet+9,crc);packet[13]=2;packet[14]=0;memcpy(packet+15,asset.gif+offset,count);
  ack.store(-1);size_t mtuPayload=min(size_t(180),size_t(max(23,int(client->getMTU()))-3));
  for(size_t pos=0;pos<length;pos+=mtuPayload){
   // Complete the current protocol frame before processing a newer OFF.
   if(!client->isConnected())return false;
   if(!c->writeValue(packet+pos,min(mtuPayload,length-pos),true))return false;
   vTaskDelay(pdMS_TO_TICKS(8));
  }
  if(requestVersion.load()!=command.version)return false;
  bool final=offset+count==asset.length;
  // Match the 12 KB image windows used by pypixelcolor; wait for each ACK.
  if(!final&&(offset+count)%12288!=0){if(ack.load()==0)return false;continue;}
  uint32_t waitStart=millis();
  while(client->isConnected()&&requestVersion.load()==command.version&&millis()-waitStart<8000){
   int result=ack.load();if(result==0){Serial.println("GIF CRC rejected");return false;}
   if(result==3||(!final&&result==1))break;vTaskDelay(pdMS_TO_TICKS(10));
  }
  if(requestVersion.load()!=command.version)return false;
  int result=ack.load();if(!client->isConnected()||(final?result!=3:(result!=1&&result!=3))){Serial.printf("GIF ACK failed: %d\n",result);return false;}
 }
 Serial.printf("GIF %d: acknowledged (%u bytes)\n",command.index+1,unsigned(asset.length));return true;
}
bool queryPanel(NimBLERemoteCharacteristic *c){
 deviceType.store(-1);passwordFlag.store(-1);const uint8_t info[]={8,0,1,0x80,0,0,0,0};
 if(!c->writeValue(info,sizeof(info),true))return false;uint32_t start=millis();while(deviceType.load()<0&&millis()-start<2500)vTaskDelay(pdMS_TO_TICKS(20));
 Serial.printf("PANEL INFO type=%d password_flag=%d\n",deviceType.load(),passwordFlag.load());
 return deviceType.load()==132&&passwordFlag.load()!=1;
}
void bleWorker(void*){
 NimBLEDevice::init("RearSignal Remote");NimBLEDevice::setMTU(185);auto scanner=NimBLEDevice::getScan();scanner->setActiveScan(true);auto client=NimBLEDevice::createClient();client->setConnectTimeout(8000);uint32_t retryDelay=1000;bool firstConnection=true;
 for(;;){
  if(scanRequested.exchange(false)){
   connection.store(Connection::Scanning);Serial.println("BLE DISCOVERY");auto results=scanner->getResults(4000);Peer found[8]={};int count=0;
   for(int i=0;i<results.getCount()&&count<8;i++){
    auto candidate=results.getDevice(i);auto name=candidate->getName();
    if(name.rfind("LED_BLE_",0)!=0&&!candidate->isAdvertisingService(NimBLEUUID(kService)))continue;
    auto &peer=found[count++];strlcpy(peer.address,candidate->getAddress().toString().c_str(),sizeof(peer.address));strlcpy(peer.name,name.empty()?"iPixel":name.c_str(),sizeof(peer.name));peer.type=candidate->getAddress().getType();peer.rssi=candidate->getRSSI();
    Serial.printf("PEER %d %s %s type=%u RSSI=%d\n",count-1,peer.name,peer.address,peer.type,peer.rssi);
   }
   scanner->clearResults();portENTER_CRITICAL(&peerMux);memcpy(discoveredPeers,found,sizeof(found));discoveredCount=count;portEXIT_CRITICAL(&peerMux);discoveryRevision.fetch_add(1);
  }
  Peer target=getSelectedPeer();uint32_t revision=selectionRevision.load();
  if(!target.address[0]||browseOnly.load()){
   if(connection.load()!=Connection::NeedsPassword&&connection.load()!=Connection::Incompatible)connection.store(Connection::Selecting);
   vTaskDelay(pdMS_TO_TICKS(100));continue;
  }
  connection.store(Connection::Connecting);Serial.printf("BLE connecting %s\n",target.address);
  NimBLERemoteCharacteristic *c=nullptr,*n=nullptr;
  if(client->connect(NimBLEAddress(std::string(target.address),target.type))){auto service=client->getService(kService);if(service){c=service->getCharacteristic(kWrite);n=service->getCharacteristic(kNotify);}}
  if(c&&c->canWrite()&&n&&n->canNotify()&&n->subscribe(true,notify)){
   xQueueReset(commands);bool initialized=queryPanel(c);
   if(!initialized){browseOnly.store(true);connection.store(passwordFlag.load()==1?Connection::NeedsPassword:Connection::Incompatible);if(client->isConnected())client->disconnect();continue;}
   if(revision!=selectionRevision.load()){writePower(c,false);client->disconnect();continue;}
   if(!firstConnection||startupOff.load())initialized=writePower(c,false);
   if(initialized)initialized=writeBrightness(c);
   if(initialized){
    firstConnection=false;retryDelay=1000;brightnessDirty.store(false);connection.store(Connection::Ready);Serial.println("BLE READY");bool powerOn=false;
    while(client->isConnected()&&revision==selectionRevision.load()){
     Command command;bool hasRequest=xQueueReceive(commands,&command,pdMS_TO_TICKS(25))==pdTRUE;
     if(hasRequest){
      if(command.action==Action::Off){playback.store(Playback::Idle);activeIndex.store(-1);powerOn=false;if(!writePower(c,false))break;}
      else if(command.action==Action::On){if(!writePower(c,true))break;startedAt.store(millis());playDuration.store(durationSeconds.load()*1000);powerOn=true;}
      else{
       powerOn=false;activeIndex.store(command.index);playback.store(Playback::Uploading);
       if(!writePower(c,false))break;
       bool ok=sendGif(client,c,command);
       if(requestVersion.load()!=command.version){playback.store(Playback::Idle);continue;}
       if(ok)ok=writePower(c,true);
       if(ok){startedAt.store(millis());playDuration.store(animations[command.index].unlimited?0:durationSeconds.load()*1000);powerOn=true;playback.store(Playback::Playing);}
       else{playback.store(Playback::Error);break;}
      }
     }
     if(powerOn&&playDuration.load()!=0&&uint32_t(millis()-startedAt.load())>=playDuration.load()){powerOn=false;playback.store(Playback::Idle);activeIndex.store(-1);Serial.println("TIMEOUT");if(!writePower(c,false))break;}
     if(brightnessDirty.exchange(false)&&!writeBrightness(c))break;
    }
   }
  }
  if(c&&client->isConnected())writePower(c,false);connection.store(Connection::Failed);playback.store(Playback::Idle);activeIndex.store(-1);xQueueReset(commands);requestVersion.fetch_add(1);if(client->isConnected())client->disconnect();Serial.printf("BLE retry in %lu ms\n",(unsigned long)retryDelay);if(!browseOnly.load())vTaskDelay(pdMS_TO_TICKS(retryDelay));retryDelay=min(retryDelay*2,uint32_t(10000));
 }
}
void setup(){
 Serial.begin(115200);delay(300);Serial.println("RearSignal UI 0.3 / MIT / ONX2432G028 V1.3");
 preferences.begin("rearsignal",false);startupOff.store(preferences.getBool("startupOff",true));durationSeconds.store(constrain(preferences.getUChar("seconds",8),1,60));ledBrightness.store(constrain(preferences.getUChar("led",40),1,100));lcdBrightness=constrain(preferences.getUChar("lcd",80),5,100);
 String savedAddress=preferences.getString("panelAddr","");String savedName=preferences.getString("panelName","");strlcpy(selectedPeer.address,savedAddress.c_str(),sizeof(selectedPeer.address));strlcpy(selectedPeer.name,savedName.c_str(),sizeof(selectedPeer.name));selectedPeer.type=preferences.getUChar("panelType",0);browseOnly.store(savedAddress.isEmpty());scanRequested.store(savedAddress.isEmpty());
 Wire.begin(8,7,400000);if(!expander.begin(0x38,&Wire)){Serial.println("FATAL PCF8574");while(true)delay(1000);}expander.digitalWriteByte(0xff);delay(20);expander.digitalWriteByte(0xbf);delay(120);expander.digitalWriteByte(0xff);delay(120);
 if(!display->begin(40000000)){Serial.println("FATAL LCD");while(true)delay(1000);}display->setRotation(0);display->fillScreen(0);ledcAttach(kBacklight,5000,8);ledcWrite(kBacklight,lcdBrightness*255/100);
 bool touchReady=touch.begin(&Wire,0x15);Serial.printf("Touch init: %s\n",touchReady?"OK":"FAILED");if(!touchReady)while(true)delay(1000);
 lv_init();lv_disp_draw_buf_init(&drawBuffer,pixels,nullptr,240*30);lv_disp_drv_init(&displayDriver);displayDriver.hor_res=240;displayDriver.ver_res=320;displayDriver.flush_cb=flushDisplay;displayDriver.draw_buf=&drawBuffer;lv_disp_drv_register(&displayDriver);lv_disp_set_theme(lv_disp_get_default(),lv_theme_default_init(lv_disp_get_default(),lv_palette_main(LV_PALETTE_BLUE),lv_palette_main(LV_PALETTE_RED),true,&roboto16));lv_indev_drv_init(&touchDriver);touchDriver.type=LV_INDEV_TYPE_POINTER;touchDriver.read_cb=readTouch;lv_indev_drv_register(&touchDriver);
 commands=xQueueCreate(1,sizeof(Command));if(!commands)abort();createUi();lv_mem_monitor_t memory;lv_mem_monitor(&memory);Serial.printf("UI ready: LVGL free %u B, heap %u B\n",unsigned(memory.free_size),ESP.getFreeHeap());if(xTaskCreate(bleWorker,"panel-ble",12288,nullptr,1,nullptr)!=pdPASS)abort();
}
void diagnostic(const String &line){
 if(line=="scan")scanPeers(nullptr);
 if(line=="forget")forgetPeer(nullptr);
 if(line.startsWith("pair ")){int index=line.substring(5).toInt();Peer peer={};bool valid=false;portENTER_CRITICAL(&peerMux);if(index>=0&&index<discoveredCount){peer=discoveredPeers[index];valid=true;}portEXIT_CRITICAL(&peerMux);if(valid)selectPeer(peer);}
 if(line.startsWith("time ")){int seconds=line.substring(5).toInt();if(seconds>=1&&seconds<=60)durationSeconds.store(seconds);}
 if(line=="status"){Peer peer=getSelectedPeer();Serial.printf("STATUS target=%s connection=%d playback=%d active=%d duration=%lu\n",peer.address,int(connection.load()),int(playback.load()),activeIndex.load(),(unsigned long)playDuration.load());}
}
void loop(){
 static uint32_t previous=millis();uint32_t now=millis();lv_tick_inc(now-previous);previous=now;lv_timer_handler();
 static bool diagnosticMode=false;static String diagnosticLine;
 if(Serial.available()){char c=Serial.read();if(c==':'){diagnosticMode=true;diagnosticLine="";}else if(diagnosticMode){if(c=='\n'){diagnostic(diagnosticLine);diagnosticMode=false;}else if(c!='\r'&&diagnosticLine.length()<63)diagnosticLine+=c;}else{if(c=='0')request(Action::Off);if(c=='1')request(Action::On);if(c>='a'&&c<='p')request(Action::Play,c-'a');if(c=='+'||c=='-'){ledBrightness.store(constrain(ledBrightness.load()+(c=='+'?10:-10),1,100));brightnessDirty.store(true);}}}
 delay(5);
}

