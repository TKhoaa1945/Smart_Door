#define BLYNK_TEMPLATE_ID "TMPL6-FpwuvC-"
#define BLYNK_TEMPLATE_NAME "FreeRTOS"
#define BLYNK_AUTH_TOKEN "bPV2coFs6GhG5u82imbcLy5k1oaka7tc"
#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <EEPROM.h>

char ssid[] = "TICH-TEP";
char pass[] = "tichtep5423";

// Servo and initial parameters
Servo myservo;
String password = "260803"; // Default password
String input = "";
#define EEPROM_SIZE 64
#define PASSWORD_ADDRESS 0
#define MAX_PASSWORD_LENGTH 10

// Keypad configuration
#define ROWS 4
#define COLS 4
char keyMap[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};
uint8_t rowPins[ROWS] = {32, 33, 27, 26};
uint8_t colPins[COLS] = {25, 13, 12, 14};
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);

// LCD configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RFID configuration
#define RST_PIN 4
#define SS_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

// Buzzer configuration
#define BUZZER_PIN 16

// Valid UIDs
String allowedUIDs[] = {"F3 9F D1 0E", "F3 07 D9 A7"};

// Global variables
unsigned long lastKeyPressTime = 0;
const unsigned long inactivityDelay = 10000;
bool isBacklightOn = true;
bool changeMode = false;
bool confirmOldPassword = false;
bool inputReady = false; // New flag to signal input submission
String newPassword = "";
int cnt = 0;

// FreeRTOS semaphores
SemaphoreHandle_t lcdSemaphore;
SemaphoreHandle_t spiSemaphore;
SemaphoreHandle_t passwordSemaphore;

// Function prototypes
bool isValidUID(String uid);
String normalizeUID(byte *buffer, byte size);
void playBuzzerSound();
void openLock();
void enterLowPowerMode();
void wakeUpFromLowPowerMode();
void loadPasswordFromEEPROM();
void savePasswordToEEPROM(String newPassword);

// FreeRTOS task handles
TaskHandle_t keypadTaskHandle = NULL;
TaskHandle_t rfidTaskHandle = NULL;
TaskHandle_t blynkTaskHandle = NULL;
TaskHandle_t passwordChangeTaskHandle = NULL;

// Normalize UID
String normalizeUID(byte *buffer, byte size) {
    String uid = "";
    for (byte i = 0; i < size; i++) {
        if (buffer[i] < 0x10) uid += "0";
        uid += String(buffer[i], HEX);
        if (i < size - 1) uid += " ";
    }
    uid.toUpperCase();
    return uid;
}

// Check valid UID
bool isValidUID(String uid) {
    for (String allowedUID : allowedUIDs) {
        if (uid == allowedUID) {
            return true;
        }
    }
    return false;
}

// Play buzzer sound
void playBuzzerSound() {
    digitalWrite(BUZZER_PIN, HIGH);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    digitalWrite(BUZZER_PIN, LOW);
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

// Load password from EEPROM
void loadPasswordFromEEPROM() {
    char storedPassword[MAX_PASSWORD_LENGTH + 1];
    for (int i = 0; i < MAX_PASSWORD_LENGTH; i++) {
        storedPassword[i] = EEPROM.read(PASSWORD_ADDRESS + i);
        if (storedPassword[i] == 0 || storedPassword[i] == 255) {
            storedPassword[i] = 0;
            break;
        }
    }
    storedPassword[MAX_PASSWORD_LENGTH] = '\0';
    if (strlen(storedPassword) >= 4) {
        password = String(storedPassword);
    } else {
        password = "260803";
        savePasswordToEEPROM(password);
    }
}

// Save password to EEPROM
void savePasswordToEEPROM(String newPassword) {
    for (int i = 0; i < MAX_PASSWORD_LENGTH; i++) {
        if (i < newPassword.length()) {
            EEPROM.write(PASSWORD_ADDRESS + i, newPassword[i]);
        } else {
            EEPROM.write(PASSWORD_ADDRESS + i, 0);
        }
    }
    EEPROM.commit();
}

// Enter low power mode
void enterLowPowerMode() {
    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
        lcd.noDisplay();
        lcd.noBacklight();
        isBacklightOn = false;
        xSemaphoreGive(lcdSemaphore);
    }
}

// Wake up from low power mode
void wakeUpFromLowPowerMode() {
    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
        lcd.display();
        lcd.backlight();
        isBacklightOn = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Nhap MK:");
        xSemaphoreGive(lcdSemaphore);
    }
}

