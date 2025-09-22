#include <Arduino.h>
#include <Adafruit_Protomatter.h>
#include <Adafruit_GFX.h>
#include <WiFiNINA.h>
#include <WiFiClient.h>
#include <ArduinoHttpClient.h>
#include <PNGdec.h>
#include <AnimatedGIF.h>

#include "secrets.h"

#define UP_BUTTON_PIN   2  // Up button pin for Matrix Portal M4
#define DOWN_BUTTON_PIN 3  // Down button pin for Matrix Portal M4

float brightness = 1.0; // Full brightness
#define BRIGHTNESS_STEP 0.1
#define BRIGHTNESS_MIN 0.1
#define BRIGHTNESS_MAX 1.0

#define MATRIX_WIDTH 64
#define MATRIX_HEIGHT 32
#define MATRIX_BITDEPTH 6
uint8_t rgbPins[] = { 7, 8, 9, 10, 11, 12 };
uint8_t addrPins[] = { 17, 18, 19, 20 }; // A, B, C, D for 32-row panels
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;

Adafruit_Protomatter matrix(
  MATRIX_WIDTH, MATRIX_BITDEPTH, 1, rgbPins, 4, addrPins, clockPin, latchPin, oePin, false
);

WiFiClient wifi;
PNG png; // PNGdec object
AnimatedGIF gif;

// Buffer for image data
uint8_t *lastImageBuffer = nullptr;
int lastImageLength = 0;
bool imageLoaded = false;
volatile int gifFrameDelay = 0;

enum ImageType { IMAGE_UNKNOWN, IMAGE_PNG, IMAGE_GIF };
ImageType detectImageType(const uint8_t *buf, int len) {
  if (len >= 4 && buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47)
    return IMAGE_PNG;
  if (len >= 6 && buf[0] == 0x47 && buf[1] == 0x49 && buf[2] == 0x46 && buf[3] == 0x38)
    return IMAGE_GIF;
  return IMAGE_UNKNOWN;
}

// Draw callback for PNGdec
int PNGDraw(PNGDRAW *pDraw) {
  uint8_t *p = pDraw->pPixels;
  for (int i = 0; i < pDraw->iWidth; i++) {
    uint8_t r = uint8_t(*p++ * brightness);
    uint8_t g = uint8_t(*p++ * brightness);
    uint8_t b = uint8_t(*p++ * brightness);
    if (i < MATRIX_WIDTH && pDraw->y < MATRIX_HEIGHT)
      matrix.drawPixel(i, pDraw->y, matrix.color565(r, g, b));
  }
  return 1;
}

// Draw callback for AnimatedGIF
void GIFDraw(GIFDRAW *pDraw) {
  uint16_t bgColor = matrix.color565(0, 0, 0); // background (black)
  int x0 = pDraw->iX;
  int w = pDraw->iWidth;
  
  if (pDraw->ucPaletteType == 1) { // Palette-based
    for (int i = 0; i < pDraw->iCanvasWidth; i++) {
      uint16_t color = bgColor;
      if (i >= pDraw->iX && i < pDraw->iX + pDraw->iWidth) {
        int idx = pDraw->pPixels[i - pDraw->iX]; // Correct index into palette
        uint8_t r = pDraw->pPalette[idx * 3 + 0];
        uint8_t g = pDraw->pPalette[idx * 3 + 1];
        uint8_t b = pDraw->pPalette[idx * 3 + 2];
        color = matrix.color565(r, g, b);
      }
      matrix.drawPixel(i, pDraw->y, color);
    }
  } else { // RGB
    uint8_t *p = pDraw->pPixels;
    for (int i = 0; i < pDraw->iCanvasWidth; i++) {
      uint16_t color = bgColor;
      if (i >= x0 && i < x0 + w) {
        // Only fill pixels within dirty rectangle
        uint8_t r = *p++;
        uint8_t g = *p++;
        uint8_t b = *p++;
        color = matrix.color565(r, g, b);
      }
      matrix.drawPixel(i, pDraw->y, color);
    }
  }

  Serial.print("Expected pixel data bytes: "); Serial.println(w * 3);

  Serial.print("GIF line y: "); Serial.print(pDraw->y);
  Serial.print(" iX="); Serial.print(pDraw->iX);
  Serial.print(" width: "); Serial.print(pDraw->iWidth);
  Serial.print(" paletteType="); Serial.print(pDraw->ucPaletteType);
  Serial.print(" iCanvasWidth="); Serial.println(pDraw->iCanvasWidth);

  uint8_t *start = pDraw->pPixels;
  uint8_t *end = pDraw->pPixels + w * 3;
  Serial.print("First pixel: ");
  Serial.print(start[0]); Serial.print(",");
  Serial.print(start[1]); Serial.print(",");
  Serial.print(start[2]); Serial.println();
  Serial.print("Last pixel: ");
  Serial.print(end[-3]); Serial.print(",");
  Serial.print(end[-2]); Serial.print(",");
  Serial.print(end[-1]); Serial.println();
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Panel init
  ProtomatterStatus status = matrix.begin();
  if (status != PROTOMATTER_OK) {
    Serial.println("Matrix init failed!");
    while (1);
  }
  matrix.fillScreen(matrix.color565(0,0,0));
  matrix.show();

  // Set buttons to brightness control
  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("Button pins initialized.");

  // WiFi
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    while (1);
  }
  while (WiFi.begin(SSID, PASS) != WL_CONNECTED) {
    Serial.println("Connecting...");
    delay(1000);
  }
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
}
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 15000; // 15 seconds

