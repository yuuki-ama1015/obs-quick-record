#include "hotkey-manager.hpp"
#include "settings-window.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QMainWindow>
#include <QDockWidget>
#include <cassert>
#include <iostream>

// Exercise the production editor/manager with in-memory settings outside a module.
extern "C" const char *obs_module_text(const char *key) { return key; }
Settings::Settings() : data(obs_data_create()) {}
Settings::~Settings() { obs_data_release(data); }

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
        assert(absent.findChild<QPushButton *>("openAutoStop")->isHidden());
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
        assert(open->isHidden());
        open->click();
        assert(open->isHidden());

        auto other = obs_hotkey_register_frontend("test.other", "Other", [](void *, obs_hotkey_id, obs_hotkey_t *, bool) {}, nullptr);
        obs_key_combination_t otherKey{INTERACT_CONTROL_KEY, OBS_KEY_O};
        obs_hotkey_load_bindings(other, &otherKey, 1);

        auto edit = [&](QDialogButtonBox::StandardButton button) {
            SettingsWindow dialog(settings, hotkey);
            auto *field = dialog.findChild<QLineEdit *>();
            assert(field);
            // A Windows native VK is required by the same conversion OBS uses.
            QKeyEvent key(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier, 0, 0x52, 0);
            QApplication::sendEvent(field, &key);
            dialog.findChild<QDialogButtonBox *>()->button(button)->click();
        };
        edit(QDialogButtonBox::Cancel);
        expect(hotkey, INTERACT_ALT_KEY, OBS_KEY_R);
        edit(QDialogButtonBox::Save);
        expect(hotkey, INTERACT_CONTROL_KEY | INTERACT_SHIFT_KEY, OBS_KEY_R);

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
    std::cout << "Alt+R default, edit, cancel, save/reload and unrelated binding passed\n";
}