// Blynk task
void blynkTask(void *pvParameters) {
    while (1) {
        Blynk.run();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Open lock
void openLock() {
    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Mo khoa");
        xSemaphoreGive(lcdSemaphore);
    }

    Serial.println("Opening lock");
    myservo.attach(15);
    myservo.setPeriodHertz(50);
    playBuzzerSound();

    for (int pos = 0; pos <= 90; pos++) {
        myservo.write(pos);
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }

    for (int i = 7; i >= 0; i--) {
        if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
            lcd.setCursor(15, 0);
            lcd.print(i);
            xSemaphoreGive(lcdSemaphore);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    playBuzzerSound();

    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Dong cua");
        xSemaphoreGive(lcdSemaphore);
    }

    for (int pos = 90; pos >= 0; pos--) {
        myservo.write(pos);
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }

    myservo.detach();
    Serial.println("Lock closed.");

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Nhap MK:");
        xSemaphoreGive(lcdSemaphore);
    }
}

// Blynk write handlers
BLYNK_WRITE(V2) {
    openLock();
}

BLYNK_WRITE(V1) {
    int pinValue = param.asInt();
    Serial.print("V1 slider value: ");
    Serial.println(pinValue);

    if (pinValue == 1) {
        Serial.println("Door Opening By App...");
        if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Mo khoa");
            xSemaphoreGive(lcdSemaphore);
        }

        myservo.attach(15);
        myservo.setPeriodHertz(50);
        playBuzzerSound();

        for (int pos = 0; pos <= 90; pos++) {
            myservo.write(pos);
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }
    } else {
        Serial.println("Door Closing By App...");
        playBuzzerSound();

        if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Dong cua");
            xSemaphoreGive(lcdSemaphore);
        }

        for (int pos = 90; pos >= 0; pos--) {
            myservo.write(pos);
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }

        myservo.detach();
        Serial.println("Lock closed.");

        vTaskDelay(2000 / portTICK_PERIOD_MS);

        if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Nhap MK:");
            xSemaphoreGive(lcdSemaphore);
        }
    }
}

// Password change task
void passwordChangeTask(void *pvParameters) {
    while (1) {
        if (changeMode && inputReady && xSemaphoreTake(passwordSemaphore, portMAX_DELAY) == pdTRUE) {
            if (!confirmOldPassword) {
                if (input.equals(password)) {
                    confirmOldPassword = true;
                    input = "";
                    inputReady = false;
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("MK cu dung");
                        lcd.setCursor(0, 1);
                        lcd.print("Nhap MK moi:");
                        xSemaphoreGive(lcdSemaphore);
                    }
                } else {
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Sai MK cu!");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Nhap MK:");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    changeMode = false;
                    input = "";
                    inputReady = false;
                }
            } else {
                if (input.length() < 4) {
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("MK qua ngan");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Nhap MK moi:");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    input = "";
                    inputReady = false;
                } else {
                    newPassword = input;
                    password = newPassword;
                    savePasswordToEEPROM(newPassword);
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Da doi MK!");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Nhap MK:");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    changeMode = false;
                    confirmOldPassword = false;
                    input = "";
                    inputReady = false;
                }
            }
            xSemaphoreGive(passwordSemaphore);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Keypad task
