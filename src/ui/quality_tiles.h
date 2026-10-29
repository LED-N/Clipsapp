#pragma once
#include <QPushButton>
#include <QSizeF>
namespace clipsapp {
// The quality of a capture in three choices, as the settings and the first launch offer them: the sizes,
// each drawn as a screen to scale; the frequencies; and the presets, from the lightest to the closest to
// the screen (their bitrate: presetBitrate).
struct VideoSize { int width, height; const char* name; const char* kind; QSizeF screen; };
inline const VideoSize videoSizes[]{{1920, 1080, "1080p", "Full HD", {22, 12.5}}, {2560, 1440, "1440p", "2K · QHD", {28, 16}}, {3840, 2160, "2160p", "4K · UHD", {36, 20.5}}};
inline constexpr int frameRates[]{30, 60, 120, 144};
struct QualityPreset { const char* key; const char* name; const char* text; };
inline const QualityPreset qualityPresets[]{
    {"light", "Léger", "Des clips légers, faciles à garder et à partager : les détails fins s'adoucissent dans les scènes les plus chargées."},
    {"balanced", "Équilibré", "Le bon compromis pour la plupart des jeux : une image nette, sans clips trop lourds."},
    {"high", "Haut", "Au plus près de l'écran, pour garder ou retravailler vos clips. Ils pèsent plus lourd, le replay demande plus de mémoire."}};
// The size offered closest to a screen of this height, never above it unless none is below.
int closestVideoSize(int screenHeight);
// A choice of the quality: its name, a line or two, and on its right what it stands for, a screen to
// scale or bars that fill up. Painted with the colors of the theme in use.
class QualityTile final : public QPushButton {
public:
    QualityTile(const QString& title, int height, QSizeF screen, int bars = 0);
    void setLines(const QString& first, const QString& second);
    QSize sizeHint() const override { return {120, height()}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_title, m_first, m_second; QSizeF m_screen; int m_bars;
};
}
