// ============================================================
//  ESP MULTITOOL ADVANCED  -  menu base  (ESP-BOY: ESP32 + ILI9341 320x240)
//  Albero cyberpunk + impostazioni (tema, colore) + SIDELOADER.
//  Le funzioni pesanti (per ora IR) sono moduli a parte in /MULTITOOL sulla SD:
//  ir.bin  (vedi mod_ir.c).
//  Libreria richiesta: MultitoolCore (cartella da copiare in Arduino/libraries).
//  Partition scheme: Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
// ============================================================
#include <MultitoolCore.h>

#pragma region CONFIGURAZIONE
float BOOT_SPEED = 1.0;          // animazione boot: 1.0 = ~2 s | 2.0 = ~1 s | 0.5 = ~4 s

#define T_DIR 0
#define T_FUN 1
#define F_IR     1
#define F_SIDE   2
#define F_THEME  4
#define F_ACCENT 5

#define MOD_DIR "/MULTITOOL"     // moduli (.bin) sulla SD

struct Node {
  const char* name;
  int8_t parent;
  uint8_t type;
  uint8_t id;
  const char* info;
  const char* bin;     // percorso del modulo .bin (NULL = voce interna)
};

#define MAX_NODES 32   // massimo voci dell'albero
#define TREE_ROWS 9

// albero
bool expanded[MAX_NODES];
int vis[MAX_NODES];
int nVis = 0;
int tCur = 0;
int treeTop = 0;
int sideCount = 0;
#pragma endregion

#pragma region BOOT
// ===================== BOOT ANIMATION =======================
#define BOOT_BASE_MS 2000      // durata a velocita' 1.0
#define BCW 280                // canvas del logo
#define BCH 90
#define BX0 ((320 - BCW) / 2)
#define BY0 ((240 - BCH) / 2)

static float bClamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float bSeg(float t, float a, float b) { return bClamp01((t - a) / (b - a)); }
static float bEaseOut(float x) { float u = 1.0f - x; return 1.0f - u * u * u; }
static float bEaseInOut(float x) { return x < 0.5f ? 4 * x * x * x : 1.0f - powf(-2 * x + 2, 3) / 2.0f; }

static uint16_t bCol(uint8_t r, uint8_t g, uint8_t b, float k) {
  k = bClamp01(k);
  r = (uint8_t)(r * k); g = (uint8_t)(g * k); b = (uint8_t)(b * k);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void bLine(GFXcanvas16 &c, int x0, int y0, int x1, int y1, uint16_t col, int th) {
  for (int dx = 0; dx < th; dx++)
    for (int dy = 0; dy < th; dy++)
      c.drawLine(x0 + dx, y0 + dy, x1 + dx, y1 + dy, col);
}

static void bStar(GFXcanvas16 &c, float cx, float cy, float R, float rot, float prog, uint16_t col) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = rot + i * PI / 5.0f - PI / 2.0f;
    float rr = (i % 2 == 0) ? R : R * 0.42f;
    px[i] = cx + cosf(a) * rr;
    py[i] = cy + sinf(a) * rr;
  }
  float edges = prog * 10.0f;
  for (int i = 0; i < 10; i++) {
    float e = edges - i;
    if (e <= 0) break;
    if (e > 1) e = 1;
    int j = (i + 1) % 10;
    int x1 = (int)(px[i] + (px[j] - px[i]) * e);
    int y1 = (int)(py[i] + (py[j] - py[i]) * e);
    bLine(c, (int)px[i], (int)py[i], x1, y1, col, 2);
  }
}

static void bChevron(GFXcanvas16 &c, int cx, int cy, int w, int h, int dir, uint16_t col) {
  int tipX = cx + dir * w / 2;
  int endX = cx - dir * w / 2;
  bLine(c, endX, cy - h / 2, tipX, cy, col, 3);
  bLine(c, tipX, cy, endX, cy + h / 2, col, 3);
}

