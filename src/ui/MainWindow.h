#pragma once

#include <QMainWindow>
#include <TopoDS_Shape.hxx>

namespace thecad::ui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const TopoDS_Shape& shape, QWidget* parent = nullptr);
};

} // namespace thecad::ui
