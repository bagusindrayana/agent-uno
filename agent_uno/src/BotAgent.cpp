#include "BotAgent.h"

BotAgent::BotAgent() : _server(80), _lastBotCheck(0), _botCheckInterval(1000), _lastSystemInfoUpdate(0) {
    _bot = nullptr;
}

void BotAgent::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    loadSettings();
    loadProfile();
    setupWiFi();
    setupWebServer();
    setupNTP();

    if (_settings.botToken.length() > 0) {
        _client.setInsecure(); // Simplify SSL for ESP32
        _bot = new UniversalTelegramBot(_settings.botToken, _client);
        
        // Fetch bot information (username) immediately
        Serial.println("Fetching Bot Info...");
        if (_bot->getMe()) {
            Serial.print("Bot Username: @");
            Serial.println(_bot->userName);
        } else {
            Serial.println("Failed to fetch Bot Info. Token might be invalid.");
        }
    }
}

void BotAgent::loadProfile() {
    File file = LittleFS.open("/profile.json", "r");
    if (!file) {
        Serial.println("No profile file found. Creating default.");
        _settings.profile.botName = "ESP32 Bot Agent";
        _settings.profile.systemPrompt = "You are a helpful AI assistant running on an ESP32 microcontroller. You have access to local files and hardware.";
        saveProfile();
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Failed to read profile file");
        return;
    }

    _settings.profile.botName = doc["botName"].as<String>();
    _settings.profile.systemPrompt = doc["systemPrompt"].as<String>();
    file.close();
}

void BotAgent::saveProfile() {
    File file = LittleFS.open("/profile.json", "w");
    if (!file) {
        Serial.println("Failed to open profile file for writing");
        return;
    }

    JsonDocument doc;
    doc["botName"] = _settings.profile.botName;
    doc["systemPrompt"] = _settings.profile.systemPrompt;

    serializeJson(doc, file);
    file.close();
}

void BotAgent::setupNTP() {
    configTime(_settings.gmtOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("NTP Configured with Offset: " + String(_settings.gmtOffsetSec));
}

String BotAgent::getSystemInfo() {
    struct tm timeinfo;
    char timeStr[64];
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %H:%M:%S", &timeinfo);
    } else {
        strcpy(timeStr, "Time not synced");
    }

    unsigned long totalSeconds = millis() / 1000;
    int days = totalSeconds / 86400;
    int hours = (totalSeconds % 86400) / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    String info = "\n--- System Info ---\n";
    info += "Current Time: " + String(timeStr) + "\n";
    info += "Uptime: " + String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s\n";
    info += "Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    info += "Chip Model: " + String(ESP.getChipModel()) + "\n";
    info += "Bot Name: " + _settings.profile.botName + "\n";
    return info;
}

