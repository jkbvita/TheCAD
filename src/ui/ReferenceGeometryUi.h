#pragma once
#include "ui/MainWindow.h"
#include "ui/PropertyManagerPage.h"
#include <array>
class QFormLayout;
namespace thecad::ui {
struct MainWindow::ReferenceDraft {
    modeling::DatumKind kind=modeling::DatumKind::Point;
    document::Feature feature;
    bool editing=false;
    bool invalidPreselection=false;
    document::PartDocument support;
    std::vector<document::ReferenceOption> options;
    PropertyManagerPage* page=nullptr;
    QComboBox* method=nullptr;
    QCheckBox* automatic=nullptr;
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
