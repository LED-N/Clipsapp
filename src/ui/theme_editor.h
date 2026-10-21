#pragma once
#include "theme.h"
#include <QWidget>
#include <functional>
class QComboBox;
class QCheckBox;
class QLabel;
namespace clipsapp {
class ColorSwatch;
class PresetTile;
// The appearance of the application: a theme to start from, then its colors and gradients, each
// shown at once in the whole window; and a code to share it.
class ThemeEditor final : public QWidget {
    Q_OBJECT
public:
    explicit ThemeEditor(QWidget* parent = nullptr);
    void setTheme(const ThemeColors& colors);
    ThemeColors theme() const { return m_colors; }
signals:
    // Each change, as it is made: while a color is being picked too.
    void changed(const ThemeColors& colors);
    void notice(const QString& title, const QString& detail);
private:
    void display(const ThemeColors& colors);
    void edit(const std::function<void(ThemeColors&)>& change);
    ThemeColors m_colors; bool m_loading = false;
    QList<PresetTile*> m_presets;
    ColorSwatch* m_background; ColorSwatch* m_accent; ColorSwatch* m_text; ColorSwatch* m_windowEnd; ColorSwatch* m_accentEnd;
    QCheckBox* m_windowGradient; QCheckBox* m_accentGradient; QComboBox* m_angle; QLabel* m_darkNote;
};
}
