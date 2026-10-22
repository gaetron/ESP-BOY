
/

// ESP-BOY LAUNCHER - legge il .bin sulla SD, lo riscarica ad ogni accensione e lo avvia // Librerie: Adafruit GFX Library, Adafruit ILI9341 (SD e Update sono nel core ESP32) // Strumenti > Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)" #include <SPI.h> #include <SD.h> #includ

pasted

rafforziamolo. se allavvio tengo premuto "A" parte in qualssiasi caso la prima partizione di lettura sd,se tengo premuto "B" avvia lultimo gioco caricato, e fai che la prima partizione NON si puo cambiare, e deve essere compatibile con i giochi gia fatti

Memoria letta, memoria aggiornata e 2 altri strumenti
Ho preparato esp_boy_launcher.c, completo e pronto da compilare (rinominalo .ino per l’IDE). Non l’ho provato sull’hardware.

Avvio

Nessun tasto: funziona come prima, legge la SD, riscarica il .bin e lo avvia.
A premuto: parte comunque il launcher con la lettura SD e la riscarica. Se c’è un errore resta sul launcher e non salta mai a un gioco.
B premuto: avvia l’ultimo gioco caricato, senza toccare la SD. Funziona anche con la SD assente.
A e B insieme: vince A.
Suggerimento a schermo: nella schermata di avvio c’è una riga in basso con i due tasti.
Lettura dei tasti: li leggo per 800 ms dopo l’accensione, con pull-up (premuto = LOW). Il tempo si cambia con BOOT_KEY_WINDOW_MS.
Ultimo gioco

Gioco valido: viene segnato come valido solo dopo una copia completata. Prima di ogni copia lo segno come non valido, così una copia interrotta non può essere avviata con B. Controllo anche il byte iniziale della partizione.
Nessun gioco valido: con B compare “Nessun gioco caricato”.
Prima partizione protetta

Dove gira il launcher: deve girare da ota_0. Se non è così, non copia nulla e mostra l’errore.
Dove va il gioco: la copia va solo in ota_1, controllata esplicitamente.
Giochi esistenti: sono compatibili. La loro riga esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL)) da ota_1 punta ancora a ota_0, quindi non vanno ricompilati.
Partition scheme: resta Minimal SPIFFS (1.9MB APP with OTA).

Esp boy launcher
C 



Claude è un'IA e può commettere errori.
Esp boy launcher · C
// ESP-BOY LAUNCHER - legge il .bin sulla SD, lo copia nella 2a partizione e lo avvia
//
// Avvio:
//   nessun tasto -> legge la SD, riscarica il .bin e lo avvia
//   tieni A      -> parte in ogni caso il launcher: legge la SD e riscarica il .bin
//   tieni B      -> avvia l'ultimo gioco caricato (senza toccare la SD)
//
// Il launcher sta SEMPRE nella prima partizione (ota_0) e non viene mai scritta:
// la copia va solo nella seconda (ota_1). Se il launcher non gira da ota_0
// la copia viene bloccata. I giochi gia' fatti restano compatibili: la loro riga
// esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL)) punta ancora qui.
//
// Librerie: Adafruit GFX Library, Adafruit ILI9341 (SD, Update, Preferences sono nel core ESP32)
// Strumenti > Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
 
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
 
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define SD_CS    5
 
#define BTN_A    32
#define BTN_B    33
 
#define SCREEN_ROTATION 0
#define TFT_SPI_HZ 16000000
#define SD_SPI_HZ  10000000
 
// Finestra (ms) in cui si leggono A e B all'accensione
#define BOOT_KEY_WINDOW_MS 800
 
// Esiti di findGame()
#define FIND_ERROR  -1
#define FIND_NONE    0
#define FIND_OK      1
 
