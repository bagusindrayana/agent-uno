#include "AIHandler.h"

AIHandler::AIHandler() {}

String AIHandler::getResponse(String userMessage, AIProvider provider, String apiKey, String model) {
    if (apiKey.length() == 0 || provider == NONE) return "";

    switch (provider) {
        case OPENAI: return callOpenAI(userMessage, apiKey, model);
        case OPENROUTER: return callOpenRouter(userMessage, apiKey, model);
        case GEMINI: return callGemini(userMessage, apiKey, model);
        case CLAUDE: return callClaude(userMessage, apiKey, model);
        default: return "";
    }
}

String AIHandler::callOpenAI(String message, String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.openai.com/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + apiKey);

    JsonDocument doc;
    doc["model"] = model.length() > 0 ? model : "gpt-3.5-turbo";
    JsonArray messages = doc.createNestedArray("messages");
    JsonObject msg = messages.createNestedObject();
    msg["role"] = "user";
    msg["content"] = message;

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

String AIHandler::callOpenRouter(String message, String apiKey, String model) {
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
    JsonObject msg = messages.createNestedObject();
    msg["role"] = "user";
    msg["content"] = message;

    String json;
    serializeJson(doc, json);
    int httpResponseCode = http.POST(json);

    String result = "";
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("OpenRouter Response: "); Serial.println(response); // Debug log
        
        JsonDocument resDoc;
        DeserializationError error = deserializeJson(resDoc, response);
        if (error) {
            result = "Error: Failed to parse OpenRouter JSON (" + String(error.c_str()) + ")";
        } else if (resDoc.containsKey("choices") && resDoc["choices"].is<JsonArray>() && resDoc["choices"].size() > 0) {
            result = resDoc["choices"][0]["message"]["content"].as<String>();
            if (result == "null" || result == "") {
                result = "Error: OpenRouter returned empty/null content";
            }
        } else if (resDoc.containsKey("error")) {
            result = "Error from OpenRouter: " + resDoc["error"]["message"].as<String>();
        } else {
            result = "Error: Unexpected OpenRouter response structure";
        }
    } else {
        result = "Error: OpenRouter request failed (Code: " + String(httpResponseCode) + ")";
        Serial.print("OpenRouter Error: "); Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
    return result;
}

String AIHandler::callGemini(String message, String apiKey, String model) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String modelName = model.length() > 0 ? model : "gemini-pro";
    String url = "https://generativelanguage.googleapis.com/v1beta/models/" + modelName + ":generateContent?key=" + apiKey;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    JsonArray contents = doc.createNestedArray("contents");
    JsonObject contentObj = contents.createNestedObject();
    JsonArray parts = contentObj.createNestedArray("parts");
    JsonObject partObj = parts.createNestedObject();
    partObj["text"] = message;

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

String AIHandler::callClaude(String message, String apiKey, String model) {
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
    JsonObject msg = messages.createNestedObject();
    msg["role"] = "user";
    msg["content"] = message;

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
