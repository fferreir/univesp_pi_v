#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <lvgl.h>
#include <WiFi.h>
#include <PubSubClient.h>

// --- 1. Configurações de Rede e MQTT ---
const char* ssid = "NOME_DO_SEU_WIFI";
const char* password = "SENHA_DO_WIFI";
const char* mqtt_server = "IP_DO_SEU_SERVIDOR"; // Ex: 192.168.1.100
const char* mqtt_user = "mqtt_user";
const char* mqtt_pass = "mqtt_pass";
const char* mqtt_topic = "mqtt_topic"; // Tópico isolado

WiFiClient espClient;
PubSubClient client(espClient);

// --- 2. Configurações de Hardware (ESP32-2432S028) ---
#define I2C_SDA 27
#define I2C_SCL 22
#define TFT_BL  21
#define TOUCH_IRQ 36

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
TFT_eSPI tft = TFT_eSPI();

static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;
static uint16_t draw_buf[screenWidth * 10];

// Objetos da Interface
lv_obj_t * arc_temp;
lv_obj_t * label_temp;
lv_obj_t * bar_hum;
lv_obj_t * label_hum;
lv_obj_t * label_alert;

// Controle de Tempo
unsigned long lastReadTime = 0;
const unsigned long readInterval = 60000; // Enviar a cada 1 minuto
uint32_t lastTick = 0;
unsigned long lastTouchTime = 0;
const unsigned long screenTimeout = 15000; // 15 segundos
bool isScreenOn = true;

// --- 3. Funções de Conectividade ---
void setup_wifi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando MQTT...");
    if (client.connect("ESP32_Bioterio_Sala1", mqtt_user, mqtt_pass)) {
      Serial.println("Conectado ao Broker!");
    } else {
      Serial.print("falha, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

// --- 4. Funções LVGL ---
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px_map, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

void build_ui() {
  lv_obj_t * label_title = lv_label_create(lv_screen_active());
  lv_label_set_text(label_title, "BIOTERIO - SALA 1");
  lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);

  arc_temp = lv_arc_create(lv_screen_active());
  lv_obj_set_size(arc_temp, 130, 130);
  lv_arc_set_range(arc_temp, 0, 50);
  lv_obj_align(arc_temp, LV_ALIGN_LEFT_MID, 20, 10);

  label_temp = lv_label_create(arc_temp);
  lv_obj_center(label_temp);
  lv_label_set_text(label_temp, "--.- C");

  bar_hum = lv_bar_create(lv_screen_active());
  lv_obj_set_size(bar_hum, 110, 20);
  lv_obj_align(bar_hum, LV_ALIGN_RIGHT_MID, -20, 0);

  label_hum = lv_label_create(lv_screen_active());
  lv_obj_align(label_hum, LV_ALIGN_RIGHT_MID, -60, 30);

  label_alert = lv_label_create(lv_screen_active());
  lv_label_set_text(label_alert, LV_SYMBOL_WARNING " SENSOR ERROR");
  lv_obj_set_style_text_color(label_alert, lv_color_hex(0xFF0000), 0);
  lv_obj_align(label_alert, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_flag(label_alert, LV_OBJ_FLAG_HIDDEN);
}

// --- 5. Setup e Loop Principal ---
void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!sht4.begin(&Wire)) {
    Serial.println("SHT4x nao encontrado!");
  }

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
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  uint32_t currentTick = millis();
  lv_tick_inc(currentTick - lastTick);
  lastTick = currentTick;
  lv_timer_handler();

  // Controle da Tela (Touch para acordar)
  if (!isScreenOn && digitalRead(TOUCH_IRQ) == LOW) {
    digitalWrite(TFT_BL, HIGH);
    isScreenOn = true;
    lastTouchTime = millis();
    delay(200);
  }

  if (isScreenOn) {
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) lastTouchTime = millis();
    if (millis() - lastTouchTime >= screenTimeout) {
      digitalWrite(TFT_BL, LOW);
      isScreenOn = false;
    }
  }

  // Leitura do Sensor e Envio MQTT
  if (millis() - lastReadTime >= readInterval) {
    lastReadTime = millis();
    sensors_event_t humidity, temp;
    bool sensorOnline = sht4.getEvent(&humidity, &temp);

    if (sensorOnline) {
      lv_obj_add_flag(label_alert, LV_OBJ_FLAG_HIDDEN);

      // Atualiza Display
      lv_arc_set_value(arc_temp, (int16_t)temp.temperature);
      lv_bar_set_value(bar_hum, (int32_t)humidity.relative_humidity, LV_ANIM_ON);
      lv_label_set_text(label_temp, (String(temp.temperature, 1) + " C").c_str());
      lv_label_set_text(label_hum, (String(humidity.relative_humidity, 1) + " %").c_str());

      // Monta JSON e envia
      String payload = "{";
      payload += "\"local\":\"Sala 1\",";
      payload += "\"temp\":" + String(temp.temperature, 2) + ",";
      payload += "\"hum\":" + String(humidity.relative_humidity, 2);
      payload += "}";

      client.publish(mqtt_topic, payload.c_str());
      Serial.println("Enviado: " + payload);

    } else {
      lv_obj_remove_flag(label_alert, LV_OBJ_FLAG_HIDDEN);
      sht4.begin(&Wire);
    }
  }
}
