#include "hotkey-manager.hpp"
#include "settings-window.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QMainWindow>
#include <QDockWidget>
#include <QMessageBox>
#include <QTimer>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <cassert>
#include <iostream>

// Exercise the production editor/manager with in-memory settings outside a module.
extern "C" const char *obs_module_text(const char *key) { return key; }
Settings::Settings() : data(obs_data_create()) {}
Settings::~Settings() { obs_data_release(data); }
static bool saveOK = true;
static int saveCalls = 0;
bool Settings::save() { ++saveCalls; return saveOK; }

static void expect(HotkeyManager &hotkey, uint32_t modifiers, obs_key_t key)
{
    const auto binding = hotkey.primaryBinding();
    assert(binding.modifiers == modifiers && binding.key == key);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    assert(obs_startup("en-US", nullptr, nullptr));
    {
        Settings settings;
        HotkeyManager hotkey(nullptr, [] {});
        hotkey.load(settings.data);
        expect(hotkey, INTERACT_ALT_KEY, OBS_KEY_R);

        SettingsWindow absent(settings, hotkey);
        absent.show();
        assert(!absent.findChild<QPushButton *>("openAutoStop")->isVisible());
        assert(absent.findChild<QWidget *>("autoStopRow")->isHidden());
        absent.hide();
        QMainWindow mainWindow;
        auto *dock = new QDockWidget(&mainWindow);
        dock->setObjectName("obs-auto-stop-dock");
        dock->setWidget(new QWidget);
        mainWindow.addDockWidget(Qt::RightDockWidgetArea, dock);
        mainWindow.showMinimized();
        SettingsWindow linked(settings, hotkey, &mainWindow);
        linked.show();
        auto *open = linked.findChild<QPushButton *>("openAutoStop");
        assert(open && open->isVisible());
        // Auto Stop remains reachable while the rest of a short settings window scrolls.
        linked.resize(540, 420);
        QApplication::processEvents();
        auto *linkedScroll = linked.findChild<QScrollArea *>("settingsScroll");
        const auto openPosition = open->mapTo(&linked, QPoint(0, 0));
        assert(openPosition.y() + open->height() <= linkedScroll->mapTo(&linked, QPoint(0, 0)).y());
        assert(linkedScroll->verticalScrollBar()->maximum() > 0);
        linkedScroll->verticalScrollBar()->setValue(linkedScroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        assert(open->mapTo(&linked, QPoint(0, 0)) == openPosition);
        assert(linked.rect().contains(QRect(openPosition, open->size())));
        // Enter on Auto Stop must open its dock, even with an unsaved draft and Save as default.
        auto *linkedMode = linked.findChild<QComboBox *>("startMode");
        auto *linkedSave = linked.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
        linkedMode->setCurrentIndex(1);
        assert(linkedSave->isEnabled());
        int opened = 0;
        QObject::connect(open, &QPushButton::clicked, &linked, [&] { ++opened; });
        const int beforeSave = saveCalls;
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
            QApplication::processEvents();
            // The minimal platform has no window manager to activate the dialog.
            QApplication::setActiveWindow(&linked);
            linkedSave->setDefault(true);
            open->setFocus();
            assert(open->hasFocus());
            const int beforeOpen = opened;
            QKeyEvent enter(QEvent::KeyPress, key, Qt::NoModifier);
            QApplication::sendEvent(open, &enter);
            assert(opened == beforeOpen + 1 && dock->isVisible());
            assert(linked.isVisible() && saveCalls == beforeSave);
            assert(settings.startMode == StartMode::Confirm && linkedSave->isEnabled());
            assert(mainWindow.isMinimized());
            dock->close();
        }
        linkedMode->setCurrentIndex(0);
        open->click();
        assert(mainWindow.isMinimized());
        assert(dock->isFloating() && dock->isVisible() && !dock->isMinimized());
        assert(dock->windowType() == Qt::Window);
        dock->close();
        mainWindow.hide();
        open->click();
        assert(!mainWindow.isVisible() && dock->isVisible());
        delete dock;
        assert(!open->isVisible() && linked.findChild<QWidget *>("autoStopRow")->isHidden());
        open->click();
        assert(!open->isVisible());
        linked.hide();

        {
            SettingsWindow dialog(settings, hotkey);
            dialog.show();
            QApplication::processEvents();
            QApplication::setActiveWindow(&dialog);
            auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
            auto *status = dialog.findChild<QLabel *>("settingsStatus");
            assert(!save->isEnabled() && status->text() == "SettingsUnchanged");
            // Every draft control enables Save, and reverting it clears the dirty state.
            for (auto *box : dialog.findChildren<QCheckBox *>()) {
                const bool checked = box->isChecked();
                box->setChecked(!checked);
                assert(save->isEnabled() && status->text() == "SettingsUnsaved");
                box->setChecked(checked);
                assert(!save->isEnabled());
            }
            auto *mode = dialog.findChild<QComboBox *>("startMode");
            mode->setCurrentIndex(1);
            assert(save->isEnabled() && dialog.findChild<QLabel *>("startModeHint")->text() == "ImmediateHint");
            mode->setCurrentIndex(2);
            assert(dialog.findChild<QLabel *>("startModeHint")->text() == "CountdownHint");
            mode->setCurrentIndex(0);
            assert(!save->isEnabled());
            auto *quality = dialog.findChild<QComboBox *>("recordingQuality");
            quality->setCurrentIndex(1);
            assert(save->isEnabled() && dialog.findChild<QLabel *>("recordingQualityHint")->text() == "QualityEconomyHint");
            quality->setCurrentIndex(0);
            assert(!save->isEnabled());

            auto *field = dialog.findChild<QLineEdit *>();
            auto *change = dialog.findChild<QPushButton *>("changeHotkey");
            assert(change && change->isVisible());
            const auto originalText = field->text();
            field->setFocus();
            assert(field->hasFocus());
            QKeyEvent modifier(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 0, 0x11, 0);
            QKeyEvent key(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, 0, 0x4B, 0);
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, 0, 0x1B, 0);
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, 0, 0x09, 0);
            // Focus alone cannot arm capture, even when Change is the default button.
            change->setDefault(true);
            for (const auto keyCode : {Qt::Key_Return, Qt::Key_Enter}) {
                QKeyEvent enter(QEvent::KeyPress, keyCode, Qt::NoModifier);
                QApplication::sendEvent(field, &enter);
                QApplication::sendEvent(field, &key);
                assert(field->text() == originalText && !save->isEnabled());
            }
            change->click();
            assert(field->text() == "HotkeyListening" && !save->isEnabled());
            QApplication::sendEvent(field, &modifier);
            assert(field->text() == "HotkeyListening" && !save->isEnabled());
            QApplication::sendEvent(field, &escape);
            assert(dialog.isVisible() && field->text() == originalText && !save->isEnabled());
            QApplication::sendEvent(field, &key);
            assert(field->text() == originalText);

            // Leaving the field cancels capture, and focusing it again does not rearm it.
            change->click();
            mode->setFocus();
            field->setFocus();
            QApplication::sendEvent(field, &key);
            assert(field->text() == originalText && !save->isEnabled());
            change->click();
            QApplication::sendEvent(field, &tab);
            assert(!field->hasFocus() && field->text() == originalText && !save->isEnabled());
            field->setFocus();
            QApplication::sendEvent(field, &key);
            assert(field->text() == originalText);

            // Capture exactly one combination; later keys cannot overwrite its unsaved draft.
            change->click();
            QApplication::sendEvent(field, &key);
            const auto draftText = field->text();
            assert(draftText != originalText && draftText != "HotkeyListening" && save->isEnabled());
            expect(hotkey, INTERACT_ALT_KEY, OBS_KEY_R); // Still a draft.
            QKeyEvent originalKey(QEvent::KeyPress, Qt::Key_R, Qt::AltModifier, 0, 0x52, 0);
            QApplication::sendEvent(field, &originalKey);
            assert(field->text() == draftText && save->isEnabled());
            change->click();
            QApplication::sendEvent(field, &escape);
            assert(dialog.isVisible() && field->text() == draftText && save->isEnabled());
            change->click();
            QApplication::sendEvent(field, &originalKey);
            assert(field->text() == originalText && !save->isEnabled());

            // Enter on Change must arm input, not save other unsaved settings.
            mode->setCurrentIndex(1);
            const int beforeChangeSave = saveCalls;
            for (const auto keyCode : {Qt::Key_Return, Qt::Key_Enter}) {
                save->setDefault(true);
                change->setFocus();
                assert(change->hasFocus());
                QKeyEvent enter(QEvent::KeyPress, keyCode, Qt::NoModifier);
                QApplication::sendEvent(change, &enter);
                assert(field->hasFocus() && field->text() == "HotkeyListening");
                assert(dialog.isVisible() && saveCalls == beforeChangeSave);
                assert(settings.startMode == StartMode::Confirm);
                QApplication::sendEvent(field, &escape);
            }
            mode->setCurrentIndex(0);

            // Details start collapsed. In a short window only the body scrolls.
            auto *scroll = dialog.findChild<QScrollArea *>("settingsScroll");
            for (const char *key : {"QualityHelp", "RestoreHelp"})
                assert(dialog.findChild<QLabel *>(key)->isHidden());
            dialog.findChild<QToolButton *>("QualityDetails")->click();
            dialog.findChild<QToolButton *>("RestoreDetails")->click();
            assert(dialog.findChild<QLabel *>("QualityHelp")->isVisible());
            assert(dialog.findChild<QLabel *>("RestoreHelp")->isVisible());
            dialog.resize(540, 420);
            QApplication::processEvents();
            assert(scroll->verticalScrollBar()->maximum() > 0);
            const auto savePosition = save->mapTo(&dialog, QPoint(0, 0));
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            QApplication::processEvents();
            assert(save->mapTo(&dialog, QPoint(0, 0)) == savePosition);
            assert(dialog.rect().contains(QRect(savePosition, save->size())));
            assert(!save->isEnabled()); // Expanding help is not a setting change.
        }

        auto other = obs_hotkey_register_frontend("test.other", "Other", [](void *, obs_hotkey_id, obs_hotkey_t *, bool) {}, nullptr);
        obs_key_combination_t otherKey{INTERACT_CONTROL_KEY, OBS_KEY_O};
        obs_hotkey_load_bindings(other, &otherKey, 1);

        auto edit = [&](QDialogButtonBox::StandardButton button) {
            SettingsWindow dialog(settings, hotkey);
            dialog.findChild<QComboBox *>("recordingQuality")->setCurrentIndex(1);
            auto *field = dialog.findChild<QLineEdit *>();
            assert(field);
            dialog.findChild<QPushButton *>("changeHotkey")->click();
            // A Windows native VK is required by the same conversion OBS uses.
            QKeyEvent key(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier, 0, 0x52, 0);
            QApplication::sendEvent(field, &key);
            dialog.findChild<QDialogButtonBox *>()->button(button)->click();
        };
        edit(QDialogButtonBox::Cancel);
        assert(!settings.economy);
        expect(hotkey, INTERACT_ALT_KEY, OBS_KEY_R);
        edit(QDialogButtonBox::Save);
        assert(settings.economy);
        expect(hotkey, INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY, OBS_KEY_R);
        {
            SettingsWindow dialog(settings, hotkey);
            dialog.show();
            dialog.findChild<QComboBox *>()->setCurrentIndex(2);
            dialog.findChild<QComboBox *>("recordingQuality")->setCurrentIndex(0);
            dialog.findChild<QPushButton *>("changeHotkey")->click();
            QKeyEvent key(QEvent::KeyPress, Qt::Key_O, Qt::AltModifier, 0, 0x4F, 0);
            QApplication::sendEvent(dialog.findChild<QLineEdit *>(), &key);
            saveOK = false;
            bool errorShown = false;
            QTimer::singleShot(0, &dialog, [&] {
                auto *error = dialog.findChild<QMessageBox *>();
                assert(error && error->text() == "SettingsSaveFailed");
                errorShown = true; error->accept();
            });
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            assert(errorShown && dialog.isVisible() && settings.startMode == StartMode::Confirm);
            assert(settings.economy);
            expect(hotkey, INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY, OBS_KEY_R);
            saveOK = true;
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            assert(dialog.result() == QDialog::Accepted && settings.startMode == StartMode::Countdown);
            assert(!settings.economy);
            expect(hotkey, INTERACT_ALT_KEY, OBS_KEY_O);
            hotkey.setPrimaryBinding({INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY, OBS_KEY_R});
        }

        hotkey.save(settings.data);
        Settings restored;
        obs_data_release(restored.data);
        restored.data = obs_data_create_from_json(obs_data_get_json(settings.data));
        assert(restored.data);
        hotkey.setPrimaryBinding({INTERACT_ALT_KEY, OBS_KEY_O});
        hotkey.load(restored.data);
        expect(hotkey, INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY, OBS_KEY_R);
        auto *otherBindings = obs_hotkey_save(other);
        assert(obs_data_array_count(otherBindings) == 1);
        auto *otherBinding = obs_data_array_item(otherBindings, 0);
        assert(obs_data_get_bool(otherBinding, "control"));
        assert(QString::fromUtf8(obs_data_get_string(otherBinding, "key")) == "OBS_KEY_O");
        obs_data_release(otherBinding);
        obs_data_array_release(otherBindings);
        obs_hotkey_unregister(other);
    }
    obs_shutdown();
    std::cout << "Settings drafts, hints, scrolling, Auto Stop, hotkey edit/cancel and save/rollback passed\n";
}
