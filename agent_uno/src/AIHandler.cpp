#include "AIHandler.h"

AIHandler::AIHandler() {}

AIResponse AIHandler::getResponse(String userMessage, String systemPrompt, AIProvider provider, String apiKey, String model) {
    if (apiKey.length() == 0 || provider == NONE) return {"", {}};

    // Prepare full message list with system prompt
    std::vector<AIMessage> fullMessages;
    if (systemPrompt.length() > 0) {
        fullMessages.push_back({ "system", systemPrompt, "", "", "" });
    }
    for (const auto& msg : _history) {
        fullMessages.push_back(msg);
    }
    
    // Add the current user message if provided (don't add to persistent history yet, BotAgent will do it)
    if (userMessage.length() > 0) {
        fullMessages.push_back({ "user", userMessage, "", "", "" });
    }

    AIResponse res;
    switch (provider) {
        case OPENAI: res = callOpenAI(fullMessages, apiKey, model); break;
        case OPENROUTER: res = callOpenRouter(fullMessages, apiKey, model); break;
        case GEMINI: res = callGemini(fullMessages, apiKey, model); break;
        case CLAUDE: res = callClaude(fullMessages, apiKey, model); break;
        default: return {"", {}};
    }
    
    return res;
}

void AIHandler::addMessage(AIMessage msg) {
    _history.push_back(msg);
    if (_history.size() > _maxHistory) {
        _history.erase(_history.begin());
    }
}

void AIHandler::clearHistory() {
    _history.clear();
    Serial.println("AI Chat History Cleared");
}

static void addTools(JsonDocument& doc) {
    JsonArray tools = doc.createNestedArray("tools");
    
    // Read File Tool
    JsonObject tool1 = tools.createNestedObject();
    tool1["type"] = "function";
    JsonObject func1 = tool1.createNestedObject("function");
    func1["name"] = "read_file";
    func1["description"] = "Read content from a local file on ESP32 LittleFS";
    JsonObject params1 = func1.createNestedObject("parameters");
    params1["type"] = "object";
    JsonObject props1 = params1.createNestedObject("properties");
    JsonObject filename1 = props1.createNestedObject("filename");
    filename1["type"] = "string";
    filename1["description"] = "The name of the file to read (e.g. /profile.json)";
    JsonArray req1 = params1.createNestedArray("required");
    req1.add("filename");

    // Write File Tool
    JsonObject tool2 = tools.createNestedObject();
    tool2["type"] = "function";
    JsonObject func2 = tool2.createNestedObject("function");
    func2["name"] = "write_file";
    func2["description"] = "Write content to a local file on ESP32 LittleFS";
    JsonObject params2 = func2.createNestedObject("parameters");
    params2["type"] = "object";
    JsonObject props2 = params2.createNestedObject("properties");
    JsonObject filename2 = props2.createNestedObject("filename");
    filename2["type"] = "string";
    JsonObject content2 = props2.createNestedObject("content");
    content2["type"] = "string";
    JsonArray req2 = params2.createNestedArray("required");
    req2.add("filename");
    req2.add("content");
}

AIResponse AIHandler::callOpenAI(std::vector<AIMessage> messages, String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.openai.com/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + apiKey);

    JsonDocument doc;
    doc["model"] = model.length() > 0 ? model : "gpt-3.5-turbo";
    JsonArray msgs = doc.createNestedArray("messages");
    for (const auto& msg : messages) {
        JsonObject m = msgs.createNestedObject();
        m["role"] = msg.role;
        if (msg.content.length() > 0) {
            m["content"] = msg.content;
        } else {
            m["content"] = nullptr; // OpenAI requires content to be null if tool_calls are present
        }

        if (msg.role == "assistant" && msg.tool_calls_json.length() > 0) {
            JsonDocument tcDoc;
            deserializeJson(tcDoc, msg.tool_calls_json);
            m["tool_calls"] = tcDoc.as<JsonArray>();
        }

        if (msg.role == "tool") {
            m["tool_call_id"] = msg.tool_call_id;
            m["name"] = msg.name;
        }
    }

    addTools(doc);

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    AIResponse result = {"", {}};
    if (httpResponseCode > 0) {
        String response = http.getString();
        JsonDocument resDoc;
        deserializeJson(resDoc, response);
        
        JsonObject choice = resDoc["choices"][0];
        JsonObject resMsg = choice["message"];
        
        if (resMsg.containsKey("content") && !resMsg["content"].isNull()) {
            result.content = resMsg["content"].as<String>();
        }

        if (resMsg.containsKey("tool_calls")) {
            JsonArray toolCalls = resMsg["tool_calls"].as<JsonArray>();
            serializeJson(toolCalls, result.toolCallsJson);
            for (JsonObject tc : toolCalls) {
                ToolCall t;
                t.id = tc["id"].as<String>();
                t.name = tc["function"]["name"].as<String>();
                t.arguments = tc["function"]["arguments"].as<String>();
                result.toolCalls.push_back(t);
            }
        }
    } else {
        result.content = "Error: OpenAI request failed (" + String(httpResponseCode) + ")";
    }
    http.end();
    return result;
}

AIResponse AIHandler::callOpenRouter(std::vector<AIMessage> messages, String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://openrouter.ai/api/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + apiKey);
    http.addHeader("HTTP-Referer", "http://esp32-agent.local");

    JsonDocument doc;
    doc["model"] = model.length() > 0 ? model : "google/gemini-flash-1.5";
    JsonArray msgs = doc.createNestedArray("messages");
    for (const auto& msg : messages) {
        JsonObject m = msgs.createNestedObject();
        m["role"] = msg.role;
        if (msg.content.length() > 0) {
            m["content"] = msg.content;
        } else {
            m["content"] = nullptr;
        }

        if (msg.role == "assistant" && msg.tool_calls_json.length() > 0) {
            JsonDocument tcDoc;
            deserializeJson(tcDoc, msg.tool_calls_json);
            m["tool_calls"] = tcDoc.as<JsonArray>();
        }

        if (msg.role == "tool") {
            m["tool_call_id"] = msg.tool_call_id;
            m["name"] = msg.name;
        }
    }

    addTools(doc);

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    AIResponse result = {"", {}};
    if (httpResponseCode > 0) {
        String response = http.getString();
        JsonDocument resDoc;
        deserializeJson(resDoc, response);
        
        JsonObject choice = resDoc["choices"][0];
        JsonObject resMsg = choice["message"];
        
        if (resMsg.containsKey("content") && !resMsg["content"].isNull()) {
            result.content = resMsg["content"].as<String>();
        }

        if (resMsg.containsKey("tool_calls")) {
            JsonArray toolCalls = resMsg["tool_calls"].as<JsonArray>();
            serializeJson(toolCalls, result.toolCallsJson);
            for (JsonObject tc : toolCalls) {
                ToolCall t;
                t.id = tc["id"].as<String>();
                t.name = tc["function"]["name"].as<String>();
                t.arguments = tc["function"]["arguments"].as<String>();
                result.toolCalls.push_back(t);
            }
        }
    } else {
        result.content = "Error: OpenRouter request failed (" + String(httpResponseCode) + ")";
    }
    http.end();
    return result;
}

AIResponse AIHandler::callGemini(std::vector<AIMessage> messages, String apiKey, String model) {
    return {"Gemini tool calling not implemented yet", {}};
}

AIResponse AIHandler::callClaude(std::vector<AIMessage> messages, String apiKey, String model) {
    return {"Claude tool calling not implemented yet", {}};
}
