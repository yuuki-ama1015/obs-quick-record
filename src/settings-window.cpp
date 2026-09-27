#include "settings-window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
SettingsWindow::SettingsWindow(Settings &settings) : QDialog(nullptr)
{
    setWindowTitle(text("Settings"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    auto *layout = new QVBoxLayout(this);
    auto *hotkey = new QLabel(text("HotkeyHelp"));
    hotkey->setWordWrap(true);
    layout->addWidget(hotkey);
    layout->addWidget(new QLabel(text("StartMode")));
    auto *mode = new QComboBox;
    mode->addItems({text("Confirm"), text("Immediate"), text("Countdown")});
    mode->setCurrentIndex(static_cast<int>(settings.startMode));
    layout->addWidget(mode);
    auto check = [&](const char *key, bool value) {
        auto *box = new QCheckBox(text(key));
        box->setChecked(value);
        layout->addWidget(box);
        return box;
    };
    auto *cursor = check("Cursor", settings.cursor);
    auto *remember = check("Remember", settings.rememberRegion);
    auto *safety = check("Safety", settings.foregroundSafety);
    auto *indicator = check("Indicator", settings.indicator);
    auto *restore = check("Restore", settings.restoreScene);
    auto *help = new QLabel(text("RestoreHelp"));
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [&, mode, cursor, remember, safety, indicator, restore] {
        settings.startMode = static_cast<StartMode>(mode->currentIndex());
        settings.cursor = cursor->isChecked();
        settings.rememberRegion = remember->isChecked();
        settings.foregroundSafety = safety->isChecked();
        settings.indicator = indicator->isChecked();
        settings.restoreScene = restore->isChecked();
        accept();
    });
    resize(520, sizeHint().height());
}