static void bGlitchSlices(GFXcanvas16 &c, int cx, int n) {
  uint16_t *buf = c.getBuffer();
  const int segW = 70;
  int sx = cx - segW / 2;
  for (int k = 0; k < n; k++) {
    int y = random(8, BCH - 8);
    int hgt = random(2, 9);
    int shift = random(-18, 19);
    if (shift == 0) continue;
    for (int r = y; r < y + hgt && r < BCH; r++) {
      uint16_t *seg = buf + r * BCW + sx;
      if (shift > 0) {
        memmove(seg + shift, seg, (segW - shift) * 2);
        memset(seg, 0, shift * 2);
      } else {
        int s = -shift;
        memmove(seg, seg + s, (segW - s) * 2);
        memset(seg + segW - s, 0, s * 2);
      }
    }
  }
}

void bootGaetron(Adafruit_ILI9341 &d) {
  float spd = BOOT_SPEED < 0.05f ? 0.05f : BOOT_SPEED;
  uint32_t dur = (uint32_t)(BOOT_BASE_MS / spd);

  GFXcanvas16 *cv = new GFXcanvas16(BCW, BCH);
  if (!cv || !cv->getBuffer()) { if (cv) delete cv; return; }

  const int cx = BCW / 2, cy = BCH / 2;
  const float R = 22.0f;            // stella (piu' piccola delle frecce)
  const int CHW = 22, CHH = 60;     // frecce
  const int CH_REST = 62;           // distanza frecce dal centro a riposo
  const int CH_MOVE = 70;           // quanto si allontanano

  d.fillScreen(ILI9341_BLACK);
  uint32_t t0 = millis();

  while (true) {
    float t = (float)(millis() - t0) / (float)dur;
    if (t > 1.0f) t = 1.0f;

    float starP   = bSeg(t, 0.00f, 0.30f);
    float arrowIn = bSeg(t, 0.10f, 0.30f);
    float arrowMv = bEaseInOut(bSeg(t, 0.38f, 0.80f));
    float arrowFd = 1.0f - bSeg(t, 0.55f, 0.95f);
    float glitchOn = (t > 0.45f && t < 0.70f) ? 1.0f : 0.0f;
    float starFd  = 1.0f - bSeg(t, 0.65f, 1.0f);

    cv->fillScreen(0);

    float ak = arrowIn * arrowFd;
    if (ak > 0.01f) {
      uint16_t ca = bCol(255, 255, 255, ak);
      int off = (int)(arrowMv * CH_MOVE);
      bChevron(*cv, cx - CH_REST - off, cy, CHW, CHH, -1, ca);
      bChevron(*cv, cx + CH_REST + off, cy, CHW, CHH, +1, ca);
    }

    float sk = starFd;
    if (sk > 0.01f && starP > 0.0f) {
      float e = bEaseOut(starP);
      float Rc = R * (0.3f + 0.7f * e);
      float rot = (1.0f - e) * 1.2566f;
      bool show = true;
      if (glitchOn > 0.5f && random(100) < 22) show = false;
      if (show) {
        if (glitchOn > 0.5f) {
          int gx = random(2, 6);
          bStar(*cv, cx - gx, cy, Rc, rot, starP, bCol(255, 0, 60, sk));
          bStar(*cv, cx + gx, cy, Rc, rot, starP, bCol(0, 255, 255, sk));
        }
        bStar(*cv, cx, cy, Rc, rot, starP, bCol(255, 255, 255, sk));
        if (glitchOn > 0.5f) bGlitchSlices(*cv, cx, random(2, 6));
      }
    }

    d.drawRGBBitmap(BX0, BY0, cv->getBuffer(), BCW, BCH);
    if (t >= 1.0f) break;
  }

  d.fillScreen(ILI9341_BLACK);
  delete cv;
}
// =================== FINE BOOT ANIMATION ====================
#pragma endregion

#pragma region MENU
// ======================= ALBERO: voci e descrizioni (una region per funzione) =======================
// IMPOSTAZIONI (con le sue voci) resta SEMPRE in fondo: aggiungi le nuove voci PRIMA di NI_IMPOSTAZIONI,
// sia nell'enum sia nell'elenco NODES (devono avere lo stesso ordine, altrimenti il controllo sotto blocca la compilazione).
enum NodeIdx {
  NI_SENSORI, NI_IR_REMOTE, NI_SIDELOADER,
  NI_IMPOSTAZIONI, NI_TEMA, NI_COLORE,
  NI_COUNT
};

