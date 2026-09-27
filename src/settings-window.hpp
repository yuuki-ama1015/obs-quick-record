#pragma once
#include <QDialog>
#include "settings.hpp"
class SettingsWindow : public QDialog {
public:
    explicit SettingsWindow(Settings &settings);
};
