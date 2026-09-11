#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>
#include <sys/time.h>

#include "LGFX_Driver.h"

// -> settings_task_esp
    TaskHandle_t gui_task;
    SemaphoreHandle_t gui_semaphore;
// -> settings_task_esp

// -> settings_time
  String initial_time() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "0000-00-00 00:00:00";
    }

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
  }

  void initial_timezone() {
    setenv("TZ", "UTC-5", 1);
    tzset();
  }
// -> settings_time

// -> UART

  #define RX_PIN 22
  #define TX_PIN 27
  #define BAUD_RATE 115200

  volatile float temp = 0.0;
  volatile bool flag_read = false;
  hw_timer_t* timer_data = NULL;

  void IRAM_ATTR change_flag() {
    flag_read = true;
  }

  void initial_timer_read() {
    timer_data = timerBegin(1000000);
    timerAttachInterrupt(timer_data, &change_flag);
    timerAlarm(timer_data, 10000, true, 0);
    timerStart(timer_data);
  }


  void initial_port() {
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(500);
  }

// -> UART

// -> server
  AsyncWebServer server(80); 

  IPAddress ip(192, 168, 2, 1);
  IPAddress geteway(192, 168, 2, 1);
  IPAddress subnet(255, 255, 255, 0);

  const char* ssid = "TorexLoggerOven";
  const char* password = "1234567890";


  void initial_wifi_ap() {
    WiFi.softAP(ssid, password);
    WiFi.softAPConfig(ip, geteway, subnet);
    delay(500);
  }

  void start_server() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request -> send(LittleFS, "/index.html", "text/html");
    });

    server.on("/get_temp", HTTP_GET, [](AsyncWebServerRequest* request) {
      request -> send(200, "text/html", String(temp));
    });

    server.on("/game", HTTP_GET, [](AsyncWebServerRequest* request) {
        request -> send(LittleFS, "/game.html", "text/html");
    });

    server.on("/get_time", HTTP_GET, [](AsyncWebServerRequest* request) {
      if (!request -> hasParam("timestamp")) {
        request -> send(400, "text/plain", "Missing timestamp");
        return;
      }

      String timestamp_str = request -> getParam("timestamp") -> value();
      time_t timestamp = timestamp_str.toInt();

      struct timeval tv;

      tv.tv_sec = timestamp;
      tv.tv_usec = 0;

      settimeofday(&tv, nullptr);

      request -> send(200, "text/plain", "Time synchronized");
    });
    server.begin();
  }
// -> server

