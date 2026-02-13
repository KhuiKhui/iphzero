#include <lvgl.h>
#include "ui.h"
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <JPEGDEC.h>
#include "MjpegClass.h"
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include "env.h"

void badApple(lv_event_t * e);

JPEGDEC jpeg;
TFT_eSPI tft = TFT_eSPI();

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 13  // T_DIN
#define XPT2046_MISO 12  // T_OUT
#define XPT2046_CLK 14   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define TFT_HOR_RES 320
#define TFT_VER_RES 480
#define FONT_SIZE 2

const char *MJPEG_FOLDER = "/"; // Name of the mjpeg folder on the SD Card
#define MAX_FILES 5 // Maximum number of files, adjust as needed
String mjpegFileList[MAX_FILES];
uint32_t mjpegFileSizes[MAX_FILES] = {0}; // Store each GIF file's size in bytes
int mjpegCount = 0;
static int currentMjpegIndex = 0;
static File mjpegFile;

I2SStream i2s;
EncodedAudioStream decoder(&i2s, new MP3DecoderHelix());
StreamCopy copier; 
File audioFile;

// Global variables for mjpeg
MjpegClass mjpeg;
int total_frames;
unsigned long total_read_video;
unsigned long total_decode_video;
unsigned long total_show_video;
unsigned long start_ms, curr_ms;
long output_buf_size, estimateBufferSize;
uint8_t *mjpeg_buf;
uint16_t *output_buf;

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

bool playBadApple = false;

File myfile;

void * myOpen(const char *filename, int32_t *size) {
  myfile = SD.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void *handle) {
  if (myfile) myfile.close();
}
int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(JPEGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

int JPEGDraw(JPEGDRAW *pDraw) {
    // pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
    for (int i=0; i<pDraw->iWidth*pDraw->iHeight; i++) {
     pDraw->pPixels[i] = __builtin_bswap16(pDraw->pPixels[i]);
    }
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  
  return 1;
}




void *draw_buf_1;
unsigned long lastTickMillis = 0;

static lv_display_t *disp;

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))

// Display flush callback
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels((uint16_t *)px_map, w * h);
  tft.endWrite();
  
  lv_disp_flush_ready(disp);
}

void printTouchToSerial(int touchX, int touchY, int touchZ) {
  Serial.print("X = ");
  Serial.print(touchX);
  Serial.print(" | Y = ");
  Serial.print(touchY);
  Serial.print(" | Pressure = ");
  Serial.print(touchZ);
  Serial.println();
}

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    
    data->point.x = map(p.x, 200, 3700, 0, TFT_HOR_RES);
    data->point.y = map(p.y, 240, 3800, 0, TFT_VER_RES);
    data->state = LV_INDEV_STATE_PRESSED;
  }
}

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 8;
const int   daylightOffset_sec = 3600;

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();
}

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();
}

void badApple(lv_event_t * e){
  if (draw_buf_1) {
    free(draw_buf_1);
    draw_buf_1 = NULL;
    Serial.println("LVGL buffer freed");
  }
  Serial.println("OK");
  playBadApple = true;
    tft.init();
    
    output_buf_size = tft.width() * 4 * 2;
    output_buf = (uint16_t *)heap_caps_aligned_alloc(16, output_buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!output_buf)
    {
        Serial.println("output_buf aligned_alloc failed!");
        while (true)
        {
        }
    }
    estimateBufferSize = tft.width() * tft.height() * 2 / 5;
    mjpeg_buf = (uint8_t *)heap_caps_malloc(estimateBufferSize, MALLOC_CAP_8BIT);
    if (!mjpeg_buf)
    {
        Serial.println("mjpeg_buf allocation failed!");
        while (true)
        {
        }
    }

    SD.begin(5);

    loadMjpegFilesList();
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);

  

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);


  
  
  // AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

  // audioFile = SD.open("/badaudio.mp3");

  // auto config = i2s.defaultConfig(TX_MODE);
  // config.pin_bck = 26;    // Bit Clock
  // config.pin_ws = 25;     // Word Select (LR Clock) 
  // config.pin_data = 4;    // Data Out (to speaker/amp)
  // config.sample_rate = 44100;
  // config.bits_per_sample = 16;
  // config.channels = 2;
  // i2s.begin(config);
  // decoder.begin();
  // copier.begin(decoder, audioFile);



  lv_init();
  
  draw_buf_1 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf_1, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
  
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);
  
  ui_init();


  WiFi.begin(NW_SSID, NW_PW);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  
}



void loop(){
    unsigned int tickPeriod = millis() - lastTickMillis;
    lv_tick_inc(tickPeriod);
    lastTickMillis = millis();

    // LVGL Task Handler
    lv_task_handler();  

    // playBadApple = true;
    

    if (playBadApple){
      Serial.println("PLAY TRUE");
      playSelectedMjpeg(currentMjpegIndex);
      currentMjpegIndex++;
      if (currentMjpegIndex >= mjpegCount)
      {
          currentMjpegIndex = 0;
          playBadApple = false;
      }
    }

    

    //copier.copy();

}

