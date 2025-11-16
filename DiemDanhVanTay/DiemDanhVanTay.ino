#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>

#define RX 16
#define TX 17

HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

WiFiClientSecure client;
HTTPClient http;

// WIFI
const char *ssid = "P2407-DucChinh";
const char *pass = "ducchinh";

// API BASE
String API_BASE = "https://script.google.com/macros/s/AKfycbyxb7WrXFDOlkKjSChvhm1kBq-yiGBzezWE4wFqAKX2RPMkj7rdzQkyWCoc8enOdY8fng/exec";

// STATE
String activeClass = "";
String activeMode = "";
String activeCa = "Ca1";
bool startFlag = false;

bool checkoutSession = false;
bool checkinSession = false;
bool isProcessing = false;

// STATUS
struct StatusInfo {
  int total;
  int checkedIn;
  int checkedOut;
  int leftIn;
  int leftOut;
};

/* ============================================================
   URL ENCODE
============================================================ */
String urlEncode(String s) {
  String out = "";
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.') out += c;
    else {
      out += '%';
      char hex1 = (c >> 4) & 0xF;
      char hex2 = c & 0xF;
      out += char(hex1 < 10 ? '0' + hex1 : 'A' + hex1 - 10);
      out += char(hex2 < 10 ? '0' + hex2 : 'A' + hex2 - 10);
    }
  }
  return out;
}

/* ============================================================
   HTTP GET
============================================================ */
String GET(String url) {
  delay(70);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(client, url);

  int code = http.GET();
  String res = http.getString();
  http.end();

  return (code == 200) ? res : "";
}

/* ============================================================
   READ START CLASS (Web quyết định Start + CA)
============================================================ */
void readStartClass() {

  String json = GET(API_BASE + "?action=getClasses&nocache=" + millis());
  if (json == "") return;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, json)) return;

  bool found = false;
  String newClass = "";
  String newMode = "";

  for (JsonObject c : doc.as<JsonArray>()) {
    if (c["Start"].as<bool>()) {
      newClass = c["ClassID"].as<String>();
      newMode = c["Mode"].as<String>();
      found = true;
      break;
    }
  }

  if (!found) {
    if (startFlag == true) {
      Serial.println("===== CLASS STOP =====");
    }

    startFlag = false;
    activeClass = "";
    activeMode = "";
    checkoutSession = false;
    checkinSession = false;
    isProcessing = false;

    return;
  }


  if (newClass != activeClass || newMode != activeMode) {

    activeClass = newClass;
    activeMode = newMode;
    startFlag = true;

    checkoutSession = false;
    checkinSession = false;
    isProcessing = false;

    Serial.println("===== CLASS START =====");
    Serial.println("Class : " + activeClass);
    Serial.println("Ca    : " + activeCa);
    Serial.println("Mode  : " + activeMode);
    Serial.println("=======================");
  }
}

/* ============================================================
   GET STUDENTS
============================================================ */
String getStudents() {
  return GET(API_BASE + "?action=getStudents&class=" + activeClass + "&nocache=" + millis());
}

/* ============================================================
   CHECK STATUS
============================================================ */
StatusInfo getStatus() {
  StatusInfo st = { 0, 0, 0, 0, 0 };

  String url = API_BASE + "?action=checkUpdate";
  url += "&class=" + activeClass;
  url += "&ca=" + activeCa;

  String json = GET(url);
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, json)) return st;

  st.total = doc["total"];
  st.checkedIn = doc["checkedIn"];
  st.checkedOut = doc["checkedOut"];
  st.leftIn = doc["leftToCheckIn"];
  st.leftOut = doc["leftToCheckOut"];

  return st;
}

/* ============================================================
   SEND ATTENDANCE
============================================================ */
void sendAttendance(String sid, String name, String event, int fid) {

  String url = API_BASE + "?action=logAttendance";
  url += "&classID=" + activeClass;
  url += "&ca=" + String(activeCa);
  url += "&fingerID=" + String(fid);
  url += "&studentID=" + sid;
  url += "&name=" + urlEncode(name);
  url += "&event=" + event;

  GET(url);
}

void saveFinger(String sid, String name, int fid) {
  GET(API_BASE + "?action=registerFinger&classID=" + activeClass + "&studentID=" + sid + "&fingerID=" + fid);
}

void autoUpdateMode(String mode) {
  GET(API_BASE + "?action=auto&class=" + activeClass + "&mode=" + mode);
}

/* ============================================================
   ENROLL FINGER
============================================================ */
bool enrollFinger(int id) {
  int p = -1;

  Serial.println("Đặt ngón tay lần 1...");
  while (p != FINGERPRINT_OK) p = finger.getImage();
  finger.image2Tz(1);

  Serial.println("Đặt ngón tay lần 2...");
  p = -1;
  while (p != FINGERPRINT_OK) p = finger.getImage();
  finger.image2Tz(2);

  if (finger.createModel() != FINGERPRINT_OK) return false;

  return finger.storeModel(id) == FINGERPRINT_OK;
}

