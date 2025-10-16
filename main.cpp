/*  ===== Photointerrupter Target – stand-alone AP version =====
 *  – unique SSID • captive portal • on-board scoreboard page   */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobotDFPlayerMini.h>

/* ────────────────── hardware pins ────────────────────────── */
#define SENSOR_PIN   32
#define LED_PIN      27
#define LED_COUNT    16
#define DFPLAYER_RX  16
#define DFPLAYER_TX  17

/* ───────────────── ISR / debouncing ─────────────────────── */
volatile uint8_t  pendingHits = 0;
volatile bool     armed       = true;
volatile uint32_t lastIsrTime = 0;
constexpr uint32_t ISR_DEBOUNCE_US = 5000;
constexpr unsigned long LOCKOUT_MS = 300;

/* ───────────────── objects ──────────────────────────────── */
WebSocketsServer   webSocket(81);          // max-clients = 4 via build flag
WebServer          web(80);
DNSServer          dns;
Adafruit_NeoPixel  strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
DFRobotDFPlayerMini dfplayer;

/* ─────────── sensor interrupt service routine ───────────── */
void IRAM_ATTR sensorIsr() {
  if (!armed) return;
  uint32_t now = micros();
  if (now - lastIsrTime > ISR_DEBOUNCE_US) {
    pendingHits++;  armed = false;  lastIsrTime = now;
  }
}

/* ───────────── full scoreboard page (fixed) ─────────────── */
const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<title>BELL TARGET SHOOTING</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<link href="https://fonts.googleapis.com/css2?family=Poppins:wght@400;600&display=swap" rel="stylesheet">
<style>
/* ─── GLOBAL RESET + BASIC SETUP ─────────────────────────── */
*{box-sizing:border-box;margin:0;padding:0;font-family:"Poppins",sans-serif;}
html,body{height:100%}
body{background:linear-gradient(135deg,#1f2024 0%,#3a416f 100%);
     color:#fafafa;text-align:center;padding:20px;overflow-x:hidden;}

/* ─── MAIN TITLE & SUBTITLE ──────────────────────────────── */
#mainTitle{font-weight:600;font-size:2.8rem;letter-spacing:2px;color:#00ffd5;
           text-shadow:0 2px 8px rgba(0,255,213,.6);margin-bottom:.2rem;}
#subtitle{font-weight:400;font-size:1rem;color:#d1d1d1;margin-bottom:1.2rem;font-style:italic;}

/* ─── STATUS LINE ────────────────────────────────────────── */
#status{font-size:.95rem;margin-bottom:1em;color:#d1d1d1;}

/* ─── SCOREBOARD & PLAYER CARDS ─────────────────────────── */
#scoreboard{display:flex;flex-wrap:wrap;justify-content:center;gap:1rem;margin-bottom:2rem;}
.playerBox{position:relative;width:160px;padding:1rem .8rem;background:rgba(255,255,255,.08);
  border:2px solid transparent;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,.3);
  cursor:pointer;transition:transform .15s,border-color .15s,background .15s;}
.playerBox:hover{transform:translateY(-4px);background:rgba(255,255,255,.12);}
.playerBox.selected{border-color:#00ffd5;background:rgba(0,255,213,.08);
  box-shadow:0 6px 16px rgba(0,255,213,.4);}
.playerBox input{width:100%;font-size:1rem;font-weight:600;color:#fff;background:transparent;
  border:none;text-align:center;margin-bottom:.4rem;outline:none;}
