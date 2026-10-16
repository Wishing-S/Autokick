/* NodKick - WT9011 link, a nod counter, and the kicker in one sketch.
 * Board: esp32:esp32:m5stack_core2. Libraries: M5Unified 0.2.26, NimBLE-Arduino 2.5.1.
 *
 * Layout, top to bottom:
 *   log strip  a rolling log, newest line first. Everything the firmware does
 *              worth knowing about lands here: link state, RESET/SAMPLE, every
 *              nod, every kick, and every refusal.
 *   fan        RESET fixes the gravity axis: the acceleration at that instant
 *              becomes g0, which is the upper edge of the fan. SAMPLE takes the
 *              deviation at that instant as ref, which is the lower edge. Both
 *              are instantaneous - there is no timed sampling anywhere. After
 *              SAMPLE a nod counts once the deviation has travelled from the
 *              upper 20% band into the lower 20% band, and it must return to
 *              the upper band before the next one can count, so a shaky reading
 *              cannot double-count. The measurement is the angle between the
 *              live gravity vector and the recorded one, so no axis is hard
 *              coded and the mounting is irrelevant.
 *   buttons    RESET and SAMPLE. Nothing is armed until both have been tapped.
 *   kicker     a counted nod fires one PCA9685 write, so the servo always runs
 *              at its own maximum speed; after HOLD_MS it is written back to
 *              rest. A nod that arrives while the motor is still moving is
 *              counted but not kicked, and says so in the log. */
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <vector>
#include <stdarg.h>

/* ------------------------------------------------------------------ link --- */
static constexpr char MAC[]="fd:3a:82:2b:45:bd";
static constexpr char SVC[]="0000ffe5-0000-1000-8000-00805f9a34fb";
static constexpr char CHR[]="0000ffe4-0000-1000-8000-00805f9a34fb";
static constexpr float LOW_F=0.20f, HIGH_F=0.80f, MAX_N=1.0f, MIN_REF=2;
static constexpr unsigned FLASH_MS=500;

static NimBLEClient *client=nullptr;
static float acc[3], g0[3];            // live accelerometer, reference gravity
static uint32_t lastMs=0;
static bool hasAcc=false;
static void notifyCb(NimBLERemoteCharacteristic*,uint8_t *d,size_t n,bool){
 for(size_t i=0;i+19<n;i++)            // 0x55 0x61 frame: accel starts at +2
  if(d[i]==0x55&&d[i+1]==0x61){
   for(int k=0;k<3;k++){
    int16_t raw=(int16_t)(d[i+2+2*k]|(uint16_t)d[i+3+2*k]<<8);
    acc[k]=raw*(16.0f/32768.0f);
   }
   lastMs=millis();hasAcc=true;
  }
}
static bool imuLive(){                 // a link is useless without a fresh frame
 return client&&client->isConnected()&&hasAcc&&millis()-lastMs<500;
}
static float deviation(){              // tilt away from the recorded posture
 float n0=sqrtf(g0[0]*g0[0]+g0[1]*g0[1]+g0[2]*g0[2]);
 float n1=sqrtf(acc[0]*acc[0]+acc[1]*acc[1]+acc[2]*acc[2]);
 if(n0<0.2f||n1<0.2f)return 0;
 float c=(g0[0]*acc[0]+g0[1]*acc[1]+g0[2]*acc[2])/(n0*n1);
 if(c>1)c=1;
 if(c<-1)c=-1;
 return acosf(c)*57.29578f;
}
static bool subscribe(){               // literal 128-bit match: this module uses
 for(auto *sv:client->getServices(true)){   // a non-standard Bluetooth base UUID
  if(strcmp(sv->getUUID().toString().c_str(),SVC)!=0)continue;
  for(auto *c:sv->getCharacteristics(true))
   if(strcmp(c->getUUID().toString().c_str(),CHR)==0&&(c->canNotify()||c->canIndicate()))
    return c->subscribe(c->canNotify(),notifyCb);
 }
 return false;
}
static bool link(){
 auto *scan=NimBLEDevice::getScan();scan->setActiveScan(true);
 auto res=scan->getResults(3000,false);
 bool ok=false;
 for(int i=0;i<res.getCount();i++)
  if(strcasecmp(res.getDevice(i)->getAddress().toString().c_str(),MAC)==0){
   ok=client->connect(res.getDevice(i))&&subscribe();
   if(!ok)client->disconnect();         // never keep a link that cannot report
   break;
  }
 scan->clearResults();
 return ok;
}

