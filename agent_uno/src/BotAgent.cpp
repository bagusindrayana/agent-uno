#include "BotAgent.h"

BotAgent::BotAgent() : _server(80), _lastBotCheck(0), _botCheckInterval(1000) {
    _bot = nullptr;
}

void BotAgent::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    loadSettings();
    setupWiFi();
    setupWebServer();

    if (_settings.botToken.length() > 0) {
        _client.setInsecure(); // Simplify SSL for ESP32
        _bot = new UniversalTelegramBot(_settings.botToken, _client);
    }
}

void BotAgent::loadSettings() {
    File file = LittleFS.open("/settings.json", "r");
    if (!file) {
        Serial.println("No settings file found. Using defaults.");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Failed to read settings file");
        return;
    }

    _settings.wifiSSID = doc["wifiSSID"].as<String>();
    _settings.wifiPassword = doc["wifiPassword"].as<String>();
    _settings.botToken = doc["botToken"].as<String>();

    JsonArray cmds = doc["commands"].as<JsonArray>();
    _settings.dynamicCommands.clear();
    for (JsonObject cmd : cmds) {
        _settings.dynamicCommands.push_back({cmd["command"].as<String>(), cmd["response"].as<String>()});
    }

    file.close();
}

void BotAgent::saveSettings() {
    File file = LittleFS.open("/settings.json", "w");
    if (!file) {
        Serial.println("Failed to open settings file for writing");
        return;
    }

    JsonDocument doc;
    doc["wifiSSID"] = _settings.wifiSSID;
    doc["wifiPassword"] = _settings.wifiPassword;
    doc["botToken"] = _settings.botToken;

    JsonArray cmds = doc.createNestedArray("commands");
    for (const auto& cmd : _settings.dynamicCommands) {
        JsonObject obj = cmds.createNestedObject();
        obj["command"] = cmd.command;
        obj["response"] = cmd.response;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to file");
    }
    file.close();
}

void BotAgent::setupWiFi() {
    if (_settings.wifiSSID.length() > 0) {
        Serial.print("Connecting to WiFi: ");
        Serial.println(_settings.wifiSSID);
        WiFi.begin(_settings.wifiSSID.c_str(), _settings.wifiPassword.c_str());

        int count = 0;
        while (WiFi.status() != WL_CONNECTED && count < 20) {
            delay(500);
            Serial.print(".");
            count++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi connected");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            return;
        }
    }

    // If WiFi fails or not configured, start AP mode
    Serial.println("\nWiFi failed. Starting Access Point...");
    WiFi.softAP("ESP32-Bot-Agent", "12345678");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
}

void BotAgent::setupWebServer() {
    _server.on("/", HTTP_GET, std::bind(&BotAgent::handleRoot, this));
    _server.on("/save", HTTP_POST, std::bind(&BotAgent::handleSaveSettings, this));
    _server.on("/settings", HTTP_GET, std::bind(&BotAgent::handleGetSettings, this));
    _server.on("/add_command", HTTP_POST, std::bind(&BotAgent::handleAddCommand, this));
    _server.on("/delete_command", HTTP_POST, std::bind(&BotAgent::handleDeleteCommand, this));
    
    _server.begin();
    Serial.println("Web server started");
}

void BotAgent::loop() {
    _server.handleClient();

    if (_bot && millis() > _lastBotCheck + _botCheckInterval) {
        int numNewMessages = _bot->getUpdates(_bot->last_message_received + 1);
        while (numNewMessages) {
            handleTelegramMessages(numNewMessages);
            numNewMessages = _bot->getUpdates(_bot->last_message_received + 1);
        }
        _lastBotCheck = millis();
    }
}

