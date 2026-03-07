#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <lvgl.h>

// --- Configurações de Pinos ---
#define I2C_SDA 27 
#define I2C_SCL 22
#define TFT_BL  21 
#define TOUCH_IRQ 36 // para acordar o ecrã

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

TFT_eSPI tft = TFT_eSPI();
static uint16_t draw_buf[screenWidth * 10];

// Ponteiros dos elementos gráficos
lv_obj_t * arc_temp;
lv_obj_t * label_temp;
lv_obj_t * bar_hum;
lv_obj_t * label_hum;
lv_obj_t * label_alert; // <-- Texto de Alerta

// Variáveis de tempo
unsigned long lastReadTime = 0;
const unsigned long readInterval = 90000;
uint32_t lastTick = 0; 

// Variáveis de Controle da Tela
unsigned long lastTouchTime = 0;
const unsigned long screenTimeout = 10000; // 10 segundos
bool isScreenOn = true;

// --- Função de Flush do LVGL 9 ---
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px_map, w * h, true);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

// --- Construção da Interface ---
void build_ui() {
  lv_obj_t * label_title = lv_label_create(lv_screen_active());
  lv_label_set_text(label_title, "Monitor Ambiental");
  lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);

  // Temperatura
  arc_temp = lv_arc_create(lv_screen_active());
  lv_obj_set_size(arc_temp, 130, 130); 
  lv_arc_set_rotation(arc_temp, 135);  
  lv_arc_set_bg_angles(arc_temp, 0, 270); 
  lv_arc_set_range(arc_temp, 0, 50);   
  lv_obj_align(arc_temp, LV_ALIGN_LEFT_MID, 20, 10); 
  lv_obj_remove_flag(arc_temp, LV_OBJ_FLAG_CLICKABLE); 

  label_temp = lv_label_create(arc_temp);
  lv_obj_center(label_temp);
  lv_label_set_text(label_temp, "--.- °C");

  // Humidade
  lv_obj_t * hum_title = lv_label_create(lv_screen_active());
  lv_label_set_text(hum_title, "Humidade:");
  lv_obj_align(hum_title, LV_ALIGN_RIGHT_MID, -60, -30);

  bar_hum = lv_bar_create(lv_screen_active());
  lv_obj_set_size(bar_hum, 110, 20); 
  lv_bar_set_range(bar_hum, 0, 100); 
  lv_obj_align(bar_hum, LV_ALIGN_RIGHT_MID, -20, 0); 

  label_hum = lv_label_create(lv_screen_active());
  lv_label_set_text(label_hum, "--.- %");
  lv_obj_align(label_hum, LV_ALIGN_RIGHT_MID, -60, 30);

  // --- Etiqueta de Alerta (Escondida no início) ---
  label_alert = lv_label_create(lv_screen_active());
  lv_label_set_text(label_alert, LV_SYMBOL_WARNING " SENSOR OFFLINE"); // Símbolo de perigo + texto
  lv_obj_set_style_text_color(label_alert, lv_color_hex(0xFF0000), 0); // Fica Vermelho
  lv_obj_align(label_alert, LV_ALIGN_BOTTOM_MID, 0, -10); // Posicionado em baixo
  lv_obj_add_flag(label_alert, LV_OBJ_FLAG_HIDDEN);       // Esconde a etiqueta
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(TOUCH_IRQ, INPUT); // Prepara o pino de toque físico

  Wire.begin(I2C_SDA, I2C_SCL);
  sht4.begin(&Wire);
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  tft.begin();
  tft.setRotation(1); 
  uint16_t calData[5] = { 300, 3600, 300, 3600, 1 };
  tft.setTouch(calData);

  lv_init();

  lv_display_t * disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  build_ui();
  
  lastTick = millis();
  lastTouchTime = millis();
}

void loop() {
  uint32_t currentTick = millis();
  lv_tick_inc(currentTick - lastTick);
  lastTick = currentTick;

  lv_timer_handler(); 
  delay(5);

  // --- Lógica de Acordar com Toque ---
  if (!isScreenOn && digitalRead(TOUCH_IRQ) == LOW) {
    digitalWrite(TFT_BL, HIGH); 
    isScreenOn = true;
    lastTouchTime = millis();
    delay(300); 
  }

  // --- Lógica de Apagar ---
  if (isScreenOn) {
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
      lastTouchTime = millis();
    }

    if (millis() - lastTouchTime >= screenTimeout) {
      digitalWrite(TFT_BL, LOW);
      isScreenOn = false;
    }
  }

  // --- Leitura do Sensor com Proteção ---
  if (millis() - lastReadTime >= readInterval) {
    lastReadTime = millis();
    sensors_event_t humidity, temp;
    
    // Tenta ler o sensor. A função retorna 'true' se funcionar e 'false' se falhar
    bool sensorOnline = sht4.getEvent(&humidity, &temp);

    if (sensorOnline) {
      // 1. O SENSOR FUNCIONANDO BEM
      lv_obj_add_flag(label_alert, LV_OBJ_FLAG_HIDDEN); // Esconde o aviso vermelho

      String textoTemp = String(temp.temperature, 1) + " °C";
      String textoHum = String(humidity.relative_humidity, 1) + " %";
      
      lv_arc_set_value(arc_temp, (int16_t)temp.temperature);
      lv_bar_set_value(bar_hum, (int32_t)humidity.relative_humidity, LV_ANIM_ON);
      lv_label_set_text(label_temp, textoTemp.c_str());
      lv_label_set_text(label_hum, textoHum.c_str());

    } else {
      // 2. O CABO DESCONECTOU-SE OU HOUVE ERRO!
      
      // Acorda o monitor sozinho para avisar!
      if (!isScreenOn) {
        digitalWrite(TFT_BL, HIGH);
        isScreenOn = true;
        lastTouchTime = millis(); 
      }

      // Mostra o texto vermelho
      lv_obj_remove_flag(label_alert, LV_OBJ_FLAG_HIDDEN); 

      // Coloca os gráficos em zero e o texto piscando com traços
      lv_label_set_text(label_temp, "--.- °C");
      lv_label_set_text(label_hum, "--.- %");
      lv_arc_set_value(arc_temp, 0);
      lv_bar_set_value(bar_hum, 0, LV_ANIM_OFF);

      // Tenta reiniciar o sensor (para que ele volte sozinho se reconectar o cabo)
      sht4.begin(&Wire);
    }
  }
}
