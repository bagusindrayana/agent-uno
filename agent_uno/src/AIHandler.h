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

enum SearchProvider {
    SEARCH_NONE = 0,
    TAVILY,
    BRAVE,
    DUCKDUCKGO
};

#include <vector>

struct AIMessage {
    String role;
    String content;
    String name;
    String tool_call_id;
    String tool_calls_json; // To store the tool_calls array for assistant messages
};

struct ToolCall {
    String id;
    String name;
    String arguments;
};

struct AIResponse {
    String content;
    std::vector<ToolCall> toolCalls;
    String toolCallsJson; // Store the original tool_calls JSON array
};

class AIHandler {
public:
    AIHandler();
    AIResponse getResponse(String userMessage, String systemPrompt, AIProvider provider, String apiKey, String model);
    void clearHistory();
    void addMessage(AIMessage msg);

private:
    std::vector<AIMessage> _history;
    const size_t _maxHistory = 15; // Set higher to accommodate tool calls

    AIResponse callOpenAI(std::vector<AIMessage> messages, String apiKey, String model);
    AIResponse callOpenRouter(std::vector<AIMessage> messages, String apiKey, String model);
    AIResponse callGemini(std::vector<AIMessage> messages, String apiKey, String model);
    AIResponse callClaude(std::vector<AIMessage> messages, String apiKey, String model);
};

#endif
