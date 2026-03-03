#include "AIHandler.h"

AIHandler::AIHandler() {}

String AIHandler::getResponse(String userMessage, AIProvider provider, String apiKey, String model) {
    if (apiKey.length() == 0 || provider == NONE) return "";

    addMessage("user", userMessage);

    String result = "";
    switch (provider) {
        case OPENAI: result = callOpenAI(apiKey, model); break;
        case OPENROUTER: result = callOpenRouter(apiKey, model); break;
        case GEMINI: result = callGemini(apiKey, model); break;
        case CLAUDE: result = callClaude(apiKey, model); break;
        default: break;
    }

    if (result.length() > 0 && !result.startsWith("Error:")) {
        addMessage("assistant", result);
    }
    return result;
}

void AIHandler::clearHistory() {
    _history.clear();
    Serial.println("AI Chat History Cleared");
}

void AIHandler::addMessage(String role, String content) {
    _history.push_back({role, content});
    if (_history.size() > _maxHistory) {
        _history.erase(_history.begin());
    }
}

String AIHandler::callOpenAI(String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.openai.com/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + apiKey);

    JsonDocument doc;
    doc["model"] = model.length() > 0 ? model : "gpt-3.5-turbo";
    JsonArray messages = doc.createNestedArray("messages");
    for (const auto& msg : _history) {
        JsonObject m = messages.createNestedObject();
        m["role"] = msg.role;
        m["content"] = msg.content;
    }

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    String result = "";
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("OpenAI Response: "); Serial.println(response);
        JsonDocument resDoc;
        deserializeJson(resDoc, response);
        result = resDoc["choices"][0]["message"]["content"].as<String>();
    } else {
        result = "Error: OpenAI request failed (" + String(httpResponseCode) + ")";
        Serial.print("OpenAI Error: "); Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
    return result;
}

String AIHandler::callOpenRouter(String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://openrouter.ai/api/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + apiKey);
    http.addHeader("HTTP-Referer", "http://esp32-agent.local");

    JsonDocument doc;
    doc["model"] = model.length() > 0 ? model : "google/gemini-2.0-flash-001";
    JsonArray messages = doc.createNestedArray("messages");
    for (const auto& msg : _history) {
        JsonObject m = messages.createNestedObject();
        m["role"] = msg.role;
        m["content"] = msg.content;
    }

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    String result = "";
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("OpenRouter Response: "); Serial.println(response);
        JsonDocument resDoc;
        DeserializationError error = deserializeJson(resDoc, response);
        if (error) {
            result = "Error: Failed to parse OpenRouter JSON (" + String(error.c_str()) + ")";
        } else if (resDoc.containsKey("choices") && resDoc["choices"].is<JsonArray>() && resDoc["choices"].size() > 0) {
            result = resDoc["choices"][0]["message"]["content"].as<String>();
        } else {
            result = "Error: Unexpected OpenRouter response structure";
        }
    } else {
        result = "Error: OpenRouter request failed (" + String(httpResponseCode) + ")";
        Serial.print("OpenRouter Error: "); Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
    return result;
}

String AIHandler::callGemini(String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String modelName = model.length() > 0 ? model : "gemini-pro";
    String url = "https://generativelanguage.googleapis.com/v1beta/models/" + modelName + ":generateContent?key=" + apiKey;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    JsonArray contents = doc.createNestedArray("contents");
    for (const auto& msg : _history) {
        JsonObject contentObj = contents.createNestedObject();
        contentObj["role"] = (msg.role == "assistant") ? "model" : "user";
        JsonArray parts = contentObj.createNestedArray("parts");
        JsonObject partObj = parts.createNestedObject();
        partObj["text"] = msg.content;
    }

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    String result = "";
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("Gemini Response: "); Serial.println(response);
        JsonDocument resDoc;
        deserializeJson(resDoc, response);
        result = resDoc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
    } else {
        result = "Error: Gemini request failed (" + String(httpResponseCode) + ")";
        Serial.print("Gemini Error: "); Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
    return result;
}

String AIHandler::callClaude(String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.anthropic.com/v1/messages");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", apiKey);
    http.addHeader("anthropic-version", "2023-06-01");

    JsonDocument doc;
    doc["model"] = model.length() > 0 ? model : "claude-3-haiku-20240307";
    doc["max_tokens"] = 1024;
    JsonArray messages = doc.createNestedArray("messages");
    for (const auto& msg : _history) {
        JsonObject m = messages.createNestedObject();
        m["role"] = msg.role;
        m["content"] = msg.content;
    }

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    String result = "";
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("Claude Response: "); Serial.println(response);
        JsonDocument resDoc;
        deserializeJson(resDoc, response);
        result = resDoc["content"][0]["text"].as<String>();
    } else {
        result = "Error: Claude request failed (" + String(httpResponseCode) + ")";
        Serial.print("Claude Error: "); Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
    return result;
}
