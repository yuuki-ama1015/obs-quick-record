// Optional manual acceptance probe. Never installed or linked into the product.
// Loading is refused unless the executable is inside the explicit QA directory.
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <QWidget>

OBS_DECLARE_MODULE()

namespace {
class Probe final : public QObject {
public:
    QString root;
    QPointer<QWidget> main;
    QTimer commands;
    bool registered = false;
    bool shuttingDown = false;
    explicit Probe(QString directory) : root(std::move(directory))
    {
        main = static_cast<QWidget *>(obs_frontend_get_main_window());
        if (main) main->installEventFilter(this);
        obs_frontend_add_event_callback(event, this);
        registered = true;
        connect(&commands, &QTimer::timeout, this, [this] {
            QFile file(root + "/probe-command.txt");
            if (!file.open(QIODevice::ReadOnly)) return;
            const auto command = file.read(64).trimmed();
            file.close();
            if (!file.remove()) return;
            log("command=" + command);
            if (command == "snapshot") snapshot();
            else if (command == "toggle") {
                obs_enum_hotkeys([](void *, obs_hotkey_id id, obs_hotkey_t *key) {
                    if (strcmp(obs_hotkey_get_name(key), "obs-quick-record.toggle") != 0) return true;
                    obs_hotkey_trigger_routed_callback(id, true);
                    obs_hotkey_trigger_routed_callback(id, false);
                    return false;
                }, nullptr);
            } else if (command == "stop") obs_frontend_recording_stop();
            else if (command == "exit" && !obs_frontend_recording_active() && main) main->close();
        });
        commands.start(300);
        log("loaded");
    }
    ~Probe() override
    {
        commands.stop();
        if (registered) obs_frontend_remove_event_callback(event, this);
        if (main) main->removeEventFilter(this);
    }
    void log(const QByteArray &line)
    {
        QFile file(root + "/probe-events.log");
        if (file.size() > 2 * 1024 * 1024 || !file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
        file.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + ' ' + line + '\n');
    }
    void snapshot()
    {
        log(QByteArray("recording=") + (obs_frontend_recording_active() ? "true" : "false"));
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (widget == main || widget->isVisible())
                log(QByteArray(widget == main ? "main " : "visible ") + widget->metaObject()->className() +
                    " name=" + widget->objectName().toUtf8() + " visible=" + QByteArray::number(widget->isVisible()) +
                    " minimized=" + QByteArray::number(widget->isMinimized()));
        }
    }
    bool eventFilter(QObject *object, QEvent *e) override
    {
        if (object == main && (e->type() == QEvent::Show || e->type() == QEvent::Hide ||
                              e->type() == QEvent::WindowActivate || e->type() == QEvent::WindowDeactivate))
            log("main event=" + QByteArray::number(e->type()));
        return false;
    }
    static void event(obs_frontend_event e, void *data)
    {
        auto *self = static_cast<Probe *>(data);
        if (self->shuttingDown) return;
        self->log("frontend event=" + QByteArray::number(e));
        if (e == OBS_FRONTEND_EVENT_RECORDING_STOPPED || e == OBS_FRONTEND_EVENT_FINISHED_LOADING)
            QTimer::singleShot(1000, self, [self] { if (!self->shuttingDown) self->snapshot(); });
        if (e == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || e == OBS_FRONTEND_EVENT_EXIT) {
            self->shuttingDown = true;
            self->commands.stop();
            obs_frontend_remove_event_callback(event, self);
            self->registered = false;
        }
    }
};
Probe *probe = nullptr;
}

bool obs_module_load()
{
    const QString root = QDir::cleanPath(qEnvironmentVariable("OBS_QUICK_RECORD_QA_ROOT"));
    const QString executable = QDir::cleanPath(QApplication::applicationFilePath());
    if (!QDir::isAbsolutePath(root) || !executable.startsWith(root + '/', Qt::CaseInsensitive)) return false;
    probe = new Probe(root);
    return true;
}
void obs_module_unload() { delete probe; probe = nullptr; }
const char *obs_module_name() { return "Quick Record acceptance probe (test only)"; }
const char *obs_module_description() { return "Private acceptance diagnostics; excluded from all packages."; }