/* ------------------------------------------------------------------- log --- */
static constexpr int LOG_LINES=4, LOG_X=6, LOG_Y=3, LOG_STEP=10;
static constexpr int DIV_Y=LOG_Y+LOG_LINES*LOG_STEP+2;      // line under the strip
static char logLn[LOG_LINES][56];       // [0] is the newest, it scrolls down
static void logf(const char *fmt,...){
 char b[48];
 va_list ap;va_start(ap,fmt);
 vsnprintf(b,sizeof b,fmt,ap);
 va_end(ap);
 for(int i=LOG_LINES-1;i>0;i--)memcpy(logLn[i],logLn[i-1],sizeof logLn[0]);
 snprintf(logLn[0],sizeof logLn[0],"%6.1fs %s",millis()/1000.0f,b);
}
static void drawLog(){
 auto &d=M5.Display;
 static const uint16_t col[LOG_LINES]={TFT_WHITE,TFT_LIGHTGREY,TFT_DARKGREY,TFT_DARKGREY};
 d.setTextSize(1);
 for(int i=0;i<LOG_LINES;i++){
  d.setTextColor(col[i]);
  d.setCursor(LOG_X,LOG_Y+i*LOG_STEP);
  d.print(logLn[i]);
 }
 d.drawLine(0,DIV_Y,319,DIV_Y,TFT_DARKGREY);
}

/* ------------------------------------------------------------------ servo -- */
static TwoWire Bus(0);
static constexpr int SERVO_SDA=32, SERVO_SCL=33;            // Core2 port A
static constexpr uint8_t PCA=0x40, CH=15;
static constexpr uint16_t KICK_US=700, REST_US=1000;        // -81.7 / -23.1 deg
static constexpr unsigned HOLD_MS=300;
static bool ready=false,busy=false;
static uint32_t holdUntil=0;
static unsigned kicks=0,skipped=0;

static bool w8(uint8_t reg,uint8_t v){
 Bus.beginTransmission(PCA);Bus.write(reg);Bus.write(v);
 return Bus.endTransmission(true)==0;
}
static bool pulse(uint16_t us){                             // one write = full speed
 uint16_t c=(uint16_t)((uint32_t)us*4096UL/20000UL);        // 50Hz, 20ms period
 if(c>4095)c=4095;
 Bus.beginTransmission(PCA);Bus.write((uint8_t)(6+4*CH));
 Bus.write(0);Bus.write(0);
 Bus.write((uint8_t)(c&255));Bus.write((uint8_t)((c>>8)&15));
 return Bus.endTransmission(true)==0;
}
static bool pcaInit(){
 if(!Bus.begin(SERVO_SDA,SERVO_SCL,100000))return false;
 Bus.setTimeOut(10);
 Bus.beginTransmission(PCA);
 if(Bus.endTransmission(true)!=0)return false;
 if(!w8(0,0))return false;                                  // sleep before prescale
 delay(5);
 if(!w8(1,0x04))return false;                               // MODE2: totem pole
 if(!w8(0,0x10)||!w8(0xFE,121)||!w8(0,0))return false;      // 121 -> 50Hz
 delay(5);
 if(!w8(0,0xA0))return false;                               // restart, auto increment
 delay(5);
 return pulse(REST_US);
}
static void doKick(){                                       // fire and log the why
 if(!ready){logf("kick refused: pca error");skipped++;return;}
 if(busy){logf("kick refused: motor busy");skipped++;return;}
 if(!pulse(KICK_US)){logf("kick refused: i2c write");skipped++;return;}
 kicks++;busy=true;holdUntil=millis()+HOLD_MS;
 logf("kick! (#%u)",kicks);
}

/* ------------------------------------------------------------------ state -- */
enum {IDLE,ZEROED,ARMED};              // nothing recorded / g0 fixed / ref fixed
static int state=IDLE;
static bool atTop=false,down=false;    // atTop: back at the upper band, nod armed
static float ref=0,dev=0;
static uint32_t flashFrom=0;
static unsigned count=0;

