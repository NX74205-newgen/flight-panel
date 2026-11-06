#pragma once
#include <Arduino.h>

enum class UiMode { Boot, Portal, Connecting, Run };

void displayInit();
void displayStartTask();
void displaySetMode(UiMode m, const char *msg = nullptr);
void displayApplyRotation();   // apply cfg.flip
void displayNextFlight();      // DOWN button
void displayFlash(const char *msg, uint32_t ms = 1500);  // short overlay message