// Pannello nativo 320x240: con rotazione 0 la libreria crede di avere
// 240x320, quindi forziamo le dimensioni reali.
class EspBoyTFT : public Adafruit_ILI9341 {
 public:
  EspBoyTFT(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ILI9341(cs, dc, rst) {}
  void setLandscapeNative() {
    _width  = 320;
    _height = 240;
  }
};
 
EspBoyTFT tft(TFT_CS, TFT_DC, TFT_RST);
 
#define COLOR_BG   ILI9341_BLACK
#define COLOR_TXT  ILI9341_GREEN
#define COLOR_MSG  ILI9341_WHITE
#define COLOR_ERR  ILI9341_RED
 
// Partizioni: launcher = prima (ota_0, protetta), gioco = seconda (ota_1)
const esp_partition_t* launcherPart = NULL;
const esp_partition_t* gamePart     = NULL;
 
// ---------------------------------------------------------------
// Schermate
// ---------------------------------------------------------------
// Schermata info con il messaggio in basso e il suggerimento tasti
void drawBootScreen(const char* msg) {
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
 
  tft.setTextColor(COLOR_TXT);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("ESP-BOY");
 
  tft.setTextSize(1);
 
  const char* colA[] = {
    "buttons = {", "  \"up\":    Pin(27),", "  \"down\":  Pin(12),",
    "  \"right\": Pin(26),", "  \"left\":  Pin(14),", "  \"A\":     Pin(32),",
    "  \"B\":     Pin(33)", "}",
    "sd card reader = {", "  \"spi\": Pin(5)", "}",
    "piezo = {", "  \"PWM\": Pin(25)", "}"
  };
  const int colACount = sizeof(colA) / sizeof(colA[0]);
 
  const char* colB[] = {
    "display = {", "  \"cs\":   Pin(15),", "  \"dc\":   Pin(2),",
    "  \"rst\":  Pin(4),", "  \"sck\":  Pin(18),", "  \"mosi\": Pin(23),",
    "  \"miso\": Pin(19)", "  #rotation=270", "}"
  };
  const int colBCount = sizeof(colB) / sizeof(colB[0]);
 
  const int startY = 34;
  const int lineH  = 11;
  const int xA = 10;
  const int xB = 175;
 
  int y = startY;
  for (int i = 0; i < colACount; i++) { tft.setCursor(xA, y); tft.print(colA[i]); y += lineH; }
 
  y = startY;
  for (int i = 0; i < colBCount; i++) { tft.setCursor(xB, y); tft.print(colB[i]); y += lineH; }
 
  if (msg != nullptr) {
    tft.setTextColor(COLOR_MSG);
    tft.setCursor(10, 205);
    tft.print(msg);
  }
 
  // Suggerimento tasti all'avvio
  tft.setTextColor(COLOR_TXT);
  tft.setCursor(10, 224);
  tft.print("Tieni A: carica da SD   Tieni B: ultimo gioco");
}
 
// Schermata di errore (rosso) con messaggio e suggerimento
void showErrorHint(const String& msg, const char* hint) {
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
 
  tft.setTextColor(COLOR_ERR);
  tft.setTextSize(3);
  tft.setCursor(10, 40);
  tft.print("ERRORE");
 
  tft.setTextColor(COLOR_MSG);
  tft.setTextSize(1);
  tft.setCursor(10, 100);
  tft.print(msg);
 
  tft.setTextColor(COLOR_TXT);
  tft.setCursor(10, 130);
  tft.print(hint);
}
 
void showError(const String& msg) {
  showErrorHint(msg, "Spegni e riaccendi per riprovare");
}
 
const int BAR_X = 20;
const int BAR_Y = 140;
const int BAR_W = 280;
const int BAR_H = 20;
 
void drawProgressFrame(const String& name) {
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
 
  tft.setTextColor(COLOR_TXT);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("ESP-BOY");
 
  tft.setTextSize(2);
  tft.setCursor(10, 70);
  tft.print("Caricamento...");
 
  String shown = name;
  if (shown.length() > 40) shown = shown.substring(0, 40);
  tft.setTextColor(COLOR_MSG);
  tft.setTextSize(1);
  tft.setCursor(10, 105);
  tft.print(shown);
 
  tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_MSG);
}
 