static void doReset(){
 float m=sqrtf(acc[0]*acc[0]+acc[1]*acc[1]+acc[2]*acc[2]);
 if(!hasAcc||m<0.2f){logf("RESET ignored: no frame yet");return;}
 for(int k=0;k<3;k++)g0[k]=acc[k];     // the gravity axis is whatever we read now
 state=ZEROED;ref=0;dev=0;count=0;atTop=false;flashFrom=millis();
 logf("g0 fixed, hold still then nod");
}
static void doSample(){
 if(state==IDLE||dev<MIN_REF){logf("SAMPLE ignored: reset, nod >%.0f deg",MIN_REF);return;}
 ref=dev;state=ARMED;atTop=false;flashFrom=millis();
 logf("ref %.1f deg, counting nods",ref);
}

/* -------------------------------------------------------------------- ui --- */
// Fan: apex on the left, the upper edge is the reset posture and the lower edge
// is the sampled angle, so the whole wedge is scaled to the wanted nod. The two
// coloured bands along the rim are the 20% zones a nod has to leave and reach.
// No needle is drawn before RESET, because there is nothing to point at yet.
static constexpr int AX=34, AY=128, RAY=100;
static constexpr float HALF=32.0f, D2R=0.0174533f;
static float tip(float n){return HALF*(1.0f-2.0f*n);}        // 0 -> upper, 1 -> lower
static void fanSeg(float a0,float a1,float r,uint16_t col){
 float c0=cosf(a0*D2R),s0=sinf(a0*D2R),c1=cosf(a1*D2R),s1=sinf(a1*D2R);
 M5.Display.drawLine(AX+(int)(r*c0),AY-(int)(r*s0),AX+(int)(r*c1),AY-(int)(r*s1),col);
}
static void fanRay(float a,float r0,float r1,uint16_t col){
 float c=cosf(a*D2R),s=sinf(a*D2R);
 M5.Display.drawLine(AX+(int)(r0*c),AY-(int)(r0*s),AX+(int)(r1*c),AY-(int)(r1*s),col);
}
static void fanBand(float n0,float n1,uint16_t col){         // striped wedge, cheap fill
 float a0=tip(n1),a1=tip(n0);                                // n grows away from upper
 for(float a=a0;a<=a1;a+=1.2f)fanRay(a,0.36f*RAY,RAY,col);
}
static void fanArc(float a0,float a1,uint16_t col){          // three passes = a thick rim
 for(float r=RAY-1;r<=RAY+1;r+=1.0f)
  for(int i=0;i<20;i++)
   fanSeg(a0+(a1-a0)*i/20.0f,a0+(a1-a0)*(i+1)/20.0f,r,col);
}
static void drawFan(uint32_t now){
 auto &d=M5.Display;
 if(state==ARMED){                      // the lower edge exists only after SAMPLE
  fanBand(HIGH_F,MAX_N,TFT_DARKGREEN);  // reaching this band scores a nod
  fanBand(0.0f,LOW_F,TFT_DARKGREY);     // a nod has to leave this band first
 }
 fanArc(-HALF,HALF,TFT_DARKGREY);       // rim
 fanRay(HALF,0,RAY+12,TFT_ORANGE);      // upper edge: the reset posture
 if(state==ARMED)fanRay(-HALF,0,RAY+12,TFT_SKYBLUE);
 d.setTextSize(1);d.setTextColor(TFT_ORANGE);
 d.setCursor(138,58);d.print("zero");
 if(state==ARMED){
  d.setTextColor(TFT_SKYBLUE);
  d.setCursor(138,182);d.printf("ref %.1f",ref);
 }
 if(state==IDLE)return;
 float n=state==ARMED&&ref>0?dev/ref:0;
 if(n<0)n=0;
 if(n>MAX_N)n=MAX_N;
 uint16_t col=flashFrom&&now-flashFrom<FLASH_MS?TFT_GREEN:TFT_WHITE;
 float a=tip(n),c=cosf(a*D2R),s=sinf(a*D2R);
 int px=AX+(int)(RAY*c),py=AY-(int)(RAY*s);
 d.drawLine(AX,AY,px,py,col);
 d.drawLine(AX,AY+1,px,py+1,col);
 d.drawLine(AX+1,AY,px+1,py,col);
 d.fillCircle(px,py,3,col);
 d.fillCircle(AX,AY,4,col);
}
// SX: the stats column. Not RX, which pins_arduino.h already takes for UART0.
static constexpr int BY=196, BW=148, BH=38, B0X=10, B1X=162, SX=166;
static void drawButtons(){
 auto &d=M5.Display;
 for(int i=0;i<2;i++){
  int x=i?B1X:B0X;
  uint16_t col=i&&state==IDLE?TFT_DARKGREY:TFT_ORANGE;      // SAMPLE needs a zero first
  d.drawRoundRect(x,BY,BW,BH,6,col);
  d.setTextColor(col);d.setTextSize(2);
  d.setCursor(x+38,BY+11);
  d.print(i?"SAMPLE":"RESET");
 }
}
static void drawStats(){
 auto &d=M5.Display;
 d.setTextSize(1);d.setTextColor(TFT_LIGHTGREY);
 d.setCursor(SX,52);d.print("NODS");
 d.setTextSize(3);d.setTextColor(state==ARMED?TFT_WHITE:TFT_DARKGREY);
 d.setCursor(SX,62);d.printf("%u",count);
 d.setTextSize(1);d.setTextColor(TFT_LIGHTGREY);
 d.setCursor(SX,92);d.printf("ref   %5.1f",ref);
 d.setCursor(SX,104);d.printf("dev   %5.1f",dev);
 d.setCursor(SX,120);d.printf("kicks %u",kicks);
 d.setTextColor(ready?TFT_GREEN:TFT_RED);
 d.setCursor(SX,134);d.print(ready?"pca ok":"pca error");
 d.setTextColor(busy?TFT_RED:TFT_DARKGREY);
 d.setCursor(SX,146);d.print(busy?"motor KICK":"motor rest");
}
static void draw(){
 auto &d=M5.Display;
 uint32_t now=millis();
 d.fillScreen(TFT_BLACK);
 drawLog();
 drawFan(now);
 drawButtons();
 drawStats();
}