.playerBox input::placeholder{color:#ccc;}
.playerBox .score{font-size:2.2rem;font-weight:600;color:#00ffd5;margin-top:.2rem;
  text-shadow:0 1px 4px rgba(0,0,0,.5);}
.deleteBtn{position:absolute;top:6px;right:8px;background:none;border:none;color:#ff6b6b;
  font-size:1.1rem;cursor:pointer;transition:color .15s;outline:none;}
.deleteBtn:hover{color:#ff3b3b;}

/* ─── BUTTONS ────────────────────────────────────────────── */
button{font-family:"Poppins",sans-serif;font-size:1.05rem;font-weight:600;color:#1f2024;
  background:linear-gradient(90deg,#00ffd5 0%,#1affd0 100%);border:none;border-radius:8px;
  padding:.6rem 1.4rem;margin:.5rem;cursor:pointer;box-shadow:0 4px 8px rgba(0,0,0,.3);
  transition:transform .15s,box-shadow .15s;outline:none;}
button:hover:not(:disabled){transform:translateY(-2px);box-shadow:0 6px 12px rgba(0,0,0,.4);}
button:disabled{opacity:.4;cursor:not-allowed;}

/* ─── LED CIRCLE ─────────────────────────────────────────── */
#led{width:36px;height:36px;background:#444;border-radius:50%;margin:1rem auto 0;
     box-shadow:0 0 8px rgba(0,0,0,.5);transition:background-color .1s,box-shadow .1s;}
#led.active{background:#00ff8c;box-shadow:0 0 12px rgba(0,255,140,.8);}

/* ─── NEOPIXEL STRIP SIMULATOR ───────────────────────────── */
#ledStrip{margin:1.5rem auto 0;display:flex;flex-wrap:wrap;justify-content:center;gap:4px;width:fit-content;}
.ledPixel{width:22px;height:22px;background:#000;border-radius:4px;box-shadow:0 0 4px rgba(0,0,0,.5);
          transition:background-color .1s,box-shadow .1s;}
.ledPixel.on{box-shadow:0 0 12px rgba(255,255,255,.6);}

/* ─── INFO PANEL ─────────────────────────────────────────── */
#infoPanel{margin-top:2rem;padding-top:1rem;border-top:1px solid rgba(255,255,255,.2);
           max-width:500px;margin-left:auto;margin-right:auto;text-align:left;font-size:.9rem;color:#ccc;}
#infoPanel label{display:inline-block;width:130px;font-weight:600;color:#fafafa;}
#infoPanel span{color:#00ffd5;}
</style>
</head>
<body>
<h1 id="mainTitle">BELL TARGET SHOOTING</h1>
<div id="subtitle">by Alex Chavez</div>
<div id="status">WebSocket closed.</div>

<div id="scoreboard"></div>
<div><button id="btnAddPlayer">Add Player</button></div>
<div><button id="btnHit" disabled>Send Hit</button></div>

<div><div id="led"></div></div>
<div id="ledStrip"></div>

<div id="infoPanel">
  <div><label>Connection:</label><span id="txtConn">Closed</span></div>
  <div><label>Last WS Message:</label><span id="txtLastMsg">—</span></div>
</div>

<script>
/* ── WebSocket URL built from current host ──────────────── */
const WS_URL=`ws://${location.hostname}:81/`;

/* ── helpers: sleep + HSV→HEX ───────────────────────────── */
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function hsvToRgb(h,s,v){
  const c=v*s,hh=h/60,x=c*(1-Math.abs(hh%2-1));
  let[r1,g1,b1]=[0,0,0];
  if(hh<1){r1=c;g1=x;}else if(hh<2){r1=x;g1=c;}else if(hh<3){g1=c;b1=x;}
  else if(hh<4){g1=x;b1=c;}else if(hh<5){r1=x;b1=c;}else{r1=c;b1=x;}
  const m=v-c;return{r:Math.round((r1+m)*255),g:Math.round((g1+m)*255),b:Math.round((b1+m)*255)};}
const rgbToHex=(r,g,b)=>`#${[r,g,b].map(x=>x.toString(16).padStart(2,"0")).join("")}`;
const hsvToHex=h=>{const{r,g,b}=hsvToRgb(h,1,1);return rgbToHex(r,g,b);}
const hsvToHexVal=(h,val)=>{const{r,g,b}=hsvToRgb(h,1,val);return rgbToHex(r,g,b);}

/* ── DOM refs & state ───────────────────────────────────── */
const scoreboardDiv=document.getElementById("scoreboard");
const btnAddPlayer=document.getElementById("btnAddPlayer");
const btnHit=document.getElementById("btnHit");
const statusDiv=document.getElementById("status");
const txtConn=document.getElementById("txtConn");
const txtLastMsg=document.getElementById("txtLastMsg");
const ledCircle=document.getElementById("led");
const ledStripDiv=document.getElementById("ledStrip");
const LED_COUNT=16, ledCells=[];
let playerCount=0,activeIndex=-1;
const scores=[], nameInputs=[], scoreSpans=[], playerBoxes=[];   /* ← fixed '=' */

/* ── UI helpers & animations ─────────────────────────────── */
function flashCircleLED(){ledCircle.classList.add("active");setTimeout(()=>ledCircle.classList.remove("active"),200);}

async function simulateLEDStrip(){
  /* 1) RGB strobe (3×) */
  for(let k=0;k<3;k++){
    for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background="#FF0000";ledCells[i].classList.add("on");}
    await sleep(100);
    for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background="#00FF00";}
    await sleep(100);
    for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background="#0000FF";}
    await sleep(100);
    for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background="#000";ledCells[i].classList.remove("on");}
    await sleep(100);
  }
  /* 2) Rainbow wipe */
  for(let i=0;i<LED_COUNT;i++){
    const hue=(i*360/LED_COUNT)%360;
    ledCells[i].style.background=hsvToHex(hue);ledCells[i].classList.add("on");
    await sleep(50);}
  /* 3) Rainbow pulse */
  for(let j=0;j<256;j+=5){
    for(let i=0;i<LED_COUNT;i++){
      const hue=((i*360/LED_COUNT)+(j*360/256))%360;
      const bright=(100+80*Math.sin(j*Math.PI/128))/180;
      ledCells[i].style.background=hsvToHexVal(hue,bright);}
    await sleep(20);}
  for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background=hsvToHex(i*360/LED_COUNT);}
  /* 4) Theatre chase */
  for(let j=0;j<256;j+=32){
    for(let q=0;q<3;q++){
      for(let i=q;i<LED_COUNT;i+=3){
        const hue=((i*360/LED_COUNT)+j)%360;
        ledCells[i].style.background=hsvToHex(hue);ledCells[i].classList.add("on");}
      await sleep(50);
      for(let i=q;i<LED_COUNT;i+=3){ledCells[i].style.background="#000";ledCells[i].classList.remove("on");}}}
  /* 5) 3-second rainbow strobe */
  let endTime=Date.now()+3000,hue=0;
  while(Date.now()<endTime){
    for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background=hsvToHex(hue);ledCells[i].classList.add("on");}
    await sleep(50);hue=(hue+360*4096/65536)%360;}
  for(let i=0;i<LED_COUNT;i++){ledCells[i].style.background="#000";ledCells[i].classList.remove("on");}
}

