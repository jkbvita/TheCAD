#include "ui/features/SplineParametersDialog.h"
#include "modeling/SketchSplines.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace thecad::ui {
bool editSplineParameters(modeling::SketchEntity& entity, QWidget* parent)
{
    QDialog dialog(parent); dialog.setObjectName("splineParametersDialog"); dialog.setWindowTitle("Spline fit points");
    auto* layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Edit fit-point coordinates in the sketch plane (mm)."));
    const auto points=modeling::splineFitPoints(entity);
    auto* table=new QTableWidget(int(points.size()),2); table->setObjectName("splineFitPoints");
    table->setHorizontalHeaderLabels({"U (mm)","V (mm)"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setMinimumSize(340,240); layout->addWidget(table);
    for(int row=0;row<int(points.size());++row)for(int column=0;column<2;++column) {
        auto* value=new QDoubleSpinBox;
        value->setObjectName(QString("splineFit%1%2").arg(row+1).arg(column?"Y":"X"));
        value->setAccessibleName(QString("Fit point %1 %2").arg(row+1).arg(column?"V":"U"));
        value->setDecimals(6); value->setRange(-1e9,1e9); value->setValue(column?points[std::size_t(row)].y:points[std::size_t(row)].x);
        table->setCellWidget(row,column,value);
    }
    auto* error=new QLabel; error->setObjectName("splineParametersError"); error->setWordWrap(true);
    error->setStyleSheet("color: #df6258"); error->hide(); layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons);
    std::optional<modeling::SketchEntity> replacement;
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        std::vector<modeling::Point2> values;
        for(int row=0;row<int(points.size());++row) {
            auto* x=static_cast<QDoubleSpinBox*>(table->cellWidget(row,0));
            auto* y=static_cast<QDoubleSpinBox*>(table->cellWidget(row,1));
            x->interpretText(); y->interpretText(); values.push_back({x->value(),y->value()});
        }
        try {
            replacement=modeling::interpolatingSpline(values,entity.id);
            replacement->construction=entity.construction; dialog.accept();
        } catch(const std::exception& failure) {error->setText(QString::fromUtf8(failure.what())); error->show();}
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return false;
    entity=std::move(*replacement); return true;
}
}