String BotAgent::executeTool(String functionName, String args) {
    JsonDocument doc;
    deserializeJson(doc, args);

    if (functionName == "read_file") {
        String filename = doc["filename"].as<String>();
        if (!filename.startsWith("/")) filename = "/" + filename;
        File file = LittleFS.open(filename, "r");
        if (!file) return "Error: File not found: " + filename;
        String content = file.readString();
        file.close();
        return content;
    } 
    else if (functionName == "write_file") {
        String filename = doc["filename"].as<String>();
        if (!filename.startsWith("/")) filename = "/" + filename;
        String content = doc["content"].as<String>();
        File file = LittleFS.open(filename, "w");
        if (!file) return "Error: Could not open file for writing: " + filename;
        file.print(content);
        file.close();
        
        // If we updated profile.json, reload it
        if (filename == "/profile.json") {
            loadProfile();
        }
        return "Success: File written: " + filename;
    }

    return "Error: Unknown tool";
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
    _settings.aiProvider = (AIProvider)doc["aiProvider"].as<int>();
    _settings.aiApiKey = doc["aiApiKey"].as<String>();
    _settings.aiModel = doc["aiModel"].as<String>();
    _settings.gmtOffsetSec = doc["gmtOffsetSec"] | 25200; // Default to UTC+7 (Jakarta)

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
    doc["aiProvider"] = (int)_settings.aiProvider;
    doc["aiApiKey"] = _settings.aiApiKey;
    doc["aiModel"] = _settings.aiModel;
    doc["gmtOffsetSec"] = _settings.gmtOffsetSec;

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

        // Serial Log for incoming message
        Serial.print("\n--- New Telegram Message ---\n");
        Serial.print("From: "); Serial.println(fromName);
        Serial.print("ChatID: "); Serial.println(chatId);
        Serial.print("Text: "); Serial.println(text);
        Serial.println("---------------------------");

        bool matched = false;
        String botName = "@" + _bot->userName;

        // Default Commands
        if (text == "/status" || text == "/status" + botName) {
            Serial.println("Command /status detected!");
            unsigned long totalSeconds = millis() / 1000;
            int days = totalSeconds / 86400;
            int hours = (totalSeconds % 86400) / 3600;
            int minutes = (totalSeconds % 3600) / 60;
            int seconds = totalSeconds % 60;

            // Get Current Time
            struct tm timeinfo;
            char timeString[64] = "Not Synchronized";
            if (getLocalTime(&timeinfo)) {
                strftime(timeString, sizeof(timeString), "%A, %d %B %Y %H:%M:%S", &timeinfo);
            }

            String status = "📊 <b>ESP32 Status</b>\n\n";
            status += "🔹 <b>Time:</b> " + String(timeString) + "\n";
            status += "🔹 <b>Chip:</b> " + String(ESP.getChipModel()) + "\n";
            status += "🔹 <b>Heap:</b> " + String(ESP.getFreeHeap() / 1024) + " KB / " + String(ESP.getHeapSize() / 1024) + " KB\n";
            status += "🔹 <b>Flash:</b> " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB\n";
            status += "🔹 <b>Uptime:</b> " + String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s\n";
            status += "🔹 <b>Bot Name:</b> @" + _bot->userName + "\n";
            
            bool success = _bot->sendMessage(chatId, status, "HTML");
            if (success) Serial.println("Status message sent!");
            else {
                Serial.println("Failed to send status message!");
                Serial.print("Message was: "); Serial.println(status);
            }
            matched = true;
        } 
        else if (text == "/help" || text == "/help" + botName || text == "/start" || text == "/start" + botName) {
            Serial.println("Command /help or /start detected!");
            String help = "🆘 <b>Available Commands</b>\n\n";
            help += "• <code>/status</code> - ESP32 stats & uptime\n";
            help += "• <code>/help</code> - Show this list\n\n";
            
            if (_settings.dynamicCommands.size() > 0) {
                help += "⚙️ <b>Dynamic Commands</b>\n";
                for (const auto& cmd : _settings.dynamicCommands) {
                    help += "• <code>/" + cmd.command + "</code>\n";
                }
            } else {
                help += "<i>No dynamic commands configured.</i>";
            }
            
            _bot->sendMessage(chatId, help, "HTML");
            matched = true;
        }

        else if (text == "/clear" || text == "/clear" + botName) {
            Serial.println("Command /clear detected!");
            _aiHandler.clearHistory();
            _bot->sendMessage(chatId, "🧹 <b>AI Chat History Cleared!</b>", "HTML");
            matched = true;
        }

        // Dynamic Commands (only if not matched by default ones)
        if (!matched) {
            for (const auto& cmd : _settings.dynamicCommands) {
                String cmdStr = "/" + cmd.command;
                if (text == cmdStr || text == cmdStr + botName) {
                    _bot->sendMessage(chatId, cmd.response, "");
                    matched = true;
                    break;
                }
            }
        }

        if (!matched) {
            String fullSystemPrompt = _settings.profile.systemPrompt + "\n\n" + getSystemInfo();
            
            // 1. Initial Call
            AIResponse aiRes = _aiHandler.getResponse(text, fullSystemPrompt, _settings.aiProvider, _settings.aiApiKey, _settings.aiModel);
            
            // 2. Persistent User Message
            _aiHandler.addMessage({ "user", text, "", "", "" });

            int maxTurns = 5; 
            for (int turn = 0; turn < maxTurns; turn++) {
                if (aiRes.toolCalls.size() > 0) {
                    // 3. Add assistant's tool call to persistent history
                    _aiHandler.addMessage({ "assistant", aiRes.content, "", "", aiRes.toolCallsJson }); 
                    
                    for (const auto& tc : aiRes.toolCalls) {
                        String result = executeTool(tc.name, tc.arguments);
                        // 4. Add tool result to persistent history
                        _aiHandler.addMessage({ "tool", result, tc.name, tc.id, "" });
                    }
                    
                    // 5. Follow-up Call (without adding new user message)
                    // We need a way to call AI with current history only.
                    // Let's call getResponse with empty user message? No, that adds an empty user msg.
                    // I'll add a continueResponse() method to AIHandler.
                    aiRes = _aiHandler.getResponse("", fullSystemPrompt, _settings.aiProvider, _settings.aiApiKey, _settings.aiModel);
                } else {
                    // Final answer
                    if (aiRes.content.length() > 0) {
                        _aiHandler.addMessage({ "assistant", aiRes.content, "", "", "" });
                        _bot->sendMessage(chatId, aiRes.content, "");
                    } else {
                        _bot->sendMessage(chatId, "I executed the tools but have nothing more to say.", "");
                    }
                    matched = true;
                    break; 
                }
            }

            if (!matched) {
                _bot->sendMessage(chatId, "I seem to be stuck in a loop of tool calls. Please try a different command.", "");
            }
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
    
    html += "<h2>AI Chatbot Settings</h2>";
    html += "AI Provider: <select name='ai_provider' style='display:block;width:100%;margin:10px 0;padding:10px;'>";
    html += "<option value='0'" + String(_settings.aiProvider == NONE ? " selected" : "") + ">None</option>";
    html += "<option value='1'" + String(_settings.aiProvider == OPENAI ? " selected" : "") + ">OpenAI</option>";
    html += "<option value='2'" + String(_settings.aiProvider == OPENROUTER ? " selected" : "") + ">OpenRouter</option>";
    html += "<option value='3'" + String(_settings.aiProvider == GEMINI ? " selected" : "") + ">Gemini</option>";
    html += "<option value='4'" + String(_settings.aiProvider == CLAUDE ? " selected" : "") + ">Claude</option>";
    html += "</select>";
    html += "AI Model: <input name='ai_model' value='" + _settings.aiModel + "' placeholder='e.g. gpt-4, gemini-pro, etc.'>";
    html += "AI API Key: <input name='ai_key' value='" + _settings.aiApiKey + "'>";
    
    html += "<h2>Time Settings</h2>";
    html += "Timezone (GMT Offset): <select name='gmt_offset' style='display:block;width:100%;margin:10px 0;padding:10px;'>";
    for (int i = -12; i <= 14; i++) {
        long offset = i * 3600;
        String label = "GMT" + (i >= 0 ? "+" + String(i) : String(i));
        html += "<option value='" + String(offset) + "'" + String(_settings.gmtOffsetSec == offset ? " selected" : "") + ">" + label + "</option>";
    }
    html += "</select>";
    
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
    _settings.aiProvider = (AIProvider)_server.arg("ai_provider").toInt();
    _settings.aiApiKey = _server.arg("ai_key");
    _settings.aiModel = _server.arg("ai_model");
    _settings.gmtOffsetSec = _server.arg("gmt_offset").toInt();
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