function registerHit(){
  flashCircleLED();
  if(activeIndex>=0&&activeIndex<scores.length){
    scores[activeIndex]++;scoreSpans[activeIndex].textContent=scores[activeIndex];}
  simulateLEDStrip();
}

/* remove a player card */
function removePlayerAt(idx){
  scoreboardDiv.removeChild(playerBoxes[idx]);
  nameInputs.splice(idx,1);scoreSpans.splice(idx,1);scores.splice(idx,1);playerBoxes.splice(idx,1);
  if(idx<activeIndex)activeIndex--;
  else if(idx===activeIndex){
    if(scores.length>0){activeIndex=0;playerBoxes[0].classList.add("selected");}
    else{activeIndex=-1;btnHit.disabled=true;}}
  if(scores.length>0&&activeIndex<0){activeIndex=0;playerBoxes[0].classList.add("selected");}
  if(scores.length===0){activeIndex=-1;btnHit.disabled=true;}
}

/* add a new player card */
function addPlayer(){
  playerCount++;
  const box=document.createElement("div");box.className="playerBox";
  const del=document.createElement("button");del.className="deleteBtn";del.textContent="✕";del.title="Remove this player";
  const name=document.createElement("input");name.type="text";name.value=`Player ${playerCount}`;name.placeholder=`Player ${playerCount} Name`;
  const score=document.createElement("div");score.className="score";score.textContent="0";
  box.appendChild(del);box.appendChild(name);box.appendChild(score);scoreboardDiv.appendChild(box);
  nameInputs.push(name);scoreSpans.push(score);scores.push(0);playerBoxes.push(box);
  if(scores.length===1){activeIndex=0;box.classList.add("selected");btnHit.disabled=false;}

  box.addEventListener("click",e=>{
    if(e.target===del)return;
    if(activeIndex>=0&&activeIndex<playerBoxes.length)playerBoxes[activeIndex].classList.remove("selected");
    activeIndex=playerBoxes.indexOf(box);box.classList.add("selected");});

  del.addEventListener("click",()=>removePlayerAt(playerBoxes.indexOf(box)));
}

/* ── WebSocket logic ─────────────────────────────────────── */
let socket=null;
function connectWebSocket(){
  socket=new WebSocket(WS_URL);
  socket.onopen   =()=>{statusDiv.textContent="WebSocket connected!";txtConn.textContent="Connected";if(scores.length)btnHit.disabled=false;};
  socket.onclose  =()=>{statusDiv.textContent="WebSocket closed."; txtConn.textContent="Closed";btnHit.disabled=true;setTimeout(connectWebSocket,2000);};
  socket.onerror  = e =>console.warn("WS error:",e);
  socket.onmessage=e =>{
    const msg=e.data.trim();txtLastMsg.textContent=msg;
    if(msg.toLowerCase()==="hit")registerHit();};
}

/* ── page init ───────────────────────────────────────────── */
window.addEventListener("load",()=>{
  /* build the 16 strip pixels */
  for(let i=0;i<LED_COUNT;i++){const p=document.createElement("div");p.className="ledPixel";
    p.style.background="#000";ledStripDiv.appendChild(p);ledCells.push(p);}
  btnAddPlayer.addEventListener("click",addPlayer);

  btnHit.addEventListener("click",()=>{        /* FIX: no local registerHit() here */
    if(socket&&socket.readyState===WebSocket.OPEN){
      socket.send("hit");
      txtLastMsg.textContent="➡ hit (sent)";
    }
  });

  connectWebSocket();
});
</script>
</body></html>
)rawliteral";

