// ESP-BOY DEV - boot: init SD + schermata info se SD assente/vuota
// Librerie: Adafruit GFX Library, Adafruit ILI9341 (SD e' nel core ESP32)

#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define SD_CS    5

// Il tuo pannello e' nativamente 320x240 (landscape): con rotazione 0 la
// libreria pero' crede di avere 240x320, quindi scriveva solo 240 colonne
// (fascia di rumore a destra) e Y oltre 240 andava a capo (messaggio nel
// punto sbagliato). Questa classe forza le dimensioni reali 320x240.
class EspBoyTFT : public Adafruit_ILI9341 {
 public:
  EspBoyTFT(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ILI9341(cs, dc, rst) {}
  void setLandscapeNative() {
    _width  = 320;
    _height = 240;
  }
};

#define SCREEN_ROTATION 0
#define TFT_SPI_HZ 16000000

EspBoyTFT tft(TFT_CS, TFT_DC, TFT_RST);

#define COLOR_BG     ILI9341_BLACK
#define COLOR_TXT    ILI9341_GREEN
#define COLOR_MSG    ILI9341_WHITE

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

String findExecutable() {
  File root = SD.open("/");
  if (!root) return "";

  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    if (!entry.isDirectory() && name.endsWith(".bin")) {
      entry.close();
      root.close();
      return "/" + name;
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return "";
}

void runExecutable(const String& path) {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TXT);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.print("Avvio:");
  tft.setCursor(20, 50);
  tft.setTextSize(1);
  tft.print(path);
}

void setup() {
  // Chip select alti prima di toccare il bus SPI condiviso
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  bool sdOk = SD.begin(SD_CS);

  tft.begin(TFT_SPI_HZ);
  tft.setRotation(SCREEN_ROTATION);
  tft.setLandscapeNative();
  tft.fillScreen(COLOR_BG);

  if (!sdOk) {
    drawBootScreen("SD assente");
    return;
  }

  String exe = findExecutable();
  if (exe == "") {
    drawBootScreen("SD vuota: nessun eseguibile");
    return;
  }

  runExecutable(exe);
}

void loop() {
}