/* ============================================================
   REGISTER ALL
============================================================ */
void registerAll() {

  static bool loaded = false;
  static DynamicJsonDocument doc(4096);
  static JsonArray arr;
  static int index = 0;

  if (!loaded) {
    String json = getStudents();
    deserializeJson(doc, json);
    arr = doc.as<JsonArray>();
    index = 0;
    loaded = true;
    Serial.println("===== BẮT ĐẦU ĐĂNG KÝ =====");
  }

  if (index >= arr.size()) {
    Serial.println("=== ĐĂNG KÝ XONG ===");
    autoUpdateMode("Checkout");
    loaded = false;
    isProcessing = false;
    return;
  }

  JsonObject s = arr[index];
  String sid = s["StudentID"].as<String>();
  String name = s["Name"].as<String>();

  finger.getTemplateCount();
  int newID = finger.templateCount + 1;

  Serial.printf("-> ĐK sinh viên %s (%s), FingerID = %d\n",
                name.c_str(), sid.c_str(), newID);

  if (!enrollFinger(newID)) {
    Serial.println("❌ Lỗi đăng ký → thử lại...");
    return;
  }

  saveFinger(sid, name, newID);
  sendAttendance(sid, name, "Check-In", newID);

  Serial.println("✔ Hoàn tất!");
  index++;
}

/* ============================================================
   SEARCH FINGER
============================================================ */
bool searchFinger(int &fid, String &sid, String &name) {
  if (finger.getImage() != FINGERPRINT_OK) return false;
  if (finger.image2Tz() != FINGERPRINT_OK) return false;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return false;

  fid = finger.fingerID;

  String json = getStudents();
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, json);

  for (JsonObject s : doc.as<JsonArray>()) {
    if (s["FingerID"].as<int>() == fid) {
      sid = s["StudentID"].as<String>();
      name = s["Name"].as<String>();
      return true;
    }
  }

  return false;
}

/* ============================================================
   CHECK-IN — KHÔNG TIMEOUT
============================================================ */
void doCheckIn() {

  if (!checkinSession) {
    checkinSession = true;
    Serial.println("===== CHECK-IN BẮT ĐẦU =====");
  }

  int fid;
  String sid, name;

  if (searchFinger(fid, sid, name)) {

    Serial.println("✔ CHECK-IN: " + name);
    sendAttendance(sid, name, "Check-In", fid);

    StatusInfo st = getStatus();
    Serial.printf("Còn %d sinh viên chưa Check-In\n", st.leftIn);

    if (st.leftIn == 0) {
      Serial.println("🎉 Hoàn tất CHECK-IN!");
      autoUpdateMode("Checkout");
      checkinSession = false;
      isProcessing = false;
    }
  }
}

/* ============================================================
   CHECK-OUT — KHÔNG TIMEOUT
============================================================ */
void doCheckOut() {

  StatusInfo st0 = getStatus();
  if (st0.checkedIn == 0) {
    Serial.println("Vui lòng CHECKIN để có thể CHECKOUT");
    autoUpdateMode("Attendance");
    checkoutSession = false;
    isProcessing = false;
    return;
  }

  if (!checkoutSession) {
    checkoutSession = true;
    Serial.println("===== CHECKOUT BẮT ĐẦU =====");
  }

  int fid;
  String sid, name;

  if (searchFinger(fid, sid, name)) {

    Serial.println("✔ CHECK-OUT: " + name);
    sendAttendance(sid, name, "Check-Out", fid);

    StatusInfo st = getStatus();
    Serial.printf("Còn %d sinh viên chưa Check-Out\n", st.leftOut);

    // ⭐ Nếu check-out xong hết → chuyển về Attendance
    if (st.leftOut == 0) {
      Serial.println("🎉 Hoàn tất CHECKOUT!");
      autoUpdateMode("Attendance");
      checkoutSession = false;
      isProcessing = false;
    }
  }
}


/* ============================================================
   SETUP
============================================================ */
void setup() {
  Serial.begin(115200);

  fingerSerial.begin(57600, SERIAL_8N1, RX, TX);
  finger.begin(57600);

  if (finger.verifyPassword())
    Serial.println("AS608 READY");
  else
    Serial.println("AS608 ERROR!");

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  client.setInsecure();
  Serial.println("WiFi Connected!");
}

/* ============================================================
   LOOP
============================================================ */
void loop() {

  readStartClass();

  if (!startFlag) {
    isProcessing = false;
    delay(100);
    return;
  }

  isProcessing = true;

  if (activeMode == "Register")       registerAll();
  else if (activeMode == "Attendance") doCheckIn();
  else if (activeMode == "Checkout")   doCheckOut();

  delay(60);
}
