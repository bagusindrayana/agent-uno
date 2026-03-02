#ifndef BOT_AGENT_H
#define BOT_AGENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>
#include "AIHandler.h"

struct BotCommand {
    String command;
    String response;
};

struct BotSettings {
    String wifiSSID;
    String wifiPassword;
    String botToken;
    std::vector<BotCommand> dynamicCommands;
    AIProvider aiProvider;
    String aiApiKey;
    String aiModel;
};

class BotAgent {
public:
    BotAgent();
    void begin();
    void loop();
    void saveSettings();
    void loadSettings();
    BotSettings& getSettings() { return _settings; }

private:
    BotSettings _settings;
    WebServer _server;
    WiFiClientSecure _client;
    UniversalTelegramBot* _bot;
    AIHandler _aiHandler;
    unsigned long _lastBotCheck;
    int _botCheckInterval;

    void setupWiFi();
    void setupWebServer();
    void handleTelegramMessages(int numNewMessages);
    
    // Web Server Handlers
    void handleRoot();
    void handleSaveSettings();
    void handleGetSettings();
    void handleAddCommand();
    void handleDeleteCommand();
};

#endif
