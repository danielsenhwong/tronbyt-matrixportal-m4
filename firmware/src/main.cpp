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

ImageType lastImageType = IMAGE_UNKNOWN;

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

// Efficient horizontal span copy, adapted from Adafruit's example
void span(uint16_t *src, int16_t x, int16_t y, int16_t width) {
  if (x >= matrix.width()) return;
  int16_t x2 = x + width - 1;
  if (x2 < 0) return;
  if (x < 0) {
    width += x;
    src -= x;
    x = 0;
  }
  if (x2 >= matrix.width()) {
    width -= (x2 - matrix.width() + 1);
  }
  if(matrix.getRotation() == 0) {
    memcpy(matrix.getBuffer() + y * matrix.width() + x, src, width * 2);
  } else {
    while(x <= x2) {
      matrix.drawPixel(x++, y, *src++);
    }
  }
}

// Adafruit-style AnimatedGIF draw callback
uint8_t clampToByte(int value) {
  return (value < 0) ? 0 : (value > 255 ? 255 : value);
}

void GIFDraw(GIFDRAW *pDraw) {
  int y = pDraw->y;
  int x0 = pDraw->iX;
  int w = pDraw->iWidth;
  uint16_t usTemp[MATRIX_WIDTH];
  uint16_t *usPalette = pDraw->pPalette;
  uint8_t *s = pDraw->pPixels;

  // Palette-based GIFs (most common for web GIFs!)
  if (pDraw->ucPaletteType == 0) {
    if (pDraw->ucHasTransparency) {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x = 0, iCount = 0;
      pEnd = s + w;
      while (x < w) {
        c = ucTransparent - 1;
        uint16_t *d = usTemp;
        while (c != ucTransparent && s < pEnd) {
          c = *s++;
          if (c == ucTransparent) {
            s--;
          } else {
            // *d++ = usPalette[c];
            // incorporate brightness adjustment
            // uint8_t idx = c;
            uint8_t r = uint8_t(pDraw->pPalette[c * 3 + 0] * brightness);
            uint8_t g = uint8_t(pDraw->pPalette[c * 3 + 1] * brightness);
            uint8_t b = uint8_t(pDraw->pPalette[c * 3 + 2] * brightness);
            *d++ = matrix.color565(r, g, b);
            iCount++;
          }
        }
        if (iCount) {
          span(usTemp, x0 + x, y, iCount);
          x += iCount;
          iCount = 0;
        }
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd) {
          c = *s++;
          if (c == ucTransparent) iCount++;
          else s--;
        }
        if (iCount) {
          x += iCount;
          iCount = 0;
        }
      }
    } else {
      s = pDraw->pPixels;
      for (int x = 0; x < w; x++) {
        // usTemp[x] = usPalette[*s++];
        // incorporate brightness adjustment
        uint8_t idx = *s++;
        uint16_t color = pDraw->pPalette[idx]; // palette entry in RGB565

        // Extract R, G, B from RGB565
        uint8_t r = ((color >> 11) & 0x1F) << 3; // 5 bits, scaled to 8
        uint8_t g = ((color >> 5) & 0x3F) << 2;  // 6 bits, scaled to 8
        uint8_t b = (color & 0x1F) << 3;         // 5 bits, scaled to 8

        // Apply brightness and clamp
        r = clampToByte(int(r * brightness));
        g = clampToByte(int(g * brightness));
        b = clampToByte(int(b * brightness));

        // Convert back to RGB565 for display
        usTemp[x] = matrix.color565(r, g, b);

        // uint8_t r = uint8_t(pDraw->pPalette[idx * 3 + 0] * brightness);
        // uint8_t g = uint8_t(pDraw->pPalette[idx * 3 + 1] * brightness);
        // uint8_t b = uint8_t(pDraw->pPalette[idx * 3 + 2] * brightness);
        // usTemp[x] = matrix.color565(r, g, b);
      }
      span(usTemp, x0, y, w);
    }
  }
  // Truecolor GIFs (paletteType==0): RGB bytes
  else {
    s = pDraw->pPixels;
    for (int x = 0; x < w; x++) {
      uint8_t r = *s++;
      uint8_t g = *s++;
      uint8_t b = *s++;
      usTemp[x] = matrix.color565(r, g, b);
    }
    span(usTemp, x0, y, w);
  }
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
    const unsigned long timeout = 5000; // 5 seconds
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
    lastImageType = imgType;

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
    if (lastImageType == IMAGE_PNG) {
      int rc = png.openRAM(lastImageBuffer, lastImageLength, PNGDraw);
      if (rc == PNG_SUCCESS) {
        png.decode(NULL, 0);
        matrix.show();
      } else {
        Serial.print("PNGdec error on redraw: "); Serial.println(rc);
      }
    } else if (lastImageType == IMAGE_GIF) {
      int rc = gif.open((uint8_t *)lastImageBuffer, lastImageLength, GIFDraw);
      if (rc == 1) {
        int delay_ms;
        do {
          matrix.show();
          delay_ms = gif.playFrame(true, NULL);
          if (delay_ms > 0)
            delay(delay_ms);
        } while (delay_ms > 0);
      } else {
        Serial.print("GIF error on redraw: "); Serial.println(rc);
      }
    }
  }

  if (millis() - lastFetchTime > fetchInterval) {
    lastFetchTime = millis();
    fetchAndDrawImage();
  }

  delay(20);
}