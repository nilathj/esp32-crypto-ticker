#include <Arduino.h>
#include "WiFi.h"
#include "NotoSansBold15.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <Ticker.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "Free_Fonts.h"
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

#define AA_FONT_SMALL NotoSansBold15
#define WIFI_SSID "XXXX"
#define WIFI_PASSWORD "XXXX"

#define MQTT_HOST IPAddress(xxx, xxx, xxx, xxx)
#define MQTT_PORT 1883
#define MQTT_PRICE_TOPIC "crypto/prices"

TFT_eSPI tft = TFT_eSPI();
const char *MINUS = "-";
AsyncMqttClient mqttClient;
TimerHandle_t wifiReconnectTimer;
TimerHandle_t mqttReconnectTimer;

void showQuotes() {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);

    tft.setTextSize(1);
    tft.drawString("Default case..", tft.width() / 2, tft.height() / 2);
}

void connectToWifi() {
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void header(const char *txt, uint16_t color) {
    tft.fillScreen(color);
    tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
    tft.fillRect(0, 0, 320, 20, TFT_GREENYELLOW);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(txt, 120, 2);
}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case SYSTEM_EVENT_WIFI_READY:
        Serial.println("WiFi ready");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        header("WIFI Connected..", TFT_BLACK);
        connectToMqtt();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
        xTimerStart(wifiReconnectTimer, 0);
        header("WIFI Disconnected..", TFT_BLACK);
        break;
    }
}

void onMqttConnect(bool sessionPresent) {
    Serial.println("Connected to MQTT.");
    Serial.print("Session present: ");
    Serial.println(sessionPresent);
    uint16_t packetIdSub = mqttClient.subscribe(MQTT_PRICE_TOPIC, 1);
    Serial.print("Subscribing at QoS 1, packetId: ");
    Serial.println(packetIdSub);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.println("Disconnected from MQTT.");

    if (WiFi.isConnected()) {
        xTimerStart(mqttReconnectTimer, 0);
    }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
    Serial.println("Subscribe acknowledged.");
    Serial.print("  packetId: ");
    Serial.println(packetId);
    header("MQTT Connected..", TFT_BLACK);
}

void onMqttUnsubscribe(uint16_t packetId) {
    Serial.println("Unsubscribe acknowledged.");
    Serial.print("  packetId: ");
    Serial.println(packetId);
}

void displayColumn(const char *colText, int xpos, int ypos) {
    int padding = tft.textWidth(colText, GFXFF);
    tft.setTextPadding(padding);
    tft.drawString(colText, xpos, ypos, GFXFF); // Draw the text string in the selected GFX free font
}

bool beginsWith(const char *a, const char *b) {
    if (strncmp(a, b, strlen(b)) == 0)
        return 1;
    return 0;
}

void onMqttMessage(char *topic, char *payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    header(doc["time"], TFT_BLACK);

    JsonArray coins = doc["coins"];
    const int coinsSize = coins.size();

    int xcol1 = 0;          // coin name
    int xcol2 = xcol1 + 58; // coin price
    int xcol3 = xcol2 + 70; // - sign
    int xcol4 = xcol3 + 4;  // change %
    int xcol5 = xcol4 + 50; // volume B or M
    int ypos = 35;

    for (int i = 0; i < coinsSize; i++) {
        const char *coinName = coins[i]["name"];
        const char *coinPrice = coins[i]["price"];
        const char *coinChange = coins[i]["price_change_24h"];
        const char *vol = coins[i]["vol"];

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextDatum(TL_DATUM); // Centre text on x,y position
        
        displayColumn(coinName, xcol1, ypos);
        displayColumn(coinPrice, xcol2, ypos);
        displayColumn(vol, xcol5, ypos);

        if (beginsWith(coinChange, MINUS)) {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            displayColumn(MINUS, xcol3, ypos);
            const char *coinChangeValue = &coinChange[1];
            displayColumn(coinChangeValue, xcol4, ypos);
        } else {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            displayColumn(coinChange, xcol4, ypos);
        }

        ypos += tft.fontHeight(GFXFF) + 8;
    }
}

void onMqttPublish(uint16_t packetId) {
    Serial.println("Publish acknowledged.");
    Serial.print("  packetId: ");
    Serial.println(packetId);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(0, 0);
    tft.loadFont(AA_FONT_SMALL);

    mqttReconnectTimer =
        xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                     reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    wifiReconnectTimer =
        xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                     reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

    WiFi.onEvent(WiFiEvent);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);

    connectToWifi();
}

void loop() {
    // Nothing in the loop, we subscribed to MQTT messages in setup.  
    // When an MQTT message arrives on the subscribed topic it is displayed.
}