void setup(){
 auto cfg=M5.config();cfg.internal_imu=false;cfg.internal_rtc=false;
 cfg.internal_spk=false;cfg.internal_mic=false;
 M5.begin(cfg);M5.Display.setRotation(1);
 logf("boot");
 ready=pcaInit();
 logf(ready?"pca ok, servo parked":"pca error, check port A");
 NimBLEDevice::init("NodKick");
 client=NimBLEDevice::createClient();
 logf("scanning %s",MAC);
 draw();                                // paint before the first blocking scan
}
void loop(){
 M5.update();
 uint32_t now=millis();
 dev=state!=IDLE&&imuLive()?deviation():0;
 if(!imuLive())atTop=false;             // a gap in the feed disarms the current nod
 bool p=M5.Touch.getCount()&&M5.Touch.getDetail(0).isPressed();
 if(p){
  if(!down){
   down=true;auto t=M5.Touch.getDetail(0);
   if(t.y>=BY&&t.y<BY+BH){
    if(t.x>=B0X&&t.x<B0X+BW)doReset();
    else if(t.x>=B1X&&t.x<B1X+BW)doSample();
   }
  }
 }else down=false;
 if(busy&&(int32_t)(now-holdUntil)>=0){busy=false;pulse(REST_US);}
 if(state==ARMED&&ref>0&&imuLive()){     // one nod = upper band -> lower band
  if(atTop){ if(dev>=HIGH_F*ref){count++;atTop=false;flashFrom=now;doKick();} }
  else if(dev<=LOW_F*ref)atTop=true;
 }
 if(flashFrom&&now-flashFrom>FLASH_MS)flashFrom=0;
 static uint32_t lastDraw=0;
 if(now-lastDraw>33){lastDraw=now;draw();}
 static bool wasConn=false;              // the link state changes are worth a line
 bool conn=client->isConnected();
 if(conn!=wasConn){wasConn=conn;logf(conn?"imu link up":"link lost, rescanning");}
 if(!conn){client->disconnect();draw();link();}
 delay(5);
}
