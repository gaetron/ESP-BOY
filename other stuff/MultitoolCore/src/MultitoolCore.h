// ============================================================
//  MultitoolCore - libreria comune ESP-BOY Multitool (header-only)
//  display, bottoni, tema/colore, liste, tastiera T9, avvio/uscita dei moduli
//  Va incluso da UN solo file .ino per volta.
// ============================================================
#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include "esp_ota_ops.h"

#pragma region PIN E IMPOSTAZIONI
#define TFT_CS  15
#define TFT_DC  2
#define TFT_RST 4
#define SD_CS   5

#define DEBOUNCE_MS      25      // antirimbalzo bottoni
#define REPEAT_DELAY_MS  380     // ritardo prima dell'auto-ripetizione (frecce)
#define REPEAT_MS        110     // intervallo auto-ripetizione (frecce)
#define LONG_B_MS        700     // pressione lunga di B
#define T9_TAP_MS        900     // tempo massimo tra due pressioni per ciclare le lettere
#pragma endregion

#pragma region COLORI E TIPI
// ======================= COLORI (tema + colore secondario, cambiano dalle impostazioni) =======================
uint16_t gBg   = 0x0000;   // sfondo
uint16_t gAcc  = 0xF800;   // colore secondario (testi e riquadri)
uint16_t gMid  = 0xB000;   // tonalita' intermedie (calcolate da applyTheme)
uint16_t gDred = 0x7800;
uint16_t gDim  = 0x3000;
#define C_BG   gBg
#define C_RED  gAcc
#define C_MID  gMid
#define C_DRED gDred
#define C_DIM  gDim

const uint16_t ACCENTS[] = { 0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x07FF, 0x04FF, 0xA81F, 0xF81F };
const char* ACC_NAMES[]  = { "ROSSO", "ARANCIO", "GIALLO", "VERDE", "CIANO", "AZZURRO", "VIOLA", "ROSA" };
#define N_ACCENTS ((int)(sizeof(ACCENTS) / sizeof(ACCENTS[0])))
bool themeDark = true;     // predefinito: scuro
int accentIdx = 0;         // predefinito: rosso


typedef const char* (*LabelFn)(int);

struct LV {
  int count;
  int cur;
  int top;
  LabelFn fn;
};
#define LV_ROWS 8
uint32_t statusUntil = 0;
bool statusOn = false;
#pragma endregion

