#include "settings-window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QPointer>
#include <QPushButton>
#include <util/dstr.h>

static QString bindingText(obs_key_combination_t binding)
{
    if (obs_key_combination_is_empty(binding)) return text("HotkeyUnset");
    dstr label = {};
    obs_key_combination_to_str(binding, &label);
    const QString result = QString::fromUtf8(label.array);
    dstr_free(&label);
    return result;
}

class HotkeyEdit : public QLineEdit {
public:
    explicit HotkeyEdit(obs_key_combination_t binding) : binding(binding)
    {
        setReadOnly(true);
        setText(bindingText(binding));
        setToolTip(::text("HotkeyHelp"));
    }
    obs_key_combination_t value() const { return binding; }
protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->isAutoRepeat()) return;
        switch (event->key()) {
        case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
            return;
        default: break;
        }
        const auto key = obs_key_from_virtual_key(static_cast<int>(event->nativeVirtualKey()));
        if (key == OBS_KEY_NONE) return;
        uint32_t modifiers = 0;
        if (event->modifiers().testFlag(Qt::ShiftModifier)) modifiers |= INTERACT_SHIFT_KEY;
        if (event->modifiers().testFlag(Qt::ControlModifier)) modifiers |= INTERACT_CONTROL_KEY;
        if (event->modifiers().testFlag(Qt::AltModifier)) modifiers |= INTERACT_ALT_KEY;
        if (event->modifiers().testFlag(Qt::MetaModifier)) modifiers |= INTERACT_COMMAND_KEY;
        binding = {modifiers, key};
        setText(bindingText(binding));
        event->accept();
    }
private:
    obs_key_combination_t binding;
};

SettingsWindow::SettingsWindow(Settings &settings, HotkeyManager &hotkey, QWidget *obsWindow) : QDialog(nullptr)
{
    setWindowTitle(text("Settings"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(text("Hotkey")));
    auto *hotkeyEdit = new HotkeyEdit(hotkey.primaryBinding());
    layout->addWidget(hotkeyEdit);
    auto *hotkeyHelp = new QLabel(text("HotkeyHelp"));
    hotkeyHelp->setWordWrap(true);
    layout->addWidget(hotkeyHelp);
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
    // Optional UI integration only: OBS owns this dock and Auto Stop owns its settings.
    auto *autoStop = new QPushButton(text("OpenAutoStop"));
    autoStop->setObjectName("openAutoStop");
    const QPointer<QDockWidget> dock = obsWindow
        ? obsWindow->findChild<QDockWidget *>("obs-auto-stop-dock") : nullptr;
    autoStop->setEnabled(!dock.isNull());
    autoStop->setToolTip(text(dock ? "AutoStopHelp" : "AutoStopUnavailable"));
    layout->addWidget(autoStop);
    connect(autoStop, &QPushButton::clicked, this, [dock, autoStop] {
        if (!dock) { autoStop->setEnabled(false); return; }
        dock->setFloating(true);
        // A normal window stays usable when the OBS owner is minimized/hidden.
        dock->setWindowFlags((dock->windowFlags() & ~Qt::WindowType_Mask) |
                             Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint |
                             Qt::WindowStaysOnTopHint);
        dock->showNormal();
        dock->raise();
        dock->activateWindow();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [&, hotkeyEdit, mode, cursor, remember, safety, indicator, restore] {
        hotkey.setPrimaryBinding(hotkeyEdit->value());
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
