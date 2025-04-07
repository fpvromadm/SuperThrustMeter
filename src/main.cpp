#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>
#include <AsyncTCP.h>
#include <FS.h>
#include <SPIFFS.h>

//файловая система
File logFile;
String logFilename = "";

bool isRunning = false;
bool isPaused = false;
bool autoPaused = false;
String currentCsvPath = "";

// Подключение HX711 к ESP32
#define DT 21  // Пин данных от HX711
#define SCK 22 // Пин тактового сигнала от HX711
HX711 scale;

// Данные Wi-Fi
const char* ssid = "romadOK24";
const char* password = "boosido3087";

// Переменные для веса
float tareOffset = 0.0;        // Смещение нуля (тарировка)
float weightGrams = 0.0;       // Текущий измеренный вес
float previousWeight = 0.0;    // Предыдущее значение веса для фильтрации
const float deadZone = 3.0;    // Мёртвая зона (не учитывать колебания менее 3 г)

// Сглаживание значений
#define SMOOTHING 3
float smoothBuffer[SMOOTHING] = {0};
int smoothIndex = 0;

float maxRecordedWeight = 0.0; // Максимальный зафиксированный вес

AsyncWebServer server(80); // Веб-сервер на порту 80

// Функция сглаживания — скользящее среднее из последних N значений
float getSmoothedWeight(float raw) {
  smoothBuffer[smoothIndex] = raw;
  smoothIndex = (smoothIndex + 1) % SMOOTHING;

  float sum = 0;
  for (int i = 0; i < SMOOTHING; i++) {
    sum += smoothBuffer[i];
  }
  return sum / SMOOTHING;
}

//Регулятор по PWM
const int escPin = 17; //пин регулятора
int pwmValue = 1000; // стартовое значение

// Преобразование из микросекунд в значение duty для 16 бит (20 мс = 65536)
uint32_t usToDuty(int us) {
  return (uint32_t)(us * 65536L / 20000L); // 20 мс период
}


void setup() {

  //Настраиваем PWM регулятор
  ledcSetup(0, 50, 16);        // канал 0, 50 Гц, 16-битное разрешение
  ledcAttachPin(escPin, 0);    // подключить пин к каналу
  ledcWrite(0, usToDuty(pwmValue));  // установить сигнал

  Serial.begin(115200); // Запускаем сериал-порт для отладки

  // 3. Инициализация HX711
  scale.begin(DT, SCK);
  delay(500);
  scale.set_scale(210);  // Устанавливаем коэффициент шкалы (подобран вручную)
  scale.tare();           // Устанавливаем текущий вес как ноль

  //Если весы не определились, пишем сообщение
  if (!scale.is_ready()) {
    Serial.println("HX711 не подключен! Проверьте соединение.");
  };

   // Инициализация SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Ошибка инициализации SPIFFS");
    return;
  }


// 2. Подключение к Wi-Fi
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
}
Serial.println("\nWi-Fi подключен: " + WiFi.localIP().toString());

// ✅ просто запускаем сервер сразу:
server.begin();

  // 5. Главная страница из SPIFFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/weight_ui.html", "text/html");
  });

  // Обработка запроса текущего веса с фильтрацией и сглаживанием
  server.on("/weight", HTTP_GET, [](AsyncWebServerRequest* request){
    float raw = scale.get_units(5);                  // Считываем 5 измерений
    float adjusted = raw - tareOffset;               // Применяем тарировку
    float absWeight = abs(adjusted);                 // Модуль веса

    float smoothed = getSmoothedWeight(absWeight);   // Применяем сглаживание

    // Обновляем вес, если изменение превышает deadZone
    if (abs(smoothed - previousWeight) > deadZone) {
      weightGrams = smoothed;
      previousWeight = smoothed;
    }

    // Если вес меньше deadZone, сбрасываем в 0
    if (smoothed < deadZone) {
      weightGrams = 0;
      previousWeight = 0;
    }

    // Гарантируем положительное значение
    float filtered = weightGrams < 0 ? 0 : weightGrams;

    // Отправляем JSON-ответ на клиент
    String json = "{\"weight\":" + String(filtered, 2) + "}";
    request->send(200, "application/json", json);
  });

  // Обработка запроса на тарировку (обнуление веса и максимумов)
  server.on("/tare", HTTP_GET, [](AsyncWebServerRequest* request){
    tareOffset = scale.get_units(10);  // Новая точка отсчета
    weightGrams = 0;
    previousWeight = 0;
    maxRecordedWeight = 0;

    // Сброс буфера сглаживания
    for (int i = 0; i < SMOOTHING; i++) {
      smoothBuffer[i] = 0;
    }
    smoothIndex = 0;

    request->send(200, "text/plain", "Tared");
  });

  //Работа с PWM
  server.on("/thrust", HTTP_GET, [](AsyncWebServerRequest* request){
    if (request->hasParam("value")) {
      int val = request->getParam("value")->value().toInt();
      if (val >= 1000 && val <= 2000) {
        pwmValue = val;
        ledcWrite(0, usToDuty(pwmValue));
        Serial.println("PWM (из браузера): " + String(pwmValue));
        request->send(200, "text/plain", "PWM updated");
        return;
      }
    }
    request->send(400, "text/plain", "Invalid PWM value");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* request){
    // Генерация нового имени файла
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "/log-%Y%m%d-%H%M%S.csv", tm_info);
    logFilename = String(filename);
  
    // Создание файла и запись заголовка
    logFile = SPIFFS.open(logFilename, FILE_WRITE);
    if (logFile) {
      logFile.println("time,weight,pwm");
      logFile.close();
      request->send(200, "text/plain", "Log file created: " + logFilename);
    } else {
      request->send(500, "text/plain", "Failed to create log file");
    }
  });

  // Обработчик загрузки CSV файла
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    if (currentCsvPath != "" && SPIFFS.exists(currentCsvPath)) {
      request->send(SPIFFS, currentCsvPath, "text/csv");
    } else {
      request->send(404, "text/plain", "Файл не найден");
    }
  });

  //Путь к странице с результатами
  server.on("/results", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/results.html", "text/html");
  });

  //разрешаем чтение папки с логами
  server.serveStatic("/output", SPIFFS, "/output");

}

void loop() {
  
  float weight = abs(scale.get_units());
  Serial.print("Текущий вес: ");
  Serial.print(weight, 2);
  Serial.println(" г");
  delay(500);

  // Пример: опрашивать значение из Serial (или HTTP / WebSocket)
  if (Serial.available()) {
    int val = Serial.parseInt();
    if (val >= 1000 && val <= 2000) {
      pwmValue = val;
      ledcWrite(0, usToDuty(pwmValue));
      Serial.println("PWM set to " + String(pwmValue));
    }
  }
  delay(20);

  //запись лога в файл
  if (isRunning && !isPaused && !autoPaused) {
    File file = SPIFFS.open(currentCsvPath, FILE_APPEND);
    if (file) {
      unsigned long timeMs = millis();
      file.printf("%lu,%.2f,%d\n", timeMs, weight, pwmValue);
      file.close();
    }
  }
  
}
