#ifndef AI_HANDLER_H
#define AI_HANDLER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

enum AIProvider {
    NONE = 0,
    OPENAI,
    OPENROUTER,
    GEMINI,
    CLAUDE
};

class AIHandler {
public:
    AIHandler();
    String getResponse(String userMessage, AIProvider provider, String apiKey, String model);

private:
    String callOpenAI(String message, String apiKey, String model);
    String callOpenRouter(String message, String apiKey, String model);
    String callGemini(String message, String apiKey, String model);
    String callClaude(String message, String apiKey, String model);
};

#endif
