#pragma once
#include <string>

enum class ConsoleMsgType {
    System,  // [S] System messages, connection status, server announcements
    Task,    // [T] Task/Tutorial/Achievements (e.g., [Welcome to Growtopia <1> #0])
    World,   // [W] World chat, talk bubbles
    Private  // [P] Private messages, whispers
};

struct ConsoleEntry {
    ConsoleMsgType type = ConsoleMsgType::System;
    std::string timestamp; // "HH:MM:SS"
    std::string text;
};
