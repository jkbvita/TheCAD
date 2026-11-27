#pragma once
#include <QDialog>
#include <QPointer>
namespace thecad::ui {
// The workspace stays transparent until its native viewport has drawn a frame.
// No artificial delay, timer-based progress, or geometry work in this widget.
class LoadingScreen final : public QDialog {
public:
    explicit LoadingScreen(const QString& message);
    void cover(QWidget* workspace);
    void finish();
protected:
    void reject() override {} // Construction cannot safely be canceled halfway through.
private:
    QPointer<QWidget> m_workspace;
};
}