// Ogni voce e' una macro NODE_xxx: { nome, genitore (NI_xxx oppure -1 = radice), tipo, id funzione, descrizione [, file .bin del modulo] }
// Le voci con il .bin si avviano come moduli (sketch separati in /MULTITOOL sulla SD); le altre sono interne.
#pragma region SENSORI
#define NODE_SENSORI { "SENSORI", -1, T_DIR, 0, \
  "CARTELLA SENSORI\n\nStrumenti per leggere e simulare segnali dal mondo esterno.\n\nA / DX: apri\nSX / B: chiudi" }
#pragma endregion

#pragma region IR REMOTE
#define NODE_IR_REMOTE { "IR REMOTE", NI_SENSORI, T_FUN, F_IR, \
  "IR REMOTE\n\nLegge e ritrasmette segnali IR. Salva i telecomandi su SD.\n\nPIN CONDIVISO:\nGPIO 13\n(lettura + invio)\n\nA: entra", MOD_DIR "/ir.bin" }
#pragma endregion

#pragma region SIDELOADER
#define NODE_SIDELOADER { "SIDELOADER", -1, T_FUN, F_SIDE, \
  "SIDELOADER\n\nAvvia un .bin dalla cartella /SIDELOADER (anche sottocartelle).\n\nAl riavvio riparte il multitool.\n\nA: entra" }
#pragma endregion

#pragma region IMPOSTAZIONI
#define NODE_IMPOSTAZIONI { "IMPOSTAZ.", -1, T_DIR, 0, \
  "IMPOSTAZIONI\n\nTema e colore.\n\nA / DX: apri\nSX / B: chiudi" }
#pragma endregion

#pragma region TEMA
#define NODE_TEMA { "TEMA", NI_IMPOSTAZIONI, T_FUN, F_THEME, \
  "TEMA\n\nScuro o chiaro.\nPredefinito: scuro.\n\nA: cambia" }
#pragma endregion

#pragma region COLORE
#define NODE_COLORE { "COLORE", NI_IMPOSTAZIONI, T_FUN, F_ACCENT, \
  "COLORE SECONDARIO\n\nColore di testi e riquadri.\nPredefinito: rosso.\n\nA: cambia" }
#pragma endregion

// elenco voci, in ordine di profondita' (stesso ordine di NodeIdx; IMPOSTAZIONI sempre per ultima)
const Node NODES[] = {
  NODE_SENSORI,
  NODE_IR_REMOTE,
  NODE_SIDELOADER,
  NODE_IMPOSTAZIONI,
  NODE_TEMA,
  NODE_COLORE,
};
#define N_NODES ((int)(sizeof(NODES) / sizeof(NODES[0])))
static_assert(NI_COUNT == (int)(sizeof(NODES) / sizeof(NODES[0])), "NODES e NodeIdx non hanno lo stesso numero di voci");

#pragma region LOGICA E INTERFACCIA
// ===================== ALBERO =====================
void buildVisible() {
  nVis = 0;
  for (int i = 0; i < N_NODES; i++) {
    bool ok = true;
    int p = NODES[i].parent;
    while (p >= 0) {
      if (!expanded[p]) { ok = false; break; }
      p = NODES[p].parent;
    }
    if (ok) vis[nVis++] = i;
  }
}

int depthOf(int i) {
  int d = 0;
  int p = NODES[i].parent;
  while (p >= 0) { d++; p = NODES[p].parent; }
  return d;
}