void drawProgress(int pct) {
  int inner = BAR_W - 4;
  int filled = (inner * pct) / 100;
  tft.fillRect(BAR_X + 2, BAR_Y + 2, filled, BAR_H - 4, COLOR_TXT);
 
  tft.fillRect(130, 175, 70, 16, COLOR_BG);
  tft.setTextColor(COLOR_MSG);
  tft.setTextSize(2);
  tft.setCursor(130, 175);
  tft.print(pct);
  tft.print("%");
}
 
// ---------------------------------------------------------------
// Partizioni: il launcher deve stare nella prima, il gioco va nella seconda
// ---------------------------------------------------------------
bool initPartitions() {
  launcherPart = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  gamePart     = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
  if (launcherPart == NULL || gamePart == NULL) return false;
 
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next    = esp_ota_get_next_update_partition(NULL);
  if (running == NULL || next == NULL) return false;
 
  // Il launcher deve girare dalla prima partizione e la copia deve finire nella seconda
  if (running->address != launcherPart->address) return false;
  if (next->address != gamePart->address) return false;
 
  return true;
}
 
// ---------------------------------------------------------------
// Memoria dell'ultimo gioco caricato
// ---------------------------------------------------------------
void setLastGame(bool ok, const String& name) {
  Preferences p;
  p.begin("launcher", false);
  p.putBool("ok", ok);
  p.putString("name", name);
  p.end();
}
 
bool getLastGame(String& name) {
  Preferences p;
  p.begin("launcher", false);
  bool ok = p.getBool("ok", false);
  name = p.getString("name", "");
  p.end();
  if (!ok) return false;
 
  // Controllo extra: la seconda partizione deve iniziare con il byte magico di un'app
  uint8_t magic = 0;
  if (esp_partition_read(gamePart, 0, &magic, 1) != ESP_OK) return false;
  if (magic != 0xE9) return false;
 
  return true;
}
 
// ---------------------------------------------------------------
// Tasti all'avvio: 'A' = forza SD, 'B' = ultimo gioco, 0 = nessuno
// ---------------------------------------------------------------
char readBootKey() {
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
 
  bool a = false;
  bool b = false;
  unsigned long t0 = millis();
  while (millis() - t0 < BOOT_KEY_WINDOW_MS) {
    if (digitalRead(BTN_A) == LOW) a = true;
    if (digitalRead(BTN_B) == LOW) b = true;
    delay(5);
  }
 
  if (a) return 'A';
  if (b) return 'B';
  return 0;
}
 
// ---------------------------------------------------------------
// Ricerca del file .bin sulla SD
// ---------------------------------------------------------------
bool isCandidate(const String& name) {
  if (name.length() == 0 || name[0] == '.') return false;
 
  String low = name;
  low.toLowerCase();
  if (!low.endsWith(".bin")) return false;
 
  // scarta i binari che NON sono l'app: bootloader, tabella partizioni, merged
  if (low.indexOf("bootloader") >= 0) return false;
  if (low.indexOf("partitions") >= 0) return false;
  if (low.indexOf("merged") >= 0) return false;
 
  return true;
}
 
int findGame(String& path, size_t& size) {
  File root = SD.open("/");
  if (!root) return FIND_ERROR;
  if (!root.isDirectory()) {
    root.close();
    return FIND_ERROR;
  }
 
  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    if (name.startsWith("/")) name = name.substring(1);
 
    size_t sz = entry.size();
    bool dir = entry.isDirectory();
    entry.close();
 
    if (!dir && isCandidate(name)) {
      path = "/" + name;
      size = sz;
      root.close();
      return FIND_OK;
    }
    entry = root.openNextFile();
  }
  root.close();
  return FIND_NONE;
}
 
