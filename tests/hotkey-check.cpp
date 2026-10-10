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
bool Settings::save() { return saveOK; }

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
            dialog.activateWindow();
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
            mode->setFocus();
            QApplication::processEvents();
            field->setFocus();
            QApplication::processEvents();
            QKeyEvent modifier(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 0, 0x11, 0);
            QApplication::sendEvent(field, &modifier);
            assert(!save->isEnabled());
            QKeyEvent key(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, 0, 0x4B, 0);
            QApplication::sendEvent(field, &key);
            assert(save->isEnabled());
            expect(hotkey, INTERACT_ALT_KEY, OBS_KEY_R); // Still a draft.
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, 0, 0x1B, 0);
            QApplication::sendEvent(field, &escape);
            assert(dialog.isVisible() && !save->isEnabled());
            field->setFocus();
            QApplication::processEvents();
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, 0, 0x09, 0);
            QApplication::sendEvent(field, &tab);
            assert(!field->hasFocus() && !save->isEnabled());

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
