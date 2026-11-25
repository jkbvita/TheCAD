#include "ui/startup/WelcomeWindow.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace thecad::ui {
namespace {
QIcon documentIcon(bool assembly) {
    QPixmap image(96, 96); image.fill(Qt::transparent); QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing); p.scale(2, 2);
    const auto cube = [&](int x, int y, int s) {
        p.setPen(QPen(QColor("#93bfdc"), 1));
        p.setBrush(QColor("#83b5d6")); p.drawPolygon(QPolygonF{{qreal(x),qreal(y+s/2.)},{qreal(x+s),qreal(y)},{qreal(x+2*s),qreal(y+s/2.)},{qreal(x+s),qreal(y+s)}});
        p.setBrush(QColor("#426985")); p.drawPolygon(QPolygonF{{qreal(x),qreal(y+s/2.)},{qreal(x+s),qreal(y+s)},{qreal(x+s),qreal(y+2*s)},{qreal(x),qreal(y+1.5*s)}});
        p.setBrush(QColor("#638fae")); p.drawPolygon(QPolygonF{{qreal(x+s),qreal(y+s)},{qreal(x+2*s),qreal(y+s/2.)},{qreal(x+2*s),qreal(y+1.5*s)},{qreal(x+s),qreal(y+2*s)}});
    };
    if (assembly) { cube(17, 5, 10); cube(4, 20, 10); cube(25, 24, 8); }
    else cube(7, 7, 17);
    return QIcon(image);
}
QLabel* heading(const QString& text) {
    auto* label = new QLabel(text); auto font = label->font(); font.setPointSize(12); font.setBold(true); label->setFont(font); return label;
}
}
WelcomeWindow::WelcomeWindow(QSettings& settings, QWidget* parent)
    : QDialog(parent), m_recent(settings) {
    setObjectName("welcomeWindow"); setWindowTitle("Welcome to TheCAD");
    resize(940, 700); setMinimumSize(700, 500);
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size() - QSize(40, 60)));
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(24, 20, 24, 16); layout->setSpacing(16);
    auto* header = new QHBoxLayout;
    auto* mark = new QLabel; mark->setPixmap(QPixmap(":/branding/thecad-mark.png").scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    header->addWidget(mark); header->addSpacing(8);
    auto* title = heading("Welcome to TheCAD"); auto font = title->font(); font.setPointSize(20); title->setFont(font);
    header->addWidget(title); header->addStretch(); layout->addLayout(header);
    m_tabs = new QTabWidget; m_tabs->setObjectName("welcomeTabs"); layout->addWidget(m_tabs, 1);
    auto* home = new QWidget; auto* homeLayout = new QVBoxLayout(home); homeLayout->setContentsMargins(16, 16, 16, 16); homeLayout->setSpacing(14);
    auto* homeScroll = new QScrollArea; homeScroll->setWidgetResizable(true); homeScroll->setWidget(home);
    m_tabs->addTab(homeScroll, "Home"); homeLayout->addWidget(heading("New document"));
    auto* choices = new QHBoxLayout;
    const auto choice = [&](const QString& name, const char* objectName, QIcon icon) {
        auto* button = new QPushButton(icon, name); button->setObjectName(objectName);
        button->setIconSize({44,44});
        // Override the application's compact toolbar-button rule for these cards.
        button->setStyleSheet("QPushButton { min-height:68px; padding:4px 10px; }");
        button->setAutoDefault(false); choices->addWidget(button, 1); return button;
    };
    auto* part = choice("Part", "welcomeNewPart", documentIcon(false));
    auto* assembly = choice("Assembly", "welcomeNewAssembly", documentIcon(true));
    auto* drawing = choice("Drawing\nComing later", "welcomeNewDrawing", style()->standardIcon(QStyle::SP_FileIcon));
    drawing->setEnabled(false); drawing->setToolTip("The Drawing workspace is not available yet.");
    connect(part, &QPushButton::clicked, this, [this] { emit newDocumentRequested(application::LaunchDocument::Part); });
    connect(assembly, &QPushButton::clicked, this, [this] { emit newDocumentRequested(application::LaunchDocument::Assembly); });
    homeLayout->addLayout(choices);
    auto* openButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), "Open document…");
    openButton->setObjectName("welcomeOpen"); openButton->setAutoDefault(false);
    connect(openButton, &QPushButton::clicked, this, [this] { browse(); }); homeLayout->addWidget(openButton, 0, Qt::AlignLeft);
    const auto tree = [&](const char* name, bool folders) {
        auto* list = new QTreeWidget; list->setObjectName(name); list->setRootIsDecorated(false); list->setAlternatingRowColors(true);
        list->setHeaderLabels(folders ? QStringList{"Folder", "Location"} : QStringList{"Document", "Type", "Location"});
        list->setIconSize({24,24}); list->setIndentation(0); list->setColumnWidth(0, 220);
        list->setSelectionMode(QAbstractItemView::SingleSelection); list->setUniformRowHeights(true);
        list->setContextMenuPolicy(Qt::CustomContextMenu); list->setMinimumHeight(100);
        connect(list, &QTreeWidget::itemClicked, this, [this, folders](QTreeWidgetItem* item) {
            const auto path = item->data(0, Qt::UserRole).toString();
            if (folders) {
                if (QFileInfo(path).isDir()) browse(path); else m_status->setText("Folder is unavailable: " + QDir::toNativeSeparators(path));
            } else open(path);
        });
        connect(list, &QTreeWidget::itemActivated, this, [this, folders](QTreeWidgetItem* item) {
            const auto path = item->data(0, Qt::UserRole).toString();
            if (folders) { if (QFileInfo(path).isDir()) browse(path); }
            else open(path);
        });
        connect(list, &QTreeWidget::customContextMenuRequested, this, [this,list,folders](const QPoint& point) { contextMenu(list, folders, point); });
        return list;
    };
    auto* recentHeading = new QHBoxLayout; recentHeading->addWidget(heading("Recent documents")); recentHeading->addStretch();
    auto* all = new QPushButton("View all"); all->setAutoDefault(false); connect(all,&QPushButton::clicked,this,&WelcomeWindow::showRecent); recentHeading->addWidget(all);
    homeLayout->addLayout(recentHeading); m_homeDocuments = tree("welcomeHomeDocuments", false); homeLayout->addWidget(m_homeDocuments, 2);
    homeLayout->addWidget(heading("Recent and pinned folders")); m_homeFolders = tree("welcomeHomeFolders", true); homeLayout->addWidget(m_homeFolders, 1);
    auto* recent = new QWidget; auto* recentLayout = new QVBoxLayout(recent); recentLayout->setContentsMargins(16,16,16,16);
    m_tabs->addTab(recent, "Recent");
    auto* filters = new QHBoxLayout; m_filter = new QLineEdit; m_filter->setObjectName("welcomeSearch"); m_filter->setPlaceholderText("Filter documents by name or folder"); m_filter->setClearButtonEnabled(true);
    m_type = new QComboBox; m_type->setObjectName("welcomeTypeFilter"); m_type->addItems({"All documents", "Parts", "Assemblies"}); filters->addWidget(m_filter,1); filters->addWidget(m_type); recentLayout->addLayout(filters);
    m_documents = tree("welcomeRecentDocuments", false); recentLayout->addWidget(m_documents, 2);
    recentLayout->addWidget(heading("Recent and pinned folders")); m_folders = tree("welcomeRecentFolders", true); recentLayout->addWidget(m_folders, 1);
    connect(m_filter, &QLineEdit::textChanged, this, [this] { populate(m_documents, false); });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] { populate(m_documents, false); });
    auto* footer = new QHBoxLayout;
    auto* addFolder = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), "Add folder…"); addFolder->setAutoDefault(false); addFolder->setObjectName("welcomeAddFolder");
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Choose a folder to pin");
        if (!path.isEmpty()) { m_recent.addFolder(path); refresh(); }
    });
    footer->addWidget(addFolder); footer->addStretch();
    auto* tip = new QLabel("Right-click a document or folder to pin or remove it."); tip->setWordWrap(true); tip->setObjectName("secondaryCaption"); footer->addWidget(tip);
    layout->addLayout(footer); m_status = new QLabel; m_status->setWordWrap(true); m_status->setObjectName("welcomeStatus"); layout->addWidget(m_status);
    refresh();
}
void WelcomeWindow::showRecent() { refresh(); m_tabs->setCurrentIndex(1); }
void WelcomeWindow::refresh() {
    populate(m_homeDocuments, false, 6); populate(m_homeFolders, true, 4);
    populate(m_documents, false); populate(m_folders, true);
    m_status->setText(m_recent.documents().empty() ? "Open or save a document to see it here." : "");
}
void WelcomeWindow::populate(QTreeWidget* tree, bool folders, int limit) {
    tree->clear(); int shown = 0;
    for (const auto& entry : folders ? m_recent.folders() : m_recent.documents()) {
        const auto kind = application::documentTypeForPath(entry.path); const bool assembly = kind == application::LaunchDocument::Assembly;
        if (tree == m_documents && (!entry.path.contains(m_filter->text(), Qt::CaseInsensitive) ||
            (m_type->currentIndex() == 1 && assembly) || (m_type->currentIndex() == 2 && !assembly))) continue;
        if (limit && shown++ >= limit) break;
        const QFileInfo info(entry.path); const bool exists = folders ? info.isDir() : info.isFile();
        const auto name = info.fileName().isEmpty() ? QDir::toNativeSeparators(entry.path) : info.fileName();
        auto* item = new QTreeWidgetItem(tree); item->setText(0, name + (entry.pinned ? "  · Pinned" : "") + (exists ? "" : "  · Missing"));
        item->setIcon(0, folders ? style()->standardIcon(QStyle::SP_DirIcon) : documentIcon(assembly));
        if (!folders) item->setText(1, assembly ? "Assembly" : "Part");
        item->setText(folders ? 1 : 2, QDir::toNativeSeparators(info.absolutePath()));
        item->setData(0, Qt::UserRole, entry.path); item->setData(0, Qt::UserRole + 1, entry.pinned);
        for (int column = 0; column < tree->columnCount(); ++column) item->setToolTip(column, QDir::toNativeSeparators(entry.path));
    }
}
void WelcomeWindow::open(const QString& path) {
    if (!QFileInfo(path).isFile()) { m_status->setText("Document is unavailable: " + QDir::toNativeSeparators(path)); return; }
    if (!application::documentTypeForPath(path)) { m_status->setText("Choose a .thepart, .tcpart or .theasmb document."); return; }
    emit openDocumentRequested(path);
}
void WelcomeWindow::browse(const QString& folder) {
    const auto path = QFileDialog::getOpenFileName(this, "Open document", folder, "TheCAD documents (*.thepart *.tcpart *.theasmb);;Parts (*.thepart *.tcpart);;Assemblies (*.theasmb)");
    if (!path.isEmpty()) open(path);
}
void WelcomeWindow::contextMenu(QTreeWidget* tree, bool folders, const QPoint& point) {
    QMenu menu(this); auto* item = tree->itemAt(point);
    if (item) {
        const auto path = item->data(0,Qt::UserRole).toString(); const bool pinned = item->data(0,Qt::UserRole+1).toBool();
        auto* openAction = menu.addAction(folders ? "Browse folder…" : "Open"); openAction->setEnabled(folders ? QFileInfo(path).isDir() : QFileInfo(path).isFile());
        connect(openAction,&QAction::triggered,this,[this,path,folders] { if(folders) browse(path); else open(path); });
        menu.addAction(pinned ? "Unpin" : "Pin",this,[this,path,folders,pinned] { m_recent.setPinned(path,folders,!pinned); refresh(); });
        if (!folders) menu.addAction("Show in folder",this,[path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
        menu.addAction("Remove from recent",this,[this,path,folders] { m_recent.remove(path,folders); refresh(); }); menu.addSeparator();
    }
    menu.addAction("Clear unpinned items",this,[this,folders] { m_recent.clearUnpinned(folders); refresh(); });
    menu.exec(tree->viewport()->mapToGlobal(point));
}
} // namespace thecad::ui
