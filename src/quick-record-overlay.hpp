#pragma once
#include <QWidget>
class QLabel;
class QuickRecordOverlay : public QWidget {
    Q_OBJECT
public:
    QuickRecordOverlay();
    void open();
    void message(const QString &message);
signals:
    void confirmed();
    void canceled();
    void settingsRequested();
    void selectionReady();
protected:
    void keyPressEvent(QKeyEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
private:
    QLabel *label;
};
