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
    else if (functionName == "http_request") {
        String url = doc["url"].as<String>();
        String method = doc["method"].as<String>();
        String headersStr = doc["headers"].as<String>();
        String body = doc["body"].as<String>();

        if (url.length() == 0 || method.length() == 0) {
            return "Error: 'url' and 'method' are required for http_request.";
        }

        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure(); // Allow insecure connections for simplicity

        if (url.startsWith("https://")) {
            http.begin(client, url);
        } else {
            http.begin(url);
        }

        // Parse and add headers
        if (headersStr.length() > 0) {
            JsonDocument headersDoc;
            deserializeJson(headersDoc, headersStr);
            JsonObject headersObj = headersDoc.as<JsonObject>();
            for (JsonPair kv : headersObj) {
                http.addHeader(kv.key().c_str(), kv.value().as<String>());
            }
        }

        int httpCode = 0;
        if (method.equalsIgnoreCase("GET")) {
            httpCode = http.GET();
        } else if (method.equalsIgnoreCase("POST")) {
            httpCode = http.POST(body);
        } else {
            http.end();
            return "Error: Unsupported HTTP method: " + method;
        }

        if (httpCode > 0) {
            String payload = http.getString();
            http.end();
            // Truncate if too long to avoid overwhelming the AI context
            if (payload.length() > 2000) {
                payload = payload.substring(0, 2000) + "\n... (truncated)";
            }
            return "Status: " + String(httpCode) + ", Response: " + payload;
        } else {
            String errorMsg = "Error: HTTP request failed. Code: " + String(httpCode) + ", " + http.errorToString(httpCode);
            http.end();
            return errorMsg;
        }
    }
    else if (functionName == "web_search") {
        String query = doc["query"].as<String>();
        if (query.length() == 0) return "Error: 'query' is required for web_search.";
        if (_settings.searchProvider == SEARCH_NONE || _settings.searchApiKey.length() == 0) {
            return "Error: Web Search provider or API key is not configured in settings.";
        }

        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure();

        int httpCode = 0;
        String payload = "";

        if (_settings.searchProvider == TAVILY) {
            http.begin(client, "https://api.tavily.com/search");
            http.addHeader("Content-Type", "application/json");
            JsonDocument searchDoc;
            searchDoc["api_key"] = _settings.searchApiKey;
            searchDoc["query"] = query;
            String json;
            serializeJson(searchDoc, json);
            httpCode = http.POST(json);
        }
        else if (_settings.searchProvider == BRAVE) {
            String url = "https://api.search.brave.com/res/v1/web/search?q=" + urlEncode(query);
            http.begin(client, url);
            http.addHeader("X-Subscription-Token", _settings.searchApiKey);
            httpCode = http.GET();
        }
        else if (_settings.searchProvider == DUCKDUCKGO) {
            String url = "https://www.searchapi.io/api/v1/search?engine=duckduckgo&q=" + urlEncode(query) + "&api_key=" + _settings.searchApiKey;
            http.begin(client, url);
            httpCode = http.GET();
        }

        if (httpCode > 0) {
            payload = http.getString();
            http.end();
            if (payload.length() > 3000) payload = payload.substring(0, 3000) + "... (truncated)";
            return payload;
        } else {
            String err = "Error: Search failed (" + String(httpCode) + "): " + http.errorToString(httpCode);
            http.end();
            return err;
        }
    }
    else if (functionName == "add_cron_job") {
        int interval = doc["intervalMinutes"] | 0;
        String scheduledTime = doc["scheduledTime"] | "";
        String prompt = doc["prompt"].as<String>();
        
        if (interval > 0 && interval < 5) interval = 5; // Minimal 5 menit
        
        int newId = 1;
        if (_settings.cronJobs.size() > 0) {
            newId = _settings.cronJobs.back().id + 1;
        }
        
        _settings.cronJobs.push_back({newId, interval, scheduledTime, prompt, millis(), -1});
        saveSettings();
        
        String msg = "Cron Job #" + String(newId) + " added.";
        if (scheduledTime.length() > 0) msg += " Time: " + scheduledTime;
        else msg += " Interval: " + String(interval) + " min";
        return msg;
    }
    else if (functionName == "list_cron_jobs") {
        if (_settings.cronJobs.empty()) return "No cron jobs scheduled.";
        String list = "Scheduled Cron Jobs:\n";
        for (const auto& job : _settings.cronJobs) {
            list += "- #" + String(job.id) + ": ";
            if (job.scheduledTime.length() > 0) list += "Time " + job.scheduledTime;
            else list += "Every " + String(job.intervalMinutes) + "m";
            list += " | Prompt: " + job.prompt + "\n";
        }
        return list;
    }
    else if (functionName == "delete_cron_job") {
        int id = doc["id"].as<int>();
        bool found = false;
        for (auto it = _settings.cronJobs.begin(); it != _settings.cronJobs.end(); ++it) {
            if (it->id == id) {
                _settings.cronJobs.erase(it);
                found = true;
                break;
            }
        }
        if (found) {
            saveSettings();
            return "Cron Job #" + String(id) + " deleted.";
        }
        return "Error: Cron Job #" + String(id) + " not found.";
    }

    return "Error: Unknown tool";
}