void drawTreeRow(int vi) {
  if (vi < treeTop || vi >= treeTop + TREE_ROWS || vi >= nVis) return;
  int y = 29 + (vi - treeTop) * 20;
  bool sel = (vi == tCur);
  uint16_t bg = sel ? C_RED : C_BG;
  uint16_t fg = sel ? C_BG : C_RED;
  tft.fillRect(5, y, 189, 19, bg);
  int n = vis[vi];
  int id = NODES[n].id;
  int d = depthOf(n);
  int x = 8 + d * 12;
  if (d > 0 && !sel) {
    tft.drawFastVLine(8 + (d - 1) * 12 + 4, y, 19, C_DRED);
    tft.drawFastHLine(8 + (d - 1) * 12 + 4, y + 9, 6, C_DRED);
  }
  char buf[28];
  if (NODES[n].type == T_DIR) snprintf(buf, sizeof(buf), "%s %s", expanded[n] ? "[-]" : "[+]", NODES[n].name);
  else if (id == F_THEME) snprintf(buf, sizeof(buf), "> TEMA: %s", themeDark ? "SCURO" : "CHIARO");
  else snprintf(buf, sizeof(buf), "> %s", NODES[n].name);
  txt(x + 2, y + 3, buf, 2, fg, bg);
  if (NODES[n].type == T_FUN) {
    if (id == F_ACCENT) {
      tft.drawRect(158, y + 4, 26, 12, fg);
      tft.fillRect(160, y + 6, 22, 8, sel ? C_BG : C_RED);
    }
  }
}

void drawTreeRows() {
  for (int r = 0; r < TREE_ROWS; r++) {
    int vi = treeTop + r;
    if (vi < nVis) drawTreeRow(vi);
    else tft.fillRect(5, 29 + r * 20, 189, 19, C_BG);
  }
}

void drawInfo() {
  int n = vis[tCur];
  tft.fillRect(201, 26, 116, 189, C_BG);
  tft.fillRect(204, 29, 110, 12, C_RED);
  txt(208, 31, "// INFO", 1, C_BG, C_RED);
  drawWrapped(206, 48, 17, 14, NODES[n].info, C_RED, C_BG);
  char b[24];
  b[0] = 0;
  if (NODES[n].bin) {
    if (!sdOk) snprintf(b, sizeof(b), "SD ASSENTE");
    else snprintf(b, sizeof(b), "MODULO: %s", SD.exists(NODES[n].bin) ? "OK" : "MANCA");
  } else if (NODES[n].type == T_DIR) {
    int c = 0;
    for (int i = 0; i < N_NODES; i++) if (NODES[i].parent == n) c++;
    snprintf(b, sizeof(b), "VOCI: %d", c);
  } else if (NODES[n].id == F_SIDE) {
    if (!sdOk) snprintf(b, sizeof(b), "SD ASSENTE");
    else snprintf(b, sizeof(b), "VOCI: %d", sideCount);
  } else if (NODES[n].id == F_THEME) {
    snprintf(b, sizeof(b), "ORA: %s", themeDark ? "SCURO" : "CHIARO");
  } else if (NODES[n].id == F_ACCENT) {
    snprintf(b, sizeof(b), "ORA: %s", ACC_NAMES[accentIdx]);
  }
  if (b[0]) {
    tft.drawFastHLine(204, 198, 110, C_DRED);
    txt(206, 203, b, 1, C_MID, C_BG);
  }
}

// titolo: "ESP_MULTITOOL" grande + "_ADVANCED" piccolo, cosi' lascia spazio alle tre icone
void drawTreeHeader() {
  drawHeader("ESP_MULTITOOL", "");
  txt(6 + 13 * 12, 9, "_ADVANCED", 1, C_RED, C_BG);
  drawStatusIcons(false, false);
}

void drawTreeAll() {
  tft.fillScreen(C_BG);
  drawTreeHeader();
  chamfer(2, 25, 195, 191, 6, C_RED);
  chamfer(200, 25, 118, 191, 6, C_RED);
  drawFooter("SU/GIU MUOVI  A APRI  B INDIETRO");
  drawTreeRows();
  drawInfo();
}

// ===================== MENU PRINCIPALE =====================
// avvia il modulo (sketch separato sulla SD) tramite il launcher
void launchModule(const char* path) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if (!sdOk) {
    msgBox("SD ASSENTE", "INSERISCI LA SD");
  } else if (!SD.exists(path)) {
    msgBox("MODULO MANCANTE", base);
  } else {
    mtLaunch(path);      // non ritorna se va a buon fine
  }
  drawTreeAll();
}

