#include "src/BotAgent.h"

BotAgent agent;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n--- ESP32 Bot Agent Starting ---");
    
    // Initialize the agent
    agent.begin();
    
    Serial.println("System ready!");
}

void loop() {
    // Handle Web Server and Telegram messages
    // The agent manages settings internally via the Web UI
    agent.loop();
    
    // If you want to access current settings:
    // BotSettings& current = agent.getSettings();

    delay(100); 
}