void playSelectedMjpeg(int mjpegIndex){
    String fullPath = String(MJPEG_FOLDER) + mjpegFileList[mjpegIndex];
    char mjpegFilename[128];
    fullPath.toCharArray(mjpegFilename, sizeof(mjpegFilename));
    mjpegPlayFromSDCard(mjpegFilename);
}

int jpegDrawCallback(JPEGDRAW *pDraw)
{
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
    return 1;
}

void mjpegPlayFromSDCard(char *mjpegFilename){
    File mjpegFile = SD.open(mjpegFilename, "r");

    if (!mjpegFile || mjpegFile.isDirectory())
    {
        Serial.printf("ERROR: Failed to open %s file for reading\n", mjpegFilename);
    }
    else
    {
        tft.fillScreen(TFT_BLACK);

        start_ms = millis();
        curr_ms = millis();
        total_frames = 0;
        total_read_video = 0;
        total_decode_video = 0;
        total_show_video = 0;

        mjpeg.setup(
            &mjpegFile, mjpeg_buf, jpegDrawCallback, true /* useBigEndian */,
            0 /* x */, 0 /* y */, tft.width() /* widthLimit */, tft.height() /* heightLimit */);

        while (mjpegFile.available() && mjpeg.readMjpegBuf())
        {
            mjpeg.drawJpg();
        }
        mjpegFile.close();
    }
}

void loadMjpegFilesList(){
    File mjpegDir = SD.open(MJPEG_FOLDER);
    if (!mjpegDir)
    {
        Serial.printf("Failed to open %s folder\n", MJPEG_FOLDER);
        while (true)
        {
            /* code */
        }
    }
    mjpegCount = 0;
    while (true)
    {
        File file = mjpegDir.openNextFile();
        if (!file)
            break;
        if (!file.isDirectory())
        {
            String name = file.name();
            if (name.endsWith(".avi"))
            {
                mjpegFileList[mjpegCount] = name;
                mjpegFileSizes[mjpegCount] = file.size(); // Save file size (in bytes)
                mjpegCount++;
                if (mjpegCount >= MAX_FILES)
                    break;
            }
        }
        file.close();
    }
    mjpegDir.close();
    Serial.printf("%d mjpeg files read\n", mjpegCount);
    for (int i = 0; i < mjpegCount; i++)
    {
        Serial.printf("File %d: %s, Size: %lu bytes (%s)\n", i, mjpegFileList[i].c_str(), mjpegFileSizes[i],formatBytes(mjpegFileSizes[i]).c_str());
    }
}

// Function helper display sizes on the serial monitor
String formatBytes(size_t bytes){
    if (bytes < 1024)
    {
        return String(bytes) + " B";
    }
    else if (bytes < (1024 * 1024))
    {
        return String(bytes / 1024.0, 2) + " KB";
    }
    else
    {
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    }
}

void ui_init_test() {
  static lv_style_t style_base;
  lv_style_init(&style_base);
  lv_style_set_border_width(&style_base, 0);

  lv_obj_t *screen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(screen, TFT_HOR_RES, TFT_VER_RES);
  lv_obj_center(screen);
  lv_obj_add_style(screen, &style_base, LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *user_label = lv_label_create(screen);
  lv_label_set_text(user_label, "Windows Insider");
  lv_obj_set_style_text_color(user_label, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_set_style_text_font(user_label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_align(user_label, LV_ALIGN_CENTER, 0, -30);

  static lv_style_t style_textarea;
  lv_style_init(&style_textarea);
  lv_style_set_border_width(&style_textarea, 2);
  lv_style_set_border_color(&style_textarea, lv_color_hex(0xffffff));
  lv_style_set_radius(&style_textarea, 0);
  lv_style_set_bg_color(&style_textarea, lv_color_hex(0x000000));
  lv_style_set_bg_opa(&style_textarea, LV_OPA_70);

  lv_obj_t *user_password = lv_textarea_create(screen);
  lv_obj_align(user_password, LV_ALIGN_CENTER, 0, 20);
  lv_textarea_set_placeholder_text(user_password, "PIN");
  lv_obj_set_size(user_password, 200, 40);
  lv_obj_add_style(user_password, &style_textarea, LV_PART_MAIN);

  static lv_style_t style_panel;
  lv_style_init(&style_panel);
  lv_style_set_border_width(&style_panel, 0);
  lv_style_set_bg_opa(&style_panel, LV_OPA_TRANSP);
  lv_style_set_pad_all(&style_panel, 0);

  lv_obj_t *menu_panel = lv_obj_create(screen);
  lv_obj_set_size(menu_panel, 140, 70);
  lv_obj_align_to(menu_panel, user_password, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_style(menu_panel, &style_panel, LV_PART_MAIN);

  lv_obj_t *sign_in_label = lv_label_create(menu_panel);
  lv_label_set_text(sign_in_label, "Sign-in options");
  lv_obj_set_style_text_color(sign_in_label, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(sign_in_label, LV_ALIGN_TOP_MID, 0, 0);

}