void runNode(int n) {
  int id = NODES[n].id;
  if (NODES[n].bin) { launchModule(NODES[n].bin); return; }
  if (id == F_THEME)  { toggleTheme(); drawTreeAll(); return; }
  if (id == F_ACCENT) { nextAccent();  drawTreeAll(); return; }
  if (id == F_SIDE) sideApp();
  sideRefreshCount();
  drawTreeAll();
}

void treeLoop() {
  static uint32_t lastStruct = 0;
  pollButtons();
  int oldCur = tCur, oldTop = treeTop;
  bool structure = false;

  if (ev[K_UP] && tCur > 0) tCur--;
  if (ev[K_DOWN] && tCur < nVis - 1) tCur++;

  // A / DX: apre la cartella; se e' gia' aperta entra nella prima voce; su una funzione la avvia
  if (ev[K_RIGHT] || ev[K_A]) {
    int n = vis[tCur];
    if (NODES[n].type == T_DIR) {
      if (!expanded[n]) {
        expanded[n] = true;
        structure = true;
        lastStruct = millis();
      } else if (tCur + 1 < nVis && NODES[vis[tCur + 1]].parent == n) {
        tCur++;
      }
    } else if (ev[K_A]) {
      runNode(n);
      return;
    }
  }

  // SX / B: chiude la cartella aperta, altrimenti torna al genitore (ignorato subito dopo un'apertura)
  if ((ev[K_LEFT] || ev[K_B]) && (millis() - lastStruct) > 300) {
    int n = vis[tCur];
    if (NODES[n].type == T_DIR && expanded[n]) {
      expanded[n] = false;
      structure = true;
      lastStruct = millis();
    } else if (NODES[n].parent >= 0) {
      int p = NODES[n].parent;
      for (int i = 0; i < nVis; i++) if (vis[i] == p) { tCur = i; break; }
    }
  }

  if (structure) {
    int keep = vis[tCur];
    buildVisible();
    for (int i = 0; i < nVis; i++) if (vis[i] == keep) { tCur = i; break; }
  }

  if (tCur < treeTop) treeTop = tCur;
  if (tCur >= treeTop + TREE_ROWS) treeTop = tCur - TREE_ROWS + 1;

  if (structure || treeTop != oldTop) {
    drawTreeRows();
    drawInfo();
  } else if (tCur != oldCur) {
    drawTreeRow(oldCur);
    drawTreeRow(tCur);
    drawInfo();
  }
}
#pragma endregion
#pragma endregion

#pragma region SIDELOADER
// ===================== SIDELOADER: avvia un .bin dalla SD =====================
// Scrive la richiesta /SIDELOAD.REQ sulla SD e riavvia sul launcher (partizione di boot gia' impostata
// dalla riga OTA). Il launcher legge la richiesta UNA SOLA VOLTA, la cancella e carica il .bin scelto.
// Allo spegnimento/riaccensione non c'e' piu' la richiesta: il launcher carica di nuovo il multitool.
#define SIDE_DIR        "/SIDELOADER"
#define SIDE_MAX_ENTS   32     // voci mostrate per cartella
#define SIDE_MAX_DEPTH  4      // livelli di sottocartelle sotto /SIDELOADER
#define SIDE_PATH_LEN   96     // lunghezza massima del percorso completo
char sidePath[SIDE_PATH_LEN] = SIDE_DIR;   // cartella attualmente aperta
char ents[SIDE_MAX_ENTS][40];              // nomi (max 39 caratteri)
bool entDir[SIDE_MAX_ENTS];                // true = cartella
int nEnts = 0;

// livello della cartella aperta: 0 = /SIDELOADER
int sideDepth() {
  int d = 0;
  for (const char* q = sidePath; *q; q++) if (*q == '/') d++;
  return d - 1;
}

