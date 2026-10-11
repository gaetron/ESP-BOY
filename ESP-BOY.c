// ESP-BOY LAUNCHER - lancia il .bin presente sulla SD, senza menu
// Librerie: Adafruit GFX Library, Adafruit ILI9341 (SD e Update sono nel core ESP32)
// Strumenti > Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
// Il launcher si carica UNA volta via USB. I giochi vanno compilati con lo stesso schema.

#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define SD_CS    5

#define SCREEN_ROTATION 0
#define TFT_SPI_HZ 16000000
#define SD_SPI_HZ  10000000

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

// Tipo di ritorno di copyToFlash(): definito PRIMA delle funzioni,
// altrimenti i prototipi generati dall'IDE non lo conoscono.
enum CopyResult { COPY_OK, COPY_NO_MARKER, COPY_FAILED };

// ---------------------------------------------------------------
// Schermate
// ---------------------------------------------------------------
void drawBootScreen(const char* msg) {
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);

  tft.setTextColor(COLOR_TXT);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("ESP-BOY DEV");

  tft.setTextSize(1);

  const char* colA[] = {
    "buttons = {", "  \"up\":    Pin(27),", "  \"down\":  Pin(12),",
    "  \"right\": Pin(26),", "  \"left\":  Pin(14),", "  \"A\":     Pin(32),",
    "  \"B\":     Pin(33)", "}",
    "sd card reader = {", "  \"spi\": Pin(5)", "}",
    "piezo = {", "  \"PWM\": Pin(7)", "}"
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
    tft.setCursor(10, 215);
    tft.print(msg);
  }
}

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
// Ricerca del gioco sulla SD
// ---------------------------------------------------------------
bool isCandidate(const String& name, size_t sz) {
  if (name.length() == 0 || name[0] == '.') return false;

  String low = name;
  low.toLowerCase();
  if (!low.endsWith(".bin")) return false;

  // scarta i binari che NON sono l'app: bootloader, tabella partizioni, merged
  if (low.indexOf("bootloader") >= 0) return false;
  if (low.indexOf("partitions") >= 0) return false;
  if (low.indexOf("merged") >= 0) return false;

  if (sz < 32768) return false;
  return true;
}

bool findGame(String& path, size_t& size, String& sig) {
  File root = SD.open("/");
  if (!root) return false;

  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    if (name.startsWith("/")) name = name.substring(1);

    size_t sz = entry.size();
    bool dir = entry.isDirectory();
    unsigned long lw = (unsigned long)entry.getLastWrite();
    entry.close();

    if (!dir && isCandidate(name, sz)) {
      path = "/" + name;
      size = sz;
      sig = name + "|" + String((unsigned long)sz) + "|" + String(lw);
      root.close();
      return true;
    }
    entry = root.openNextFile();
  }
  root.close();
  return false;
}

// ---------------------------------------------------------------
// Copia SD -> flash (partizione OTA libera)
// ---------------------------------------------------------------
String updateErrorText() {
  uint8_t e = Update.getError();
  if (e == UPDATE_ERROR_MAGIC_BYTE) return String("File non valido: non e' un'app compilata");
  if (e == UPDATE_ERROR_SPACE)      return String("Gioco troppo grande per la flash");
  return String(Update.errorString());
}

// Marcatore che ogni gioco deve contenere (vedi blocco "ritorno al launcher").
// Se manca, il gioco non rimetterebbe il launcher come avvio e resterebbe
// bloccato: in quel caso il launcher lo rifiuta senza mai impostarlo come boot.
static const char MARK[] = "ESPBOY_RETURN_V1";
static const size_t MARK_LEN = sizeof(MARK) - 1;
static const size_t CHUNK = 4096;

CopyResult copyToFlash(const String& path, size_t size, const String& name) {
  File f = SD.open(path);
  if (!f) {
    showError("Impossibile leggere il file");
    return COPY_FAILED;
  }

  if (!Update.begin(size)) {
    String msg = updateErrorText();
    f.close();
    showError(msg);
    return COPY_FAILED;
  }

  drawProgressFrame(name);
  drawProgress(0);

  // I primi (MARK_LEN - 1) byte del buffer conservano la coda del blocco
  // precedente, cosi' il marcatore viene trovato anche a cavallo di due blocchi.
  const size_t P = MARK_LEN - 1;
  static uint8_t buf[MARK_LEN - 1 + CHUNK];
  memset(buf, 0, sizeof(buf));

  size_t done = 0;
  int lastPct = 0;
  bool found = false;

  while (done < size) {
    size_t remaining = size - done;
    size_t toRead = remaining < CHUNK ? remaining : CHUNK;

    size_t n = f.read(buf + P, toRead);
    if (n == 0) {
      Update.abort();
      f.close();
      showError("Lettura SD interrotta");
      return COPY_FAILED;
    }

    if (Update.write(buf + P, n) != n) {
      String msg = updateErrorText();
      Update.abort();
      f.close();
      showError(msg);
      return COPY_FAILED;
    }

    if (!found) {
      size_t total = P + n;
      for (size_t i = 0; i + MARK_LEN <= total; i++) {
        if (memcmp(buf + i, MARK, MARK_LEN) == 0) {
          found = true;
          break;
        }
      }
    }
    memmove(buf, buf + n, P);

    done += n;

    int pct = (int)((done * 100UL) / size);
    if (pct != lastPct) {
      drawProgress(pct);
      lastPct = pct;
    }
  }

  f.close();

  // Senza marcatore NON si conferma l'aggiornamento: la partizione di boot
  // resta quella del launcher.
  if (!found) {
    Update.abort();
    return COPY_NO_MARKER;
  }

  if (!Update.end(true)) {
    showError(updateErrorText());
    return COPY_FAILED;
  }

  return COPY_OK;
}

// ---------------------------------------------------------------
// Avvio del gioco: imposta l'altra partizione come boot e riavvia
// ---------------------------------------------------------------
bool bootGame() {
  const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
  if (other == NULL) return false;
  if (esp_ota_set_boot_partition(other) != ESP_OK) return false;
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

  // Protezione: con uno schema di partizioni senza OTA la copia andrebbe a
  // sovrascrivere il launcher stesso. In quel caso non si copia nulla.
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* other   = esp_ota_get_next_update_partition(NULL);
  if (running == NULL || other == NULL || other->address == running->address) {
    showErrorHint("Schema partizioni senza OTA",
                  "Usa Minimal SPIFFS (1.9MB APP with OTA)");
    return;
  }

  if (!sdOk) {
    drawBootScreen("SD assente");
    return;
  }

  String path;
  String sig;
  size_t size = 0;

  if (!findGame(path, size, sig)) {
    drawBootScreen("SD vuota: nessun eseguibile");
    return;
  }

  // Ad ogni accensione il gioco viene riscaricato da zero, anche se e' lo stesso
  String name = path.substring(1);
  CopyResult r = copyToFlash(path, size, name);

  if (r == COPY_NO_MARKER) {
    showErrorHint("Manca ESPBOY_RETURN_V1 nel gioco",
                  "Aggiungi il blocco di ritorno e ricompila");
    return;
  }

  if (r == COPY_FAILED) {
    return;
  }

  if (!bootGame()) {
    showError("Avvio del gioco fallito");
  }
}

void loop() {
}