#pragma region DISPLAY E BOTTONI
class EspBoyTFT : public Adafruit_ILI9341 {
public:
  EspBoyTFT(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ILI9341(cs, dc, rst) {}
  void setLandscapeNative() {
    _width = 320;
    _height = 240;
    uint8_t m = 0x40;
    sendCommand(ILI9341_MADCTL, &m, 1);
  }
};

EspBoyTFT tft(TFT_CS, TFT_DC, TFT_RST);

bool sdOk = false;

// bottoni
#define NB 6
#define K_UP    0
#define K_DOWN  1
#define K_LEFT  2
#define K_RIGHT 3
#define K_A     4
#define K_B     5
const uint8_t BTN_PIN[NB] = { 27, 12, 14, 26, 32, 33 };
bool ev[NB];
bool evBLong = false;
bool kRaw[NB];
bool kStable[NB];
uint32_t kChg[NB];
uint32_t kDown[NB];
uint32_t kRep[NB];
bool kLongDone = false;


// T9: indici 0-8 = tasti 1-9, 9 = DEL, 10 = tasto 0, 11 = OK
const char* T9CH[12] = { "1-_", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9", "", "0", "" };
#pragma endregion

#pragma region UTILITY (BOTTONI / GRAFICA / LISTE)
// ===================== BOTTONI =====================
void pollButtons() {
  uint32_t now = millis();
  for (int i = 0; i < NB; i++) ev[i] = false;
  evBLong = false;
  for (int i = 0; i < NB; i++) {
    bool r = (digitalRead(BTN_PIN[i]) == LOW);
    if (r != kRaw[i]) { kRaw[i] = r; kChg[i] = now; }
    if ((now - kChg[i]) >= DEBOUNCE_MS && r != kStable[i]) {
      kStable[i] = r;
      if (r) {
        kDown[i] = now;
        kRep[i] = now + REPEAT_DELAY_MS;
        if (i == K_B) kLongDone = false; else ev[i] = true;
      } else {
        if (i == K_B && !kLongDone) ev[K_B] = true;   // B corto: evento al rilascio
      }
    }
    if (kStable[i]) {
      if (i <= K_RIGHT && (int32_t)(now - kRep[i]) >= 0) { ev[i] = true; kRep[i] = now + REPEAT_MS; }
      if (i == K_B && !kLongDone && (now - kDown[i]) >= LONG_B_MS) { kLongDone = true; evBLong = true; }
    }
  }
}

// ===================== GRAFICA DI BASE =====================
void txt(int x, int y, const char* s, uint8_t sz, uint16_t fg, uint16_t bg) {
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  tft.setCursor(x, y);
  tft.print(s);
}

void txtC(int y, const char* s, uint8_t sz, uint16_t fg, uint16_t bg) {
  int w = (int)strlen(s) * 6 * sz;
  int x = (320 - w) / 2;
  if (x < 0) x = 0;
  txt(x, y, s, sz, fg, bg);
}

// cornice con angoli smussati (alto-sinistra e basso-destra)
void chamfer(int x, int y, int w, int h, int c, uint16_t col) {
  tft.drawLine(x + c, y, x + w - 1, y, col);
  tft.drawLine(x + w - 1, y, x + w - 1, y + h - 1 - c, col);
  tft.drawLine(x + w - 1, y + h - 1 - c, x + w - 1 - c, y + h - 1, col);
  tft.drawLine(x + w - 1 - c, y + h - 1, x, y + h - 1, col);
  tft.drawLine(x, y + h - 1, x, y + c, col);
  tft.drawLine(x, y + c, x + c, y, col);
}

void drawHeader(const char* title, const char* right) {
  tft.fillRect(0, 0, 320, 23, C_BG);
  txt(6, 3, title, 2, C_RED, C_BG);
  int rl = (int)strlen(right) * 6;
  if (rl > 0) txt(314 - rl, 7, right, 1, C_MID, C_BG);
  tft.drawFastHLine(0, 21, 320, C_RED);
  tft.drawFastHLine(0, 22, 320, C_DRED);
}

void drawFooter(const char* hint) {
  tft.fillRect(0, 222, 320, 18, C_BG);
  tft.drawFastHLine(0, 222, 320, C_DRED);
  txt(6, 229, hint, 1, C_MID, C_BG);
}

// testo con a-capo automatico (wc = caratteri per riga, font size 1)
void drawWrapped(int x, int y, int wc, int maxLn, const char* s, uint16_t fg, uint16_t bg) {
  char line[40];
  int ll = 0;
  int ln = 0;
  tft.setTextSize(1);
  tft.setTextColor(fg, bg);
  auto emit = [&]() {
    if (ln < maxLn) {
      line[ll] = 0;
      tft.setCursor(x, y + ln * 10);
      tft.print(line);
    }
    ll = 0;
    ln++;
  };
  const char* p = s;
  while (*p && ln < maxLn) {
    if (*p == '\n') { emit(); p++; continue; }
    if (*p == ' ') { p++; continue; }
    int wl = 0;
    while (p[wl] && p[wl] != ' ' && p[wl] != '\n') wl++;
    if (wl > wc) wl = wc;
    int need = (ll ? ll + 1 : 0) + wl;
    if (need > wc) emit();
    if (ln >= maxLn) break;
    if (ll) line[ll++] = ' ';
    memcpy(line + ll, p, wl);
    ll += wl;
    p += wl;
  }
  if (ll && ln < maxLn) emit();
}

void setStatus(const char* m) {
  tft.fillRect(4, 200, 312, 20, C_BG);
  txt(8, 203, m, 2, C_RED, C_BG);
  statusUntil = millis() + 1800;
  statusOn = true;
}

void statusTick() {
  if (statusOn && (int32_t)(millis() - statusUntil) >= 0) {
    tft.fillRect(4, 200, 312, 20, C_BG);
    statusOn = false;
  }
}

void boxFrame(const char* l1, const char* l2, const char* hint) {
  tft.fillRect(30, 66, 260, 108, C_BG);
  chamfer(30, 66, 260, 108, 10, C_RED);
  txtC(84, l1, 2, C_RED, C_BG);
  txtC(112, l2, 2, C_RED, C_BG);
  txtC(150, hint, 1, C_MID, C_BG);
}

bool confirmBox(const char* l1, const char* l2) {
  boxFrame(l1, l2, "A: SI    B: NO");
  while (true) {
    pollButtons();
    if (ev[K_A]) return true;
    if (ev[K_B]) return false;
    delay(2);
  }
}

void msgBox(const char* l1, const char* l2) {
  boxFrame(l1, l2, "A: OK");
  while (true) {
    pollButtons();
    if (ev[K_A] || ev[K_B]) return;
    delay(2);
  }
}

// ===================== LISTE GENERICHE =====================
void lvRow(LV &l, int i) {
  if (i < l.top || i >= l.top + LV_ROWS || i >= l.count) return;
  int y = 29 + (i - l.top) * 20;
  bool sel = (i == l.cur);
  uint16_t bg = sel ? C_RED : C_BG;
  uint16_t fg = sel ? C_BG : C_RED;
  tft.fillRect(5, y, 310, 19, bg);
  txt(10, y + 3, l.fn(i), 2, fg, bg);
}

void lvRows(LV &l) {
  for (int r = 0; r < LV_ROWS; r++) {
    int i = l.top + r;
    if (i < l.count) lvRow(l, i);
    else tft.fillRect(5, 29 + r * 20, 310, 19, C_BG);
  }
}

void lvFix(LV &l) {
  if (l.cur >= l.count) l.cur = l.count - 1;
  if (l.cur < 0) l.cur = 0;
  if (l.cur < l.top) l.top = l.cur;
  if (l.cur >= l.top + LV_ROWS) l.top = l.cur - LV_ROWS + 1;
  if (l.top < 0) l.top = 0;
}

void lvNav(LV &l) {
  if (l.count <= 0) return;
  int oc = l.cur, ot = l.top;
  if (ev[K_UP])   l.cur = (l.cur > 0) ? l.cur - 1 : l.count - 1;
  if (ev[K_DOWN]) l.cur = (l.cur < l.count - 1) ? l.cur + 1 : 0;
  if (l.cur < l.top) l.top = l.cur;
  if (l.cur >= l.top + LV_ROWS) l.top = l.cur - LV_ROWS + 1;
  if (l.top != ot) lvRows(l);
  else if (l.cur != oc) { lvRow(l, oc); lvRow(l, l.cur); }
}

void irStatic(const char* title, const char* hint, const char* right) {
  tft.fillScreen(C_BG);
  drawHeader(title, right);
  chamfer(2, 25, 316, 172, 6, C_RED);
  drawFooter(hint);
  statusOn = false;
}

void drawScan(int pos) {
  for (int i = 0; i < 20; i++) {
    uint16_t c = (i == pos) ? C_RED : ((i == (pos + 19) % 20) ? C_DRED : C_DIM);
    tft.fillRect(40 + i * 12, 160, 10, 8, c);
  }
}
#pragma endregion

#pragma region TEMA E ICONE DI STATO
// ===================== IMPOSTAZIONI: tema, colore, WiFi, Bluetooth, icone di stato =====================
Preferences prefs;

// miscela due colori RGB565: t = quantita' di b (0..256)
uint16_t mix565(uint16_t a, uint16_t b, int t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bgc = (b >> 5) & 63, bb = b & 31;
  int r = (ar * (256 - t) + br * t) / 256;
  int g = (ag * (256 - t) + bgc * t) / 256;
  int bl = (ab * (256 - t) + bb * t) / 256;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// ricalcola tutti i colori dal tema e dal colore secondario
void applyTheme() {
  gBg = themeDark ? 0x0000 : 0xFFFF;
  gAcc = ACCENTS[accentIdx];
  gMid = mix565(gAcc, gBg, 80);
  gDred = mix565(gAcc, gBg, 136);
  gDim = mix565(gAcc, gBg, 207);
}

void loadSettings() {
  prefs.begin("mtool", false);
  themeDark = (prefs.getUChar("dark", 1) != 0);
  accentIdx = prefs.getUChar("acc", 0);
  prefs.end();
  if (accentIdx < 0 || accentIdx >= N_ACCENTS) accentIdx = 0;
}

void saveSettings() {
  prefs.begin("mtool", false);
  prefs.putUChar("dark", themeDark ? 1 : 0);
  prefs.putUChar("acc", (uint8_t)accentIdx);
  prefs.end();
}

void toggleTheme() {
  themeDark = !themeDark;
  applyTheme();
  saveSettings();
}

void nextAccent() {
  accentIdx = (accentIdx + 1) % N_ACCENTS;
  applyTheme();
  saveSettings();
}


// ---- icone di stato (dall'immagine di riferimento) ----
const uint8_t ICON_SD[] = {  // 16x17
  0xFF, 0xFC, 0x88, 0x12, 0x88, 0x12, 0x88, 0x12,
  0x88, 0x11, 0x8F, 0xF1, 0x80, 0x01, 0x80, 0x01,
  0x80, 0x01, 0x9F, 0xF9, 0x90, 0x09, 0x90, 0x09,
  0x90, 0x09, 0x9F, 0xF9, 0x80, 0x01, 0x80, 0x01,
  0xFF, 0xFF
};
const uint8_t ICON_WIFI[] = {  // 16x13
  0x03, 0xC0, 0x1C, 0x38, 0xE0, 0x07, 0x03, 0xC0,
  0x1C, 0x38, 0x60, 0x06, 0x01, 0x80, 0x06, 0x60,
  0x18, 0x18, 0x01, 0x80, 0x03, 0xC0, 0x03, 0xC0,
  0x01, 0x80
};
const uint8_t ICON_BT[] = {  // 10x14
  0x0C, 0x00, 0x0C, 0x00, 0xCF, 0x00, 0xCF, 0x00,
  0x3C, 0xC0, 0x3C, 0xC0, 0x0F, 0x00, 0x0F, 0x00,
  0x3C, 0xC0, 0x3C, 0xC0, 0xCF, 0x00, 0xCF, 0x00,
  0x0C, 0x00, 0x0C, 0x00
};

// SD sempre visibile (barrata se assente); WiFi e Bluetooth solo se accesi
void drawStatusIcons(bool wifiOn, bool btOn) {
  int x = 314;
  if (btOn) {
    x -= 10;
    tft.drawBitmap(x, 4, ICON_BT, 10, 14, C_RED);
    x -= 6;
  }
  if (wifiOn) {
    x -= 16;
    tft.drawBitmap(x, 5, ICON_WIFI, 16, 13, C_RED);
    x -= 6;
  }
  x -= 16;
  tft.drawBitmap(x, 2, ICON_SD, 16, 17, C_RED);
  if (!sdOk) {
    tft.drawLine(x, 2, x + 15, 18, 0xF800);
    tft.drawLine(x + 1, 2, x + 16, 18, 0xF800);
    tft.drawLine(x + 15, 2, x, 18, 0xF800);
    tft.drawLine(x + 16, 2, x + 1, 18, 0xF800);
  }
}
#pragma endregion

#pragma region TASTIERA T9
// ===================== TASTIERA T9 (programmata una volta, si richiama ovunque) =====================
// kbInput(titolo, buffer, lunghezza massima, completa)
//   completa = false -> nomi file: A-Z, 0-9, "-" e "_" (maiuscolo), max 11 caratteri
//   completa = true  -> testo libero (password ecc.): minuscole/maiuscole, numeri, simboli, spazio, max 63 caratteri
// Comandi: frecce = muovi, A = tasto (ripremi per ciclare le lettere), B = cancella, B lungo = annulla, OK = conferma.
// Ritorna true se confermato (testo in out), false se annullato.
#define KB_SYMBOLS "!@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~"
const char* KBFULL[12] = { "1.,?!'-_@", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9", "", "0", "" };
bool kbFull = false;     // modalita' corrente
bool kbUpper = false;    // maiuscolo (solo modalita' completa)

// tasti: 0-8 = 1-9, 9 = DEL, 10 = 0, 11 = OK; solo modalita' completa: 12 = abc/ABC, 13 = SYM, 14 = SPC, 15 = CLR
void kbRC(int k, int* r, int* c) {
  if (k < 12) { *r = k / 3; *c = k % 3; }
  else { *r = k - 12; *c = 3; }
}

int kbIdx(int r, int c) { return (c < 3) ? r * 3 + c : 12 + r; }

void kbPos(int k, int* x, int* y, int* w, int* h) {
  int r, c;
  kbRC(k, &r, &c);
  if (kbFull) { *x = 12 + c * 74; *w = 70; }
  else        { *x = 14 + c * 98; *w = 94; }
  *y = 68 + r * 36;
  *h = 32;
}

void kbKey(int k, bool sel) {
  int x, y, w, h;
  kbPos(k, &x, &y, &w, &h);
  uint16_t bg = sel ? C_RED : C_BG;
  uint16_t fg = sel ? C_BG : C_RED;
  tft.fillRect(x, y, w, h, bg);
  tft.drawRect(x, y, w, h, C_RED);
  const char* label = NULL;
  if (k == 9) label = "DEL";
  else if (k == 11) label = "OK";
  else if (k == 12) label = kbUpper ? "ABC" : "abc";
  else if (k == 13) label = "SYM";
  else if (k == 14) label = "SPC";
  else if (k == 15) label = "CLR";
  if (label) {
    int lw = (int)strlen(label) * 12;
    txt(x + (w - lw) / 2, y + 9, label, 2, fg, bg);
    return;
  }
  char dg[2];
  dg[0] = (k == 10) ? '0' : (char)('1' + k);
  dg[1] = 0;
  txt(x + 8, y + 9, dg, 2, fg, bg);
  char lt[8];
  lt[0] = 0;
  const char* ch = kbFull ? KBFULL[k] : T9CH[k];
  int cl = (int)strlen(ch);
  int n = 0;
  if (k == 0) {
    n = cl - 1;
    if (n > 5) n = 5;
    if (n < 0) n = 0;
    memcpy(lt, ch + 1, n);
  } else if (k <= 8) {
    n = cl - 1;
    if (n > 5) n = 5;
    memcpy(lt, ch, n);
    if (kbFull && kbUpper) for (int i = 0; i < n; i++) lt[i] = (char)toupper(lt[i]);
  }
  lt[n] = 0;
  txt(x + (kbFull ? 28 : 32), y + 12, lt, 1, fg, bg);
}

void kbField(const char* buf, int len) {
  tft.fillRect(14, 30, 292, 30, C_BG);
  char b[40];
  if (kbFull) {
    int show = (len > 21) ? 21 : len;
    int st = len - show;
    snprintf(b, sizeof(b), "%s%s_", st > 0 ? "<" : "", buf + st);
    txt(20, 40, b, 2, C_RED, C_BG);
  } else {
    snprintf(b, sizeof(b), "%s_", buf);
    txt(22, 35, b, 3, C_RED, C_BG);
  }
}

void kbCounter(int len, int maxLen) {
  char c[12];
  snprintf(c, sizeof(c), "%d/%d", len, maxLen);
  tft.fillRect(250, 4, 64, 14, C_BG);
  txt(314 - (int)strlen(c) * 6, 7, c, 1, C_MID, C_BG);
}

bool kbInput(const char* title, char* out, int maxLen, bool full) {
  static char buf[68];
  buf[0] = 0;
  int len = 0, lastKey = -1, tap = 0;
  uint32_t lastT = 0;
  int cur = 1;
  kbFull = full;
  kbUpper = false;
  int limit = full ? 63 : 11;
  if (maxLen > limit) maxLen = limit;
  const int cols = full ? 4 : 3;
  const int nk = full ? 16 : 12;

  tft.fillScreen(C_BG);
  drawHeader(title, "");
  drawFooter("A TASTO  B CANC  B LUNGO ESCI");
  chamfer(12, 28, 296, 34, 6, C_RED);
  for (int k = 0; k < nk; k++) kbKey(k, k == cur);
  kbField(buf, len);
  kbCounter(len, maxLen);

  while (true) {
    pollButtons();
    if (evBLong) return false;

    if (ev[K_B] && len > 0) {
      buf[--len] = 0;
      lastKey = -1;
      kbField(buf, len);
      kbCounter(len, maxLen);
    }

    int oc = cur, r, c;
    kbRC(cur, &r, &c);
    if (ev[K_LEFT])  c = (c + cols - 1) % cols;
    if (ev[K_RIGHT]) c = (c + 1) % cols;
    if (ev[K_UP])    r = (r + 3) % 4;
    if (ev[K_DOWN])  r = (r + 1) % 4;
    cur = kbIdx(r, c);
    if (cur != oc) { kbKey(oc, false); kbKey(cur, true); }

    if (ev[K_A]) {
      if (cur == 9) {                       // DEL
        if (len > 0) { buf[--len] = 0; kbField(buf, len); kbCounter(len, maxLen); }
        lastKey = -1;
      } else if (cur == 11) {               // OK
        if (len > 0) { strcpy(out, buf); return true; }
      } else if (cur == 12) {               // abc <-> ABC
        kbUpper = !kbUpper;
        for (int k = 0; k < nk; k++) kbKey(k, k == cur);
        lastKey = -1;
      } else if (cur == 14) {               // spazio
        if (len < maxLen) { buf[len++] = ' '; buf[len] = 0; kbField(buf, len); kbCounter(len, maxLen); }
        lastKey = -1;
      } else if (cur == 15) {               // cancella tutto
        len = 0;
        buf[0] = 0;
        kbField(buf, len);
        kbCounter(len, maxLen);
        lastKey = -1;
      } else {                              // tasti con lettere/simboli (multi-tap)
        const char* ch = (cur == 13) ? KB_SYMBOLS : (full ? KBFULL[cur] : T9CH[cur]);
        int cl = (int)strlen(ch);
        uint32_t now = millis();
        bool again = (cur == lastKey && len > 0 && (now - lastT) < T9_TAP_MS);
        tap = again ? (tap + 1) % cl : 0;
        if (again || len < maxLen) {
          char nc = ch[tap];
          if (full && kbUpper && cur != 13) nc = (char)toupper(nc);
          if (again) {
            buf[len - 1] = nc;
          } else {
            buf[len++] = nc;
            buf[len] = 0;
          }
          kbField(buf, len);
          kbCounter(len, maxLen);
          lastKey = cur;
          lastT = now;
        }
      }
    }
    delay(2);
  }
}

// versione per i nomi (usata da IR REMOTE)
bool t9Input(const char* title, char* out, int maxLen) {
  return kbInput(title, out, maxLen, false);
}
#pragma endregion

#pragma region MODULI (avvio e uscita)
// ===================== MODULI: avvio e uscita =====================
// Ogni funzione pesante e' un .bin a parte sulla SD. Il multitool lo avvia scrivendo /SIDELOAD.REQ e riavviando:
// il launcher carica quel .bin una volta sola. A fine funzione il modulo chiama mtExit(): il launcher
// ricarica il multitool (di default), senza rifare l'animazione di boot.
#define MT_REQ "/SIDELOAD.REQ"

// inizializzazione comune: pin, tema salvato, display
void mtInit() {
  pinMode(SD_CS, OUTPUT);  digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  for (int i = 0; i < NB; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
  loadSettings();
  applyTheme();
  tft.begin(16000000);
  tft.setRotation(0);
  tft.setLandscapeNative();
  tft.fillScreen(C_BG);
}

void mtMountSd() {
  sdOk = SD.begin(SD_CS);
}

// true se si torna da un modulo: in quel caso il multitool salta l'animazione di boot (una volta sola)
bool mtTakeSkipBoot() {
  prefs.begin("mtool", false);
  bool skip = (prefs.getUChar("skipboot", 0) != 0);
  if (skip) prefs.putUChar("skipboot", 0);
  prefs.end();
  return skip;
}

// avvia il .bin indicato tramite il launcher (non ritorna se va a buon fine)
void mtLaunch(const char* fullPath) {
  File f = SD.open(MT_REQ, FILE_WRITE);
  if (!f) {
    msgBox("ERRORE SD", "SCRITTURA");
    return;
  }
  f.print(fullPath);
  f.print('\n');
  f.close();

  const char* base = strrchr(fullPath, '/');
  base = base ? base + 1 : fullPath;
  irStatic("RIAVVIO", "", "");
  txtC(92, "AVVIO IN CORSO...", 2, C_RED, C_BG);
  txtC(120, base, 1, C_MID, C_BG);
  delay(700);
  esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));   // boot sul launcher
  SD.end();
  ESP.restart();
}

// fine modulo: riavvia, il launcher ricarica il multitool
void mtExit() {
  prefs.begin("mtool", false);
  prefs.putUChar("skipboot", 1);
  prefs.end();
  esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
  if (sdOk) SD.end();
  delay(50);
  ESP.restart();
}
#pragma endregion
