#pragma once
#include <QDialog>
#include "settings.hpp"
#include "hotkey-manager.hpp"
class SettingsWindow : public QDialog {
public:
    SettingsWindow(Settings &settings, HotkeyManager &hotkey, QWidget *obsWindow = nullptr);
};
