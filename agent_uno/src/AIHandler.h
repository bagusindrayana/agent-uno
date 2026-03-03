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

struct AIMessage {
    String role;
    String content;
};

class AIHandler {
public:
    AIHandler();
    String getResponse(String userMessage, AIProvider provider, String apiKey, String model);
    void clearHistory();

private:
    std::vector<AIMessage> _history;
    const size_t _maxHistory = 10; // Max 5 turns (5 user, 5 assistant)

    void addMessage(String role, String content);
    String callOpenAI(String apiKey, String model);
    String callOpenRouter(String apiKey, String model);
    String callGemini(String apiKey, String model);
    String callClaude(String apiKey, String model);
};

#endif