// elenca la cartella aperta: prima le sottocartelle, poi i file .bin
void sideListEntries() {
  nEnts = 0;
  bool wantDirs = (sideDepth() < SIDE_MAX_DEPTH);
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 0 && !wantDirs) continue;
    File dir = SD.open(sidePath);
    if (!dir) return;
    File f = dir.openNextFile();
    while (f && nEnts < SIDE_MAX_ENTS) {
      String n = f.name();
      int sl = n.lastIndexOf('/');
      if (sl >= 0) n = n.substring(sl + 1);
      bool d = f.isDirectory();
      if (n.length() > 0 && n.length() < 40 && n[0] != '.') {
        bool take = false;
        if (pass == 0) {
          take = d && !n.equalsIgnoreCase("System Volume Information");
        } else if (!d) {
          String u = n;
          u.toUpperCase();
          take = u.endsWith(".BIN");
        }
        if (take) {
          strncpy(ents[nEnts], n.c_str(), 39);
          ents[nEnts][39] = 0;
          entDir[nEnts] = d;
          nEnts++;
        }
      }
      f.close();
      f = dir.openNextFile();
    }
    if (f) f.close();
    dir.close();
  }
}

// conteggio mostrato nel riquadro info (voci della cartella principale)
void sideRefreshCount() {
  sideCount = 0;
  if (!sdOk) return;
  strcpy(sidePath, SIDE_DIR);
  sideListEntries();
  sideCount = nEnts;
}

const char* lblEnts(int i) {
  static char b[28];
  if (entDir[i]) snprintf(b, sizeof(b), "[+] %.20s", ents[i]);
  else snprintf(b, sizeof(b), "> %.22s", ents[i]);
  return b;
}

void sideApp() {
  if (!sdOk) {
    msgBox("SD ASSENTE", "INSERISCI LA SD");
    return;
  }
  if (!SD.exists(SIDE_DIR)) SD.mkdir(SIDE_DIR);
  strcpy(sidePath, SIDE_DIR);
  sideListEntries();
  LV l = { nEnts, 0, 0, lblEnts };
  char title[20];
  char right[12];

  auto redraw = [&]() {
    const char* base = strrchr(sidePath, '/') + 1;
    snprintf(title, sizeof(title), "%.16s", base);
    snprintf(right, sizeof(right), "%d VOCI", nEnts);
    irStatic(title, "A APRI  B INDIETRO", right);
    l.count = nEnts;
    lvFix(l);
    if (nEnts > 0) {
      lvRows(l);
    } else {
      txtC(84, "CARTELLA VUOTA", 2, C_RED, C_BG);
      txtC(116, sidePath, 1, C_MID, C_BG);
    }
  };
  redraw();

  while (true) {
    pollButtons();
    lvNav(l);

    if (ev[K_B]) {
      if (sideDepth() == 0) return;          // dalla radice esce dalla funzione
      *strrchr(sidePath, '/') = 0;           // altrimenti sale di un livello
      sideListEntries();
      l.cur = 0;
      l.top = 0;
      redraw();
    } else if (ev[K_A] && nEnts > 0) {
      if (strlen(sidePath) + 1 + strlen(ents[l.cur]) >= SIDE_PATH_LEN - 1) {
        msgBox("PERCORSO", "TROPPO LUNGO");
        redraw();
      } else if (entDir[l.cur]) {
        strcat(sidePath, "/");
        strcat(sidePath, ents[l.cur]);
        sideListEntries();
        l.cur = 0;
        l.top = 0;
        redraw();
      } else {
        char full[SIDE_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", sidePath, ents[l.cur]);
        char nm[24];
        snprintf(nm, sizeof(nm), "%.20s", ents[l.cur]);
        if (confirmBox("AVVIARE?", nm)) mtLaunch(full);
        redraw();
      }
    }
    delay(2);
  }
}
#pragma endregion

#pragma region SETUP / LOOP
void setup() {
  esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
  mtInit();

  // l'animazione di boot parte solo all'accensione, non quando si torna da un modulo
  if (!mtTakeSkipBoot()) {
    randomSeed(micros());
    tft.fillScreen(ILI9341_BLACK);
    bootGaetron(tft);
  }

  mtMountSd();
  if (sdOk) {
    if (!SD.exists(SIDE_DIR)) SD.mkdir(SIDE_DIR);
    if (!SD.exists(MOD_DIR)) SD.mkdir(MOD_DIR);
  }
  sideRefreshCount();

  buildVisible();
  drawTreeAll();
}

void loop() {
  treeLoop();
  delay(2);
}
#pragma endregion