// Simple URL encode helper
String BotAgent::urlEncode(String str) {
    String encoded = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (isalnum(c)) {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) code0 = c - 10 + 'A';
            encoded += '%';
            encoded += code0;
            encoded += code1;
        }
    }
    return encoded;
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
    _settings.searchProvider = (SearchProvider)(doc["searchProvider"] | 0);
    _settings.searchApiKey = doc["searchApiKey"].as<String>();
    _settings.adminChatId = doc["adminChatId"].as<String>();

    JsonArray cmds = doc["commands"].as<JsonArray>();
    _settings.dynamicCommands.clear();
    for (JsonObject cmd : cmds) {
        _settings.dynamicCommands.push_back({cmd["command"].as<String>(), cmd["response"].as<String>()});
    }

    JsonArray jobs = doc["cronJobs"].as<JsonArray>();
    _settings.cronJobs.clear();
    for (JsonObject job : jobs) {
        _settings.cronJobs.push_back({
            job["id"].as<int>(),
            job["intervalMinutes"].as<int>(),
            job["scheduledTime"] | "",
            job["prompt"].as<String>(),
            0, // reset lastRun on boot
            -1 // reset lastRunDay
        });
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
    doc["searchProvider"] = (int)_settings.searchProvider;
    doc["searchApiKey"] = _settings.searchApiKey;
    doc["adminChatId"] = _settings.adminChatId;

    JsonArray cmds = doc.createNestedArray("commands");
    for (const auto& cmd : _settings.dynamicCommands) {
        JsonObject obj = cmds.createNestedObject();
        obj["command"] = cmd.command;
        obj["response"] = cmd.response;
    }

    JsonArray jobs = doc.createNestedArray("cronJobs");
    for (const auto& job : _settings.cronJobs) {
        JsonObject obj = jobs.createNestedObject();
        obj["id"] = job.id;
        obj["intervalMinutes"] = job.intervalMinutes;
        obj["scheduledTime"] = job.scheduledTime;
        obj["prompt"] = job.prompt;
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
    checkCronJobs();

    if (_bot && millis() > _lastBotCheck + _botCheckInterval) {
        int numNewMessages = _bot->getUpdates(_bot->last_message_received + 1);
        while (numNewMessages) {
            handleTelegramMessages(numNewMessages);
            numNewMessages = _bot->getUpdates(_bot->last_message_received + 1);
        }
        _lastBotCheck = millis();
    }
}

void BotAgent::checkCronJobs() {
    if (_settings.adminChatId.length() == 0) return;

    struct tm timeinfo;
    bool hasTime = getLocalTime(&timeinfo);
    char currentHM[6];
    if (hasTime) strftime(currentHM, sizeof(currentHM), "%H:%M", &timeinfo);

    for (auto& job : _settings.cronJobs) {
        bool shouldTrigger = false;

        // Cek berdasarkan jam spesifik (misal 03:00)
        if (hasTime && job.scheduledTime.length() > 0) {
            if (job.scheduledTime == String(currentHM) && job.lastRunDay != timeinfo.tm_mday) {
                shouldTrigger = true;
                job.lastRunDay = timeinfo.tm_mday;
            }
        } 
        // Cek berdasarkan interval menit
        else if (job.intervalMinutes >= 5) {
            unsigned long intervalMillis = (unsigned long)job.intervalMinutes * 60000;
            if (millis() - job.lastRunMillis >= intervalMillis) {
                shouldTrigger = true;
                job.lastRunMillis = millis();
            }
        }

        if (shouldTrigger) {
            Serial.print("Executing Cron Job ID: "); Serial.println(job.id);
            String fullSystemPrompt = _settings.profile.systemPrompt + "\n\n" + getSystemInfo();
            AIResponse aiRes = _aiHandler.getResponse(job.prompt, fullSystemPrompt, _settings.aiProvider, _settings.aiApiKey, _settings.aiModel);
            
            if (aiRes.content.length() > 0) {
                String notification = "🕒 <b>Cron Job #" + String(job.id) + " Triggered</b>\n\n" + aiRes.content;
                _bot->sendMessage(_settings.adminChatId, notification, "HTML");
            }
        }
    }
}

void BotAgent::handleTelegramMessages(int numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
        String text = _bot->messages[i].text;
        String chatId = _bot->messages[i].chat_id;
        String fromName = _bot->messages[i].from_name;

        // Set adminChatId to the first person who talks to it if not set
        if (_settings.adminChatId.length() == 0) {
            _settings.adminChatId = chatId;
            saveSettings();
        }

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
    
    html += "<h2>Web Search Settings</h2>";
    html += "Search Provider: <select name='search_provider' style='display:block;width:100%;margin:10px 0;padding:10px;'>";
    html += "<option value='0'" + String(_settings.searchProvider == SEARCH_NONE ? " selected" : "") + ">None</option>";
    html += "<option value='1'" + String(_settings.searchProvider == TAVILY ? " selected" : "") + ">Tavily</option>";
    html += "<option value='2'" + String(_settings.searchProvider == BRAVE ? " selected" : "") + ">Brave Search</option>";
    html += "<option value='3'" + String(_settings.searchProvider == DUCKDUCKGO ? " selected" : "") + ">DuckDuckGo (SearchAPI.io)</option>";
    html += "</select>";
    html += "Search API Key: <input name='search_key' value='" + _settings.searchApiKey + "'>";

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
    _settings.searchProvider = (SearchProvider)_server.arg("search_provider").toInt();
    _settings.searchApiKey = _server.arg("search_key");
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