/* forward declaration of LED + sound effect */
void performHitEffect();

/* ───────────────── helper: start soft-AP ────────────────── */
char ssidBuf[32];
const char *AP_PWD="shoot1234";
void startSoftAP(){
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(ssidBuf,sizeof(ssidBuf),"Target-%02X%02X%02X",mac[3],mac[4],mac[5]);
  WiFi.mode(WIFI_AP); WiFi.softAP(ssidBuf,AP_PWD);
  Serial.printf("[AP] SSID:%s  PWD:%s  IP:%s\n",
                ssidBuf,AP_PWD,WiFi.softAPIP().toString().c_str());
}

/* ───────────────── captive-portal web server ────────────── */
void setupWeb(){
  dns.start(53,"*",WiFi.softAPIP());
  web.onNotFound([](){ web.send_P(200,"text/html",PAGE_HTML); });
  web.begin();
}

/* ───────────────────────── setup() ───────────────────────── */
void setup(){
  Serial.begin(115200);
  pinMode(SENSOR_PIN,INPUT_PULLUP);
  attachInterrupt(SENSOR_PIN,sensorIsr,FALLING);
  strip.begin(); strip.setBrightness(100); strip.clear(); strip.show();

  startSoftAP();
  setupWeb();

  webSocket.begin();
  webSocket.onEvent([](uint8_t, WStype_t type, uint8_t* payload, size_t){
    if(type==WStype_TEXT && String((char*)payload)=="hit") pendingHits++;});

  Serial2.begin(9600,SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if(!dfplayer.begin(Serial2)){Serial.println("DFPlayer error");while(true)delay(1000);}
  dfplayer.volume(25);

  randomSeed(esp_random());
  Serial.println("Ready – break the beam to score.");
}

/* ───────────────────────── loop() ───────────────────────── */
void loop(){
  dns.processNextRequest();
  web.handleClient();
  webSocket.loop();

  /* re-arm once the beam has been HIGH 20 ms */
  static uint32_t hiStart=0;
  if(!armed && digitalRead(SENSOR_PIN)==HIGH){
    if(!hiStart) hiStart=millis();
    else if(millis()-hiStart>20){armed=true;hiStart=0;}}
  else hiStart=0;

  /* play effect if hit queued & lock-out passed */
  static unsigned long lastEff=0;
  if(pendingHits && millis()-lastEff>LOCKOUT_MS){
    noInterrupts(); uint8_t hits=pendingHits; pendingHits=0; interrupts();
    webSocket.broadcastTXT("HIT");
    Serial.printf(">> Hit (%u queued)\n",hits);
    performHitEffect();
    lastEff=millis();}
}

/* ───────────────── LED + sound effect ───────────────────── */
void performHitEffect(){
  int track=random(1,16);
  dfplayer.stop(); delay(50); dfplayer.play(track);
  for(int k=0;k<3;k++){
    strip.fill(strip.Color(255,0,0));strip.show();delay(100);
    strip.fill(strip.Color(0,255,0));strip.show();delay(100);
    strip.fill(strip.Color(0,0,255));strip.show();delay(100);
    strip.clear();strip.show();delay(100);}
  for(int i=0;i<LED_COUNT;i++){strip.setPixelColor(i,strip.ColorHSV((i*65536UL)/LED_COUNT));strip.show();delay(50);}
  for(int j=0;j<256;j+=5){
    for(int i=0;i<LED_COUNT;i++){
      strip.setPixelColor(i,strip.ColorHSV(((i*65536UL)/LED_COUNT + j*256)%65536));}
    strip.setBrightness(100+(80*sin(j*PI/128)));strip.show();delay(20);}
  strip.setBrightness(100);
  for(int j=0;j<256;j+=32){
    for(int q=0;q<3;q++){
      for(int i=q;i<LED_COUNT;i+=3){
        strip.setPixelColor(i,strip.ColorHSV(((i*65536UL)/LED_COUNT + j)%65536));}
      strip.show();delay(50);
      for(int i=q;i<LED_COUNT;i+=3)strip.setPixelColor(i,0);}}
  unsigned long end=millis()+3000;
  while(millis()<end){
    for(int h=0;h<65536 && millis()<end;h+=4096){
      strip.fill(strip.ColorHSV(h));strip.show();delay(50);}}
  strip.clear();strip.show();
}