// -> GUI
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf[320*10];
    static lv_disp_drv_t disp_drv;

    static lv_indev_drv_t indev_drv;

    lv_obj_t* screen_temp;
    lv_obj_t* screen_settings;

    void my_flush_disp(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
        uint32_t width = area -> x2 - area -> x1 + 1;
        uint32_t height = area -> y2 - area -> y1 + 1;
        lcd.startWrite();
        lcd.setAddrWindow(area -> x1, area -> y1, width, height);
        lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(color_p), width * height);
        lcd.endWrite();
        lv_disp_flush_ready(disp);
    }

    void my_touch_read(lv_indev_drv_t* indev, lv_indev_data_t* data) {
        uint16_t x;
        uint16_t y;

        if (lcd.getTouch(&x, &y)) {
            x = 319 - x;
            data->state = LV_INDEV_STATE_PRESSED;
            data -> point.x = x;
            data -> point.y = y;
        } else {
            data -> state = LV_INDEV_STATE_RELEASED;
        }
    }

    lv_obj_t* date_lbl;
    lv_obj_t* time_lbl;

    lv_obj_t* indicator;
    lv_obj_t* active_status_lbl;

    lv_obj_t* lbl_period_value;
    lv_obj_t* lbl_value;

    lv_obj_t* chart_temp;
    lv_chart_series_t* series_temp;

    lv_obj_t* btn_start;
    lv_obj_t* btn_stop;

    bool state_exp = false;
    unsigned long exp_start_time = 0;
    unsigned long exp_stop_time = 0;

    lv_obj_t* create_panel(lv_obj_t* scr, int width, int height, lv_align_t align, int x_pos, int y_pos) {
        lv_obj_t* panel = lv_obj_create(scr);
        lv_obj_set_size(panel, width, height);
        lv_obj_align(panel, align, x_pos, y_pos);
        return panel;
    }

    lv_obj_t* create_lbl(lv_obj_t* scr, const char* txt, lv_align_t align, int x_pos, int y_pos) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, txt);
        lv_obj_align(lbl, align, x_pos, y_pos);
        return lbl;
    }

    void start_experiment();

    void stop_experiment();

    static void handle_btn_exp(lv_event_t* e) {
        lv_obj_t* btn = lv_event_get_target(e);
        if (btn == btn_start) {
          start_experiment();
        } else if (btn == btn_stop) {
          stop_experiment();
        }
    }

    lv_obj_t* create_btn(lv_obj_t* scr, 
                         const char* lbl_btn, 
                         lv_align_t align_lbl, 
                         lv_align_t align_btn, 
                         int width, 
                         int height, 
                         int x_pos, 
                         int y_pos) {
        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_t* lbl = create_lbl(btn, lbl_btn, align_lbl, 0, 0);

        lv_obj_set_size(btn, width, height);
        lv_obj_align(btn, align_btn, x_pos, y_pos);
        return btn;
    }

    lv_obj_t* create_chart(lv_obj_t* scr, int width, int height, lv_align_t align, int x_pos, int y_pos) {
      lv_obj_t* chart = lv_chart_create(scr);
      
      lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
      lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1500);
      lv_chart_set_point_count(chart, 30);
      lv_chart_set_div_line_count(chart, 6, 8);
      lv_obj_set_style_bg_color(chart, lv_color_hex(0x15191E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);

      lv_obj_set_size(chart, width, height);
      lv_obj_align(chart, align, x_pos, y_pos);
      return chart;
    }

    lv_obj_t* create_indicator(lv_obj_t* scr, int width, lv_align_t align, int x_pos, int y_pos) {
      lv_obj_t* indicator = lv_obj_create(scr);

      lv_obj_set_size(indicator, width, width); 
      lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);

      lv_obj_set_style_border_width(indicator, 3, 0);
      lv_obj_set_style_border_color(indicator, lv_palette_main(LV_PALETTE_RED), 0);

      lv_obj_set_style_shadow_width(indicator, 0, 0);
      lv_obj_set_style_pad_all(indicator, 0, 0);

      lv_obj_align(indicator, align, x_pos, y_pos);
      return indicator;
    }

    void update_datetime() {
      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        lv_label_set_text(date_lbl, "00.00.0000");
        lv_label_set_text(time_lbl, "00:00:00");
        return;
      }

      char date_buffer[16];
      char time_buffer[16];

      strftime(date_buffer, sizeof(date_buffer), "%d.%m.%Y", &timeinfo);
      strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &timeinfo);

      lv_label_set_text(date_lbl, date_buffer);
      lv_label_set_text(time_lbl, time_buffer);
    }

    void set_compile_temp() {
      struct tm timeinfo = {};
      
      char month[4];
      int day;
      int year;

      sscanf(__DATE__, "%3s %d %d", month, &day, &year);

      const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
        "Aug", "Sep", "Oct", "Nov", "Dec"
      };

      int month_number = 0;
      for (int i = 0; i < 12; i++) {
        if (strcmp(month, months[i]) == 0) {
          month_number = i;
          break;
        }
      }

      int hour;
      int min;
      int sec;

      sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
      
      timeinfo.tm_year = year - 1900;
      timeinfo.tm_mon = month_number;
      timeinfo.tm_mday = day;

      time_t timestamp = mktime(&timeinfo);
      
      struct timeval tv;
      tv.tv_sec = timestamp;
      tv.tv_usec = 0;

      settimeofday(&tv, nullptr);
    }

    void start_experiment() {
      state_exp = true;
      exp_start_time = millis();

      lv_label_set_text(active_status_lbl, "Active");
      lv_obj_set_style_border_color(indicator, lv_palette_main(LV_PALETTE_GREEN), 0);
    }

    void stop_experiment() {
      state_exp = false;
      exp_stop_time = millis() - exp_start_time;

      lv_label_set_text(active_status_lbl, "Not Active");
      lv_obj_set_style_border_color(indicator, lv_palette_main(LV_PALETTE_RED), 0);
    }

    void update_period_exp(unsigned long millisec) {
      unsigned long all_sec = millisec / 1000;

      unsigned long hours = all_sec / 3600;
      unsigned long min = (all_sec % 3600) / 60;
      unsigned long sec = all_sec % 60;

      char buffer[16];
      snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, min, sec);

      lv_label_set_text(lbl_period_value, buffer); 
    }

    void create_temp_window(lv_obj_t* scr) {
        // -> header
            lv_obj_t* panel_header = create_panel(scr, 320, 60, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_clear_flag(panel_header, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_header, 0, LV_PART_MAIN);
            
            lv_obj_t* header_lbl_h1 = create_lbl(panel_header, "LOGGER TEMP", LV_ALIGN_TOP_LEFT, 7, 10);
            lv_obj_t* header_lbl_h2 = create_lbl(panel_header, "By Torex Enterprise", LV_ALIGN_BOTTOM_LEFT, 7, -10);
            
            date_lbl = create_lbl(panel_header, "00.00.0000", LV_ALIGN_TOP_RIGHT, -7, 10);
            time_lbl = create_lbl(panel_header, "00:00:00", LV_ALIGN_BOTTOM_RIGHT, -7, -10);
        // -> header

        // -> main
            lv_obj_t* panel_main = create_panel(scr, 320, 180, LV_ALIGN_TOP_MID, 0, 60);
            lv_obj_clear_flag(panel_main, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_main, 0, LV_PART_MAIN);

            lv_obj_t* panel_screen_chart = create_panel(panel_main, 85, 40, LV_ALIGN_TOP_LEFT, 5, 3);
            lv_obj_clear_flag(panel_screen_chart, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_screen_chart, 0, LV_PART_MAIN);

            lv_obj_t* btn_screen_settings = create_btn(panel_screen_chart, "Settings", LV_ALIGN_CENTER, LV_ALIGN_TOP_MID, 81, 34, 0, 0);

            lv_obj_t* panel_status = create_panel(panel_main, 85, 128, LV_ALIGN_TOP_LEFT, 5, 45);
            lv_obj_clear_flag(panel_status, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_status, 0, LV_PART_MAIN);
            
            indicator = create_indicator(panel_status, 20, LV_ALIGN_TOP_MID, 0, 5);
            active_status_lbl = create_lbl(panel_status, "Not Active", LV_ALIGN_TOP_MID, 0, 30);
            
            lv_obj_t* lbl_period = create_lbl(panel_status, "Period Exp", LV_ALIGN_CENTER, 0, 20);
            lbl_period_value = create_lbl(panel_status, "00:00:00", LV_ALIGN_CENTER, 0, 40);

            lv_obj_t* panel_sensor_view = create_panel(panel_main, 220, 170, LV_ALIGN_TOP_RIGHT, -5, 3);
            lv_obj_clear_flag(panel_sensor_view, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_sensor_view, 0, LV_PART_MAIN);
            
            lv_obj_t* lbl_header_h3 = create_lbl(panel_sensor_view, "Temperature:", LV_ALIGN_TOP_LEFT, 3, 5);
            lv_obj_t* panel_sensor_data = create_panel(panel_sensor_view, 100, 30, LV_ALIGN_TOP_RIGHT, 0, 0);
            lv_obj_clear_flag(panel_sensor_data, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_sensor_data, 0, LV_PART_MAIN);

            lbl_value = create_lbl(panel_sensor_data, "0.0", LV_ALIGN_CENTER, 0, 0);

            chart_temp = create_chart(panel_sensor_view, 220, 95, LV_ALIGN_CENTER, 0, -5);
            series_temp = lv_chart_add_series(chart_temp, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);

            lv_obj_t* panel_active = create_panel(panel_sensor_view, 220, 40, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_clear_flag(panel_active, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(panel_active, 0, LV_PART_MAIN);

            btn_start = create_btn(panel_active, "Start", LV_ALIGN_CENTER, LV_ALIGN_LEFT_MID, 105, 40, 0, 0);
            lv_obj_add_event_cb(btn_start, handle_btn_exp, LV_EVENT_CLICKED, NULL);

            btn_stop = create_btn(panel_active, "Stop", LV_ALIGN_CENTER, LV_ALIGN_RIGHT_MID, 105, 40, 0, 0);
            lv_obj_add_event_cb(btn_stop, handle_btn_exp, LV_EVENT_CLICKED, NULL);
        // -> main  
    }

    void create_settings_window(lv_obj_t* scr) {
        // TODO -> settings
    }

    void main_initial_display() {
        // -> settings
            lcd.init();
            lcd.setRotation(1);

            lv_init();
            lv_disp_draw_buf_init(&draw_buf, buf, NULL, (320 * 10));
            lv_disp_drv_init(&disp_drv);
            disp_drv.hor_res = 320;
            disp_drv.ver_res = 240;

            disp_drv.flush_cb = my_flush_disp;
            disp_drv.draw_buf = &draw_buf;
            lv_disp_drv_register(&disp_drv);

            lv_indev_drv_init(&indev_drv);
            indev_drv.type = LV_INDEV_TYPE_POINTER;
            indev_drv.read_cb = my_touch_read;
            lv_indev_drv_register(&indev_drv);

            lcd.fillScreen(TFT_BLACK);
            lcd.setTextColor(TFT_WHITE);
        // -> settings
        
        // -> window_displayd
            screen_temp = lv_obj_create(NULL);
            create_temp_window(screen_temp);

            screen_settings = lv_obj_create(NULL);
            create_settings_window(screen_settings);

            lv_obj_t* scr = lv_scr_act();
            lv_scr_load(screen_temp);
        // -> window_display
    }

    void task_gui_loop(void* param) {
        main_initial_display();

        TickType_t last_temp_update = xTaskGetTickCount();
        TickType_t last_chart_update = xTaskGetTickCount();
        TickType_t last_period_update = xTaskGetTickCount();
        TickType_t last_datetime_update = xTaskGetTickCount();

        for (; ;) { // -> loop
            lv_tick_inc(5);
            lv_timer_handler();

            if (xTaskGetTickCount() - last_temp_update >= pdMS_TO_TICKS(100)) {
              last_temp_update = xTaskGetTickCount();
              
              char buffer[24];
              snprintf(buffer, sizeof(buffer), "%.3f", temp);
              lv_label_set_text(lbl_value, buffer);
            }

            if (xTaskGetTickCount() - last_chart_update >= pdMS_TO_TICKS(100)) {
              last_chart_update = xTaskGetTickCount();
              lv_chart_set_next_value(chart_temp, series_temp, (lv_coord_t) temp);
            }

            if (state_exp && xTaskGetTickCount() - last_period_update >= pdMS_TO_TICKS(100)) {
              last_period_update = xTaskGetTickCount();
              exp_stop_time = millis() - exp_start_time;
              update_period_exp(exp_stop_time);
            }

            if (xTaskGetTickCount() - last_datetime_update >= pdMS_TO_TICKS(1000)) {
              last_datetime_update = xTaskGetTickCount();
              update_datetime();
            }

            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
// -> GUI


void setup() {
  Serial.begin(115200);
  delay(500);

  gui_semaphore = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(
    task_gui_loop,
    "Task GUI",
    10000,
    NULL,
    1,
    &gui_task,
    0
  );

  initial_timezone();
  set_compile_temp();

  // -> littleFS
    LittleFS.begin(true);
  // -> littleFS

  initial_port();
  initial_wifi_ap();
  start_server();
  initial_timer_read();
}


void loop() {
  if (flag_read) {
    flag_read = false;
    if (Serial1.available()) {
      String request = Serial1.readStringUntil('\n');
      temp = request.toFloat();
      Serial.println(temp);
    }
  }
}