void keypadTask(void *pvParameters) {
    while (1) {
        char key = keypad.getKey();
        if (key) {
            lastKeyPressTime = millis();
            wakeUpFromLowPowerMode();
            playBuzzerSound();

            Serial.print("Key pressed: ");
            Serial.println(key);

            if (key == 'A') {
                input = "";
                if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                    lcd.setCursor(0, 1);
                    lcd.print("Xoa nhap lieu   ");
                    xSemaphoreGive(lcdSemaphore);
                }
            } else if (key == '#') {
                if (xSemaphoreTake(passwordSemaphore, portMAX_DELAY) == pdTRUE) {
                    changeMode = true;
                    confirmOldPassword = false;
                    input = "";
                    inputReady = false;
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Doi mat khau");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.clear();
                        lcd.print("Nhap MK cu:");
                        xSemaphoreGive(lcdSemaphore);
                    }
                    xSemaphoreGive(passwordSemaphore);
                }
            } else if (key == '*') {
                Serial.print("Input: '"); Serial.print(input); Serial.println("'");
                Serial.print("Password: '"); Serial.println(password);

                if (xSemaphoreTake(passwordSemaphore, portMAX_DELAY) == pdTRUE) {
                    if (!changeMode) {
                        if (input.equals(password)) {
                            openLock();
                        } else {
                            if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                                lcd.clear();
                                lcd.print("Sai MK!");
                                xSemaphoreGive(lcdSemaphore);
                            }
                            vTaskDelay(1000 / portTICK_PERIOD_MS);
                            if (cnt == 3) {
                                digitalWrite(BUZZER_PIN, HIGH);
                                vTaskDelay(5000 / portTICK_PERIOD_MS);
                                digitalWrite(BUZZER_PIN, LOW);
                                vTaskDelay(100 / portTICK_PERIOD_MS);
                                cnt = 0;
                            }
                            Serial.println(cnt);
                            if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                                lcd.clear();
                                lcd.print("Nhap MK:");
                                xSemaphoreGive(lcdSemaphore);
                            }
                            cnt++;
                        }
                        input = "";
                    } else {
                        // Signal passwordChangeTask to process input
                        inputReady = true;
                    }
                    xSemaphoreGive(passwordSemaphore);
                }
            } else {
                if (input.length() < MAX_PASSWORD_LENGTH) {
                    input += key;
                    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                        lcd.setCursor(0, 1);
                        for (int i = 0; i < input.length(); i++) {
                            lcd.print("*");
                        }
                        for (int i = input.length(); i < MAX_PASSWORD_LENGTH; i++) {
                            lcd.print(" ");
                        }
                        xSemaphoreGive(lcdSemaphore);
                    }
                }
            }
        }

        if (isBacklightOn && (millis() - lastKeyPressTime > inactivityDelay)) {
            enterLowPowerMode();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// RFID task
void rfidTask(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(spiSemaphore, portMAX_DELAY) == pdTRUE) {
            if (rfid.PICC_IsNewCardPresent()) {
                Serial.println("Card detected");
                if (rfid.PICC_ReadCardSerial()) {
                    String uid = normalizeUID(rfid.uid.uidByte, rfid.uid.size);
                    Serial.println("UID: " + uid);

                    lastKeyPressTime = millis();
                    wakeUpFromLowPowerMode();

                    if (isValidUID(uid)) {
                        Serial.println("UID hợp lệ. Mở khóa.");
                        openLock();
                    } else {
                        Serial.println("UID không hợp lệ.");
                        if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                            lcd.clear();
                            lcd.setCursor(0, 0);
                            lcd.print("Sai The Roi!");
                            xSemaphoreGive(lcdSemaphore);
                        }
                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                        if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
                            lcd.clear();
                            lcd.setCursor(0, 0);
                            lcd.print("Nhap MK:");
                            xSemaphoreGive(lcdSemaphore);
                        }
                    }

                    rfid.PICC_HaltA();
                    rfid.PCD_StopCrypto1();
                } else {
                    Serial.println("Failed to read card serial");
                }
            }
            xSemaphoreGive(spiSemaphore);
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);
    loadPasswordFromEEPROM();
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
    Wire.begin(21, 22);

    lcd.begin(16, 2);
    lcd.init();
    lcd.backlight();
    isBacklightOn = true;

    myservo.attach(15);
    myservo.setPeriodHertz(50);
    myservo.write(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    Serial.println("Servo initialized");
    myservo.detach();

    SPI.begin();
    rfid.PCD_Init();
    rfid.PCD_SetAntennaGain(rfid.RxGain_max);
    Serial.println("RFID Ready");

    pinMode(BUZZER_PIN, OUTPUT);

    lcdSemaphore = xSemaphoreCreateBinary();
    spiSemaphore = xSemaphoreCreateBinary();
    passwordSemaphore = xSemaphoreCreateBinary();
    if (lcdSemaphore != NULL && spiSemaphore != NULL && passwordSemaphore != NULL) {
        xSemaphoreGive(lcdSemaphore);
        xSemaphoreGive(spiSemaphore);
        xSemaphoreGive(passwordSemaphore);
    }

    if (xSemaphoreTake(lcdSemaphore, portMAX_DELAY) == pdTRUE) {
        lcd.setCursor(0, 0);
        lcd.print("Smart Lock:");
        lcd.setCursor(0, 1);
        lcd.print("Nhap MK:");
        xSemaphoreGive(lcdSemaphore);
    }

    xTaskCreatePinnedToCore(keypadTask, "KeypadTask", 4096, NULL, 2, &keypadTaskHandle, 1);
    xTaskCreatePinnedToCore(rfidTask, "RFIDTask", 4096, NULL, 3, &rfidTaskHandle, 0);
    xTaskCreatePinnedToCore(blynkTask, "BlynkTask", 4096, NULL, 1, &blynkTaskHandle, 1);
    xTaskCreatePinnedToCore(passwordChangeTask, "PasswordChangeTask", 4096, NULL, 2, &passwordChangeTaskHandle, 1);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}