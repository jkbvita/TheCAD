#pragma once
#include <QObject>
#include <QColor>
#include <Quantity_Color.hxx>
namespace thecad::ui {
enum class ThemeMode { Dark, Light, System };
enum class ThemeColor { Defined, Underdefined, Construction, Reference, Error, Accent };
class ThemeManager final : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();
    ThemeMode mode() const {return m_mode;}
    bool isDark() const;
    void setMode(ThemeMode mode,bool persist=true);
signals:
    void changed();
private:
    ThemeManager();
    void apply();
    ThemeMode m_mode=ThemeMode::Dark;
};
QColor themeColor(ThemeColor role);
Quantity_Color viewportColor(ThemeColor role);
}
