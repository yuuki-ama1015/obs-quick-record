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
#include <QMessageBox>
#include <QScrollArea>
#include <QScreen>
#include <QToolButton>
#include <tuple>
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
    void focusInEvent(QFocusEvent *event) override
    {
        beforeEdit = binding;
        QLineEdit::focusInEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        event->accept();
        if (event->isAutoRepeat()) return;
        switch (event->key()) {
        case Qt::Key_Escape:
            binding = beforeEdit;
            setText(bindingText(binding));
            clearFocus();
            return;
        case Qt::Key_Tab: case Qt::Key_Backtab:
            QLineEdit::keyPressEvent(event);
            return;
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
    obs_key_combination_t beforeEdit = binding;
};

SettingsWindow::SettingsWindow(Settings &settings, HotkeyManager &hotkey, QWidget *obsWindow) : QDialog(nullptr)
{
    setWindowTitle(text("Settings"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 18, 18, 18);
    outer->setSpacing(12);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("settingsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(8);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    auto label = [&](const char *key) {
        auto *widget = new QLabel(text(key));
        widget->setWordWrap(true);
        layout->addWidget(widget);
        return widget;
    };
    auto section = [&](const char *key) {
        if (layout->count()) layout->addSpacing(12);
        auto *row = new QHBoxLayout;
        auto *heading = new QLabel(text(key));
        auto font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        row->addWidget(heading);
        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        row->addWidget(line, 1);
        layout->addLayout(row);
    };
    auto details = [&](const char *key, const char *helpKey) {
        auto *toggle = new QToolButton;
        toggle->setObjectName(key);
        toggle->setText(text(key));
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setAutoRaise(true);
        toggle->setCheckable(true);
        layout->addWidget(toggle, 0, Qt::AlignLeft);
        auto *help = label(helpKey);
        help->setObjectName(helpKey);
        help->hide();
        connect(toggle, &QToolButton::toggled, help, [toggle, help](bool expanded) {
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            help->setVisible(expanded);
        });
    };
    section("ShortcutSection");
    auto *hotkeyLabel = label("ShortcutLabel");
    auto *hotkeyEdit = new HotkeyEdit(hotkey.primaryBinding());
    hotkeyEdit->setObjectName("recordingHotkey");
    hotkeyLabel->setBuddy(hotkeyEdit);
    layout->addWidget(hotkeyEdit);
    label("ShortcutHint");
    section("RecordingSection");
    auto *modeLabel = label("StartMode");
    auto *mode = new QComboBox;
    mode->setObjectName("startMode");
    mode->addItems({text("ConfirmRecommended"), text("Immediate"), text("Countdown")});
    mode->setCurrentIndex(static_cast<int>(settings.startMode));
    modeLabel->setBuddy(mode);
    layout->addWidget(mode);
    auto *modeHint = label("ConfirmHint");
    modeHint->setObjectName("startModeHint");
    auto updateModeHint = [mode, modeHint] {
        const char *hints[] = {"ConfirmHint", "ImmediateHint", "CountdownHint"};
        const int index = mode->currentIndex();
        if (index >= 0 && index < 3) modeHint->setText(text(hints[index]));
    };
    connect(mode, &QComboBox::currentIndexChanged, modeHint, updateModeHint);
    updateModeHint();
    auto *qualityLabel = label("RecordingQuality");
    auto *quality = new QComboBox;
    quality->setObjectName("recordingQuality");
    quality->addItems({text("QualityCurrent"), text("QualityEconomy")});
    quality->setCurrentIndex(settings.economy ? 1 : 0);
    qualityLabel->setBuddy(quality);
    layout->addWidget(quality);
    auto *qualityHint = label("QualityCurrentHint");
    qualityHint->setObjectName("recordingQualityHint");
    auto updateQualityHint = [quality, qualityHint] {
        qualityHint->setText(text(quality->currentIndex() == 1 ? "QualityEconomyHint" : "QualityCurrentHint"));
    };
    connect(quality, &QComboBox::currentIndexChanged, qualityHint, updateQualityHint);
    updateQualityHint();
    details("QualityDetails", "QualityHelp");
    section("CaptureSection");
    auto check = [&](const char *key, bool value) {
        auto *box = new QCheckBox(text(key));
        box->setObjectName(key);
        box->setChecked(value);
        layout->addWidget(box);
        return box;
    };
    auto *cursor = check("Cursor", settings.cursor);
    auto *remember = check("Remember", settings.rememberRegion);
    auto *safety = check("Safety", settings.foregroundSafety);
    label("SafetyHint");
    auto *indicator = check("Indicator", settings.indicator);
    auto *restore = check("Restore", settings.restoreScene);
    details("RestoreDetails", "RestoreHelp");
    // Optional UI integration only: OBS owns this dock and Auto Stop owns its settings.
    auto *autoStopRow = new QWidget;
    autoStopRow->setObjectName("autoStopRow");
    auto *autoStopLayout = new QVBoxLayout(autoStopRow);
    autoStopLayout->setContentsMargins(0, 12, 0, 0);
    auto *autoStopLine = new QFrame;
    autoStopLine->setFrameShape(QFrame::HLine);
    autoStopLayout->addWidget(autoStopLine);
    auto *autoStop = new QPushButton(text("OpenAutoStop"));
    autoStop->setObjectName("openAutoStop");
    const QPointer<QDockWidget> dock = obsWindow
        ? obsWindow->findChild<QDockWidget *>("obs-auto-stop-dock") : nullptr;
    autoStop->setToolTip(text("AutoStopHelp"));
    autoStopLayout->addWidget(autoStop);
    auto *autoStopHint = new QLabel(text("AutoStopHint"));
    autoStopHint->setWordWrap(true);
    autoStopLayout->addWidget(autoStopHint);
    layout->addWidget(autoStopRow);
    autoStopRow->setVisible(!dock.isNull());
    layout->addStretch();
    if (dock) connect(dock, &QObject::destroyed, autoStopRow, &QWidget::hide);
    connect(autoStop, &QPushButton::clicked, this, [dock, autoStopRow] {
        if (!dock) { autoStopRow->hide(); return; }
        dock->setFloating(true);
        // A normal window stays usable when the OBS owner is minimized/hidden.
        dock->setWindowFlags((dock->windowFlags() & ~Qt::WindowType_Mask) |
                             Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint |
                             Qt::WindowStaysOnTopHint);
        dock->showNormal();
        dock->raise();
        dock->activateWindow();
    });
    auto *footer = new QHBoxLayout;
    auto *status = new QLabel;
    status->setObjectName("settingsStatus");
    status->setWordWrap(true);
    footer->addWidget(status, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(text("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(text("Cancel"));
    footer->addWidget(buttons);
    outer->addLayout(footer);
    // Compare drafts only. The active OBS binding and settings change on Save.
    auto values = [=] {
        const auto binding = hotkeyEdit->value();
        return std::make_tuple(binding.modifiers, binding.key, mode->currentIndex(), quality->currentIndex(),
                               cursor->isChecked(), remember->isChecked(), safety->isChecked(),
                               indicator->isChecked(), restore->isChecked());
    };
    const auto initial = values();
    auto updateDirty = [=] {
        const bool changed = values() != initial;
        buttons->button(QDialogButtonBox::Save)->setEnabled(changed);
        status->setText(text(changed ? "SettingsUnsaved" : "SettingsUnchanged"));
    };
    connect(hotkeyEdit, &QLineEdit::textChanged, this, updateDirty);
    for (auto *combo : {mode, quality}) connect(combo, &QComboBox::currentIndexChanged, this, updateDirty);
    for (auto *box : {cursor, remember, safety, indicator, restore})
        connect(box, &QCheckBox::toggled, this, updateDirty);
    updateDirty();
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [&, hotkeyEdit, mode, quality, cursor, remember, safety, indicator, restore] {
        hotkey.save(settings.data);
        auto *previousData = obs_data_create_from_json(obs_data_get_json(settings.data));
        const auto previous = std::make_tuple(settings.startMode, settings.cursor, settings.rememberRegion,
                                              settings.foregroundSafety, settings.indicator, settings.restoreScene, settings.economy);
        hotkey.setPrimaryBinding(hotkeyEdit->value());
        settings.startMode = static_cast<StartMode>(mode->currentIndex());
        settings.economy = quality->currentIndex() == 1;
        settings.cursor = cursor->isChecked();
        settings.rememberRegion = remember->isChecked();
        settings.foregroundSafety = safety->isChecked();
        settings.indicator = indicator->isChecked();
        settings.restoreScene = restore->isChecked();
        hotkey.save(settings.data);
        if (!settings.save()) {
            obs_data_release(settings.data);
            settings.data = previousData;
            std::tie(settings.startMode, settings.cursor, settings.rememberRegion,
                     settings.foregroundSafety, settings.indicator, settings.restoreScene, settings.economy) = previous;
            hotkey.load(settings.data);
            QMessageBox::warning(this, text("Title"), text("SettingsSaveFailed"));
            return;
        }
        obs_data_release(previousData);
        accept();
    });
    const auto available = screen()->availableGeometry().size();
    resize(qMin(568, available.width() - 40), qMin(760, available.height() - 80));
}
