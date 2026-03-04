#pragma once

extern char g_csvPath[64];

void NameTheCSVFile();
bool CreateCSVFile();
bool SaveEvent(const char* eventType);