void BotAgent::handleTelegramMessages(int numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
        String text = _bot->messages[i].text;
        String chatId = _bot->messages[i].chat_id;
        String fromName = _bot->messages[i].from_name;

        bool matched = false;

        // Default Commands
        if (text == "/status") {
            unsigned long totalSeconds = millis() / 1000;
            int days = totalSeconds / 86400;
            int hours = (totalSeconds % 86400) / 3600;
            int minutes = (totalSeconds % 3600) / 60;
            int seconds = totalSeconds % 60;

            String status = "📊 *ESP32 Status*\n\n";
            status += "🔹 *Chip:* " + String(ESP.getChipModel()) + "\n";
            status += "🔹 *Heap:* " + String(ESP.getFreeHeap() / 1024) + " KB / " + String(ESP.getHeapSize() / 1024) + " KB\n";
            status += "🔹 *Flash:* " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB\n";
            status += "🔹 *Uptime:* " + String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s\n";
            status += "🔹 *Bot Name:* " + _bot->userName + "\n"; // userName is populated after first getUpdates or getMe
            
            _bot->sendMessage(chatId, status, "Markdown");
            matched = true;
        } 
        else if (text == "/help" || text == "/start") {
            String help = "🆘 *Available Commands*\n\n";
            help += "• `/status` - ESP32 stats & uptime\n";
            help += "• `/help` - Show this list\n\n";
            
            if (_settings.dynamicCommands.size() > 0) {
                help += "⚙️ *Dynamic Commands*\n";
                for (const auto& cmd : _settings.dynamicCommands) {
                    help += "• `/" + cmd.command + "`\n";
                }
            } else {
                help += "_No dynamic commands configured._";
            }
            
            _bot->sendMessage(chatId, help, "Markdown");
            matched = true;
        }

        // Dynamic Commands (only if not matched by default ones)
        if (!matched) {
            for (const auto& cmd : _settings.dynamicCommands) {
                if (text == "/" + cmd.command) {
                    _bot->sendMessage(chatId, cmd.response, "");
                    matched = true;
                    break;
                }
            }
        }

        if (!matched) {
            _bot->sendMessage(chatId, "Unknown command. Use /help to see available commands.", "");
        }
    }
}

void BotAgent::handleRoot() {
    String html = "<html><head><title>ESP32 Bot Config</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:20px;max-width:600px;margin:auto;}input,button{display:block;width:100%;margin:10px 0;padding:10px;}button{background:#007bff;color:white;border:none;cursor:pointer;}</style>";
    html += "</head><body>";
    html += "<h1>ESP32 Bot Agent</h1>";
    html += "<form action='/save' method='POST'>";
    html += "WiFi SSID: <input name='ssid' value='" + _settings.wifiSSID + "'>";
    html += "WiFi Password: <input name='password' type='password' value='" + _settings.wifiPassword + "'>";
    html += "Bot Token: <input name='token' value='" + _settings.botToken + "'>";
    html += "<button type='submit'>Save & Restart</button></form>";
    
    html += "<h2>Commands</h2>";
    html += "<div id='commands'>";
    for (size_t i = 0; i < _settings.dynamicCommands.size(); i++) {
        html += "<div><b>/" + _settings.dynamicCommands[i].command + "</b>: " + _settings.dynamicCommands[i].response;
        html += " <form action='/delete_command' method='POST' style='display:inline;'><input type='hidden' name='index' value='" + String(i) + "'><button style='width:auto;display:inline;padding:2px 5px;'>Del</button></form></div>";
    }
    html += "</div>";
    
    html += "<h3>Add Command</h3>";
    html += "<form action='/add_command' method='POST'>";
    html += "Command: <input name='cmd' placeholder='e.g. info'>";
    html += "Response: <input name='resp' placeholder='e.g. System is OK'>";
    html += "<button type='submit'>Add</button></form>";
    
    html += "</body></html>";
    _server.send(200, "text/html", html);
}

void BotAgent::handleSaveSettings() {
    _settings.wifiSSID = _server.arg("ssid");
    _settings.wifiPassword = _server.arg("password");
    _settings.botToken = _server.arg("token");
    saveSettings();
    _server.send(200, "text/html", "Settings saved. Restarting... <script>setTimeout(()=>location.href='/', 3000);</script>");
    delay(2000);
    ESP.restart();
}

void BotAgent::handleGetSettings() {
    JsonDocument doc;
    doc["wifiSSID"] = _settings.wifiSSID;
    doc["botToken"] = _settings.botToken;
    JsonArray cmds = doc.createNestedArray("commands");
    for (const auto& cmd : _settings.dynamicCommands) {
        JsonObject obj = cmds.createNestedObject();
        obj["command"] = cmd.command;
        obj["response"] = cmd.response;
    }
    String json;
    serializeJson(doc, json);
    _server.send(200, "application/json", json);
}

void BotAgent::handleAddCommand() {
    String cmd = _server.arg("cmd");
    String resp = _server.arg("resp");
    if (cmd.length() > 0 && resp.length() > 0) {
        _settings.dynamicCommands.push_back({cmd, resp});
        saveSettings();
    }
    _server.sendHeader("Location", "/");
    _server.send(303);
}

void BotAgent::handleDeleteCommand() {
    int index = _server.arg("index").toInt();
    if (index >= 0 && index < _settings.dynamicCommands.size()) {
        _settings.dynamicCommands.erase(_settings.dynamicCommands.begin() + index);
        saveSettings();
    }
    _server.sendHeader("Location", "/");
    _server.send(303);
}
