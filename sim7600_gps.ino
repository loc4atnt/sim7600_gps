#define TINY_GSM_MODEM_SIM7600
#define ARDUINOJSON_USE_DOUBLE 1
#define VIETTEL //MOBI VINA

#include <ArduinoJson.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>

#define SIM_RESET_PIN 15
#define SIM_AT_BAUDRATE 115200
#define GSM_PIN ""

#define GETTING_GPS_INTERVAL 10000UL

// GPRS config
#ifdef MOBI
const char apn[] = "m-wap";
const char gprsUser[] = "mms";
const char gprsPass[] = "mms";
#endif
#ifdef VIETTEL
const char apn[] = "v-internet";
const char gprsUser[] = "";
const char gprsPass[] = "";
#endif
#ifdef VINA
const char apn[] = "m3-world";
const char gprsUser[] = "mms";
const char gprsPass[] = "mms";
#endif

// MQTT config
#define PORT_MQTT 1883
const char* broker = "bsmart.cloud.shiftr.io";
const char* mqtt_client_name = "loc4atnt nek";
const char* mqtt_user = "bsmart";
const char* mqtt_pass = "VohqtjUTtJpruNxu";
const char* topic_gps = "mycar/gps";

TinyGsm modem(Serial2);
TinyGsmClient gsmClient(modem);
PubSubClient gsmMqtt(gsmClient);

String gps_updtTime;
float gps_lat       = 0;
float gps_lon       = 0;
float gps_speed     = 0;
float gps_alt       = 0;
int   gps_vsat      = 0;
int   gps_usat      = 0;
float gps_accuracy  = 0;
int   gps_year      = 0;
int   gps_month     = 0;
int   gps_day       = 0;
int   gps_hour      = 0;
int   gps_min       = 0;
int   gps_sec       = 0;

unsigned long lastTime = 0;

// Hàm dùng để thiết lập modem SIM tạo kết nối GPRS
bool connectToGPRS() {
  // Unlock SIM (nếu có)
  if (GSM_PIN && modem.getSimStatus() != 3)
    modem.simUnlock(GSM_PIN);

  Serial.println("Ket noi toi nha mang...");
  while (!modem.waitForNetwork(60000L)) {
    Serial.println("._.");
    delay(5000);
  }

  // Nếu không thấy sóng từ nhà mạng thì trả về false
  if (!modem.isNetworkConnected())
    return false;

  Serial.println("Ket noi GPRS");
  if (!modem.gprsConnect(apn)) {// Hàm kết nối tới gprs trả về true/false cho biết có kết nối được hay chưa
    Serial.print("Khong the ket noi GPRS - ");
    Serial.println(apn);
    return false;
  }

  // Kiểm tra lại lần nữa để chắc cú
  if (modem.isGprsConnected()) {
    Serial.println("Da ket noi duoc GPRS!");
    return true;
  }
  return false;
}

// Hàm khởi động module SIM
bool initModemSIM() {
  Serial.println("Bat dau khoi dong module SIM");

  // Đặt chân reset của module xuống LOW để chạy
  pinMode(SIM_RESET_PIN, OUTPUT);
  digitalWrite(SIM_RESET_PIN, LOW);
  delay(5000);

  Serial2.begin(SIM_AT_BAUDRATE);// Module SIM giao tiếp với ESP qua cổng Serial2 bằng AT cmds

  if (!modem.restart()) {
    Serial.println("Khong the khoi dong lai module SIM => Co loi");
    return false;
  }
  return true;
}

// Hàm kết nối tới MQTT Broker
boolean connectMQTT(PubSubClient *mqtt) {
  Serial.print("Ket noi broker ");
  Serial.print(broker);
  boolean status = mqtt->connect(mqtt_client_name, mqtt_user, mqtt_pass);
  if (status == false) {
    Serial.println(" khong thanh cong!");
    return false;
  }
  Serial.println(" thanh cong!");

  return mqtt->connected();
}

void pushGPSData() {
  if (!gps_updtTime.isEmpty()) { // Chỉ publish dữ liệu khi nó được cập nhật (updtTime!="")
    String sendingStr = "";
    StaticJsonDocument<128> doc;
    doc["lat"] = gps_lat;
    doc["lon"] = gps_lon;
    doc["alt"] = gps_alt;
    doc["speed"] = gps_speed;
    doc["time"] = gps_updtTime;
    serializeJson(doc, sendingStr);
    gsmMqtt.publish(topic_gps, sendingStr.c_str());
    gps_updtTime = "";
    Serial.print("publish: ");
    Serial.println(sendingStr);// Debug
  }
}

// Hàm xử lý trong loop() dành cho MQTT Client dùng GPRS
void mqttLoopWithGPRS() {
  if (!gsmMqtt.connected()) { // Nếu chưa có kết nối tới MQTT Broker
    Serial.println("Ket noi MQTT voi GPRS");
    while ( (!gsmMqtt.connected()) && modem.isGprsConnected()) {
      connectMQTT(&gsmMqtt);
      delay(5000);
    }
  } else { // Nếu đã có kết nối tới MQTT Broker rồi
    pushGPSData();
    gsmMqtt.loop();// Hàm xử lý của thư viện mqtt
  }
}

void getGPSData() {
  if ((millis() - lastTime) < GETTING_GPS_INTERVAL) return; // Chi lay du lieu GPS moi GETTING_GPS_INTERVAL (ms)
  if (modem.getGPS(&gps_lat, &gps_lon, &gps_speed, &gps_alt, &gps_vsat, &gps_usat, &gps_accuracy,
                   &gps_year, &gps_month, &gps_day, &gps_hour, &gps_min, &gps_sec)) {
    Serial.println("Getted GPS Info");
    gps_updtTime = modem.getGSMDateTime(DATE_FULL);
  } else {
    Serial.println("Cannot get GPS");
    gps_updtTime = "";
  }
  lastTime = millis();
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\nInit modem SIM");
  while (!initModemSIM()) {
    Serial.print("..");
    delay(1000);
  }

  Serial.println("\nConnect to GPRS");
  while (!connectToGPRS()) {
    Serial.print("..");
    delay(1000);
  }

  Serial.println("\nEnabling GPS/GNSS/GLONASS and waiting 20s for warm-up");
  modem.enableGPS();
  delay(20000L);

  gsmMqtt.setServer(broker, PORT_MQTT);

  Serial.println("Setup Done!");
}

void loop() {
  if (modem.isGprsConnected()) {
    getGPSData();
    mqttLoopWithGPRS();
  } else {
    Serial.println("Khong co ket noi GRPS!");
    delay(2000);
  }
}
