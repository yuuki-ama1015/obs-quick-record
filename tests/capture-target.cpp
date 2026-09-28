// Manual integration fixture: static, recognizable pixels for capture / Auto Stop testing.
#include <QApplication>
#include <QWidget>
#include <QPainter>
class Pattern : public QWidget {
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(0, 0, width()/2, height()/2, QColor("#c03030"));
        p.fillRect(width()/2, 0, width(), height()/2, QColor("#20a050"));
        p.fillRect(0, height()/2, width()/2, height(), QColor("#3050c0"));
        p.fillRect(width()/2, height()/2, width(), height(), QColor("#e0c040"));
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 24));
        p.drawText(rect(), Qt::AlignCenter, "Quick Record QA\nStatic capture target\nResize / move to test Window Capture");
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Pattern target;
    target.setWindowTitle("Quick Record QA target");
    target.resize(1000, 700);
    target.show();
    return app.exec();
}