void fetchAndDrawImage() {
  // Parse FEED_URL into host and path
  String url = FEED_URL;
  url.replace("http://", "");
  int slashIdx = url.indexOf('/');
  int colonIdx = url.indexOf(":");
  String host = url.substring(0, colonIdx);
  String path = url.substring(slashIdx);

  Serial.println(host);
  Serial.println(path);

  HttpClient client(wifi, host.c_str(), FEED_PORT); 
  client.get(path.c_str());

  int statusCode = client.responseStatusCode();
  if (statusCode == 200) {
    int contentLength = client.contentLength();
    Serial.print("Content-Length: ");
    Serial.println(contentLength);

    uint8_t *buf = (uint8_t *)malloc(contentLength);
    if (!buf) {
      Serial.println("Out of memory!");
      client.stop();
      return;
    }
    int pos = 0;
    unsigned long lastRead = millis();
    const unsigned long timeout = 10000; // 10 seconds
    while (pos < contentLength) {
      if (millis() - lastRead > timeout) {
        Serial.println("Timeout during download!");
        break;
      }
      int available = client.available();
      if (available > 0) {
        int remaining = contentLength - pos;
        int toRead = min(1024, remaining);
        int actuallyRead = client.read(&buf[pos], toRead);
        if (actuallyRead > 0) {
          pos += actuallyRead;
          lastRead = millis(); // reset timer
        }
      } else {
        delay(1);
      }
    }

    if (pos < contentLength) {
      Serial.printf("Image download incomplete! Only %d of %d bytes read.\n", pos, contentLength);
      free(buf);
      client.stop();
      return;
    }
    Serial.println("Image downloaded.");

    // Free previous buffer if needed
    if (lastImageBuffer) free(lastImageBuffer);

    lastImageBuffer = buf;
    lastImageLength = contentLength;
    imageLoaded = true;

    ImageType imgType = detectImageType(lastImageBuffer, lastImageLength);

    matrix.fillScreen(matrix.color565(0,0,0));
    matrix.show();

    if (imgType == IMAGE_PNG) {
      int rc = png.openRAM(lastImageBuffer, lastImageLength, PNGDraw);
      if (rc == PNG_SUCCESS) {
        Serial.println("PNG loaded, drawing...");
        png.decode(NULL, 0);
        matrix.show();
      } else {
        Serial.print("PNGdec error: "); Serial.println(rc);
      }
    } else if (imgType == IMAGE_GIF) {
      int rc = gif.open((uint8_t *)lastImageBuffer, lastImageLength, GIFDraw);
      if (rc == 1) {
        Serial.println("GIF loaded, drawing...");
        int delay_ms;
        do {
          matrix.show();
          delay_ms = gif.playFrame(true, NULL); // returns delay for this frame
          if (delay_ms > 0)
            delay(delay_ms);
        } while (delay_ms > 0);
      } else {
        Serial.print("GIF error: "); Serial.println(rc);
      }
      imageLoaded = true;
    } else {
      Serial.println("Unknown image format.");
    }
  } else {
    Serial.print("HTTP error: "); Serial.println(statusCode);
  }
  client.stop();
}

// void printButtonStates() {
//   Serial.print("UP raw: "); Serial.println(digitalRead(UP_BUTTON_PIN));
//   Serial.print("DOWN raw: "); Serial.println(digitalRead(DOWN_BUTTON_PIN));
// }

void loop() {
  // printButtonStates();

  static bool upPrev = HIGH, downPrev = HIGH;
  bool upNow = digitalRead(UP_BUTTON_PIN);
  bool downNow = digitalRead(DOWN_BUTTON_PIN);
  bool redraw = false;

  if (upPrev == HIGH && upNow == LOW) {
    brightness += BRIGHTNESS_STEP;
    if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
    Serial.print("Brightness up: "); Serial.println(brightness, 2);
    redraw = true;
  }
  if (downPrev == HIGH && downNow == LOW) {
    brightness -= BRIGHTNESS_STEP;
    if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
    Serial.print("Brightness down: "); Serial.println(brightness, 2);
    redraw = true;
  }
  upPrev = upNow;
  downPrev = downNow;

  if (redraw && imageLoaded) {
    matrix.fillScreen(matrix.color565(0,0,0));
    matrix.show();
    int rc = png.openRAM(lastImageBuffer, lastImageLength, PNGDraw);
    if (rc == PNG_SUCCESS) {
      png.decode(NULL, 0);
      matrix.show();
    } else {
      Serial.print("PNGdec error on redraw: "); Serial.println(rc);
    }
  }

  if (millis() - lastFetchTime > fetchInterval) {
    lastFetchTime = millis();
    fetchAndDrawImage();
  }

  delay(20);
}