// ---------------------------------------------------------------
// Copia SD -> flash (solo seconda partizione)
// ---------------------------------------------------------------
String updateErrorText() {
  uint8_t e = Update.getError();
  if (e == UPDATE_ERROR_MAGIC_BYTE) return String("File non valido: non e' un'app compilata");
  if (e == UPDATE_ERROR_SPACE)      return String("File troppo grande per la flash");
  return String(Update.errorString());
}
 
bool copyToFlash(const String& path, size_t size, const String& name) {
  if (size == 0) {
    showError("File vuoto");
    return false;
  }
 
  File f = SD.open(path);
  if (!f) {
    showError("Impossibile leggere il file");
    return false;
  }
 
  // Da qui la seconda partizione verra' riscritta: l'ultimo gioco non e' piu' valido
  setLastGame(false, "");
 
  if (!Update.begin(size)) {
    String msg = updateErrorText();
    f.close();
    showError(msg);
    return false;
  }
 
  drawProgressFrame(name);
  drawProgress(0);
 
  static uint8_t buf[4096];
  size_t done = 0;
  int lastPct = 0;
 
  while (done < size) {
    size_t remaining = size - done;
    size_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
 
    size_t n = f.read(buf, toRead);
    if (n == 0) {
      Update.abort();
      f.close();
      showError("Lettura SD interrotta");
      return false;
    }
 
    if (Update.write(buf, n) != n) {
      String msg = updateErrorText();
      Update.abort();
      f.close();
      showError(msg);
      return false;
    }
 
    done += n;
 
    int pct = (int)((done * 100UL) / size);
    if (pct != lastPct) {
      drawProgress(pct);
      lastPct = pct;
    }
  }
 
  f.close();
 
  if (!Update.end(true)) {
    showError(updateErrorText());
    return false;
  }
 
  setLastGame(true, name);
  return true;
}
 
// ---------------------------------------------------------------
// Avvio: imposta la seconda partizione come boot e riavvia
// ---------------------------------------------------------------
bool bootGame() {
  if (gamePart == NULL) return false;
  if (esp_ota_set_boot_partition(gamePart) != ESP_OK) return false;
  ESP.restart();
  return true;
}
 
// ---------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------
void setup() {
  // Chip select alti prima di toccare il bus SPI condiviso
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
 
  bool sdOk = SD.begin(SD_CS, SPI, SD_SPI_HZ);
 
  tft.begin(TFT_SPI_HZ);
  tft.setRotation(SCREEN_ROTATION);
  tft.setLandscapeNative();
  tft.fillScreen(COLOR_BG);
 
  // Il launcher deve stare nella prima partizione: altrimenti non si copia nulla
  if (!initPartitions()) {
    showErrorHint("Launcher non nella prima partizione",
                  "Usa Minimal SPIFFS (1.9MB APP with OTA)");
    return;
  }
 
  drawBootScreen(nullptr);
  char key = readBootKey();
 
  // B = avvia l'ultimo gioco caricato, senza toccare la SD
  if (key == 'B') {
    String last;
    if (!getLastGame(last)) {
      drawBootScreen("Nessun gioco caricato");
      return;
    }
    drawBootScreen("Avvio ultimo gioco...");
    delay(300);
    if (!bootGame()) showError("Avvio fallito");
    return;
  }
 
  // A oppure nessun tasto = lettura SD e riscarica del .bin
  if (!sdOk) {
    drawBootScreen("SD assente o non leggibile");
    return;
  }
 
  String path;
  size_t size = 0;
  int found = findGame(path, size);
 
  if (found == FIND_ERROR) {
    drawBootScreen("Impossibile leggere la SD");
    return;
  }
 
  if (found == FIND_NONE) {
    drawBootScreen("Nessun file .bin sulla SD");
    return;
  }
 
  // Ad ogni accensione il file viene riscaricato da zero, anche se e' lo stesso
  String name = path.substring(1);
  if (!copyToFlash(path, size, name)) {
    return;
  }
 
  if (!bootGame()) {
    showError("Avvio fallito");
  }
}
 
void loop() {
}
 
Claude ha terminato la risposta
