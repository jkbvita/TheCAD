#pragma once
#include "ui/MainWindow.h"
#include "ui/PropertyManagerPage.h"
#include <QList>
#include <array>
class QFormLayout;
class QSplitter;
namespace thecad::ui {
struct MainWindow::ReferenceDraft {
    modeling::DatumKind kind=modeling::DatumKind::Point;
    document::Feature feature;
    bool editing=false;
    bool invalidPreselection=false;
    document::PartDocument support;
    std::vector<document::ReferenceOption> options;
    PropertyManagerPage* page=nullptr;
    QSplitter* panel=nullptr;
    QSplitter* historySplitter=nullptr;
    QList<int> historySizes;
    QComboBox* method=nullptr;
    QCheckBox* automatic=nullptr;
    QComboBox* offsetConstraint=nullptr;
    QPushButton* reverse=nullptr;
    QFormLayout* referenceForm=nullptr;
    std::array<QComboBox*,3> references{};
    std::array<QDoubleSpinBox*,3> position{},direction{};
    QGroupBox *positionGroup=nullptr,*directionGroup=nullptr;
    QFormLayout* valueForm=nullptr;
    QDoubleSpinBox* value=nullptr;
    int activeReference=0;
    std::optional<document::PartDocument> candidate;
    std::string candidateId;
};
}
