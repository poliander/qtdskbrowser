#pragma once

#include <QByteArray>
#include <QImage>
#include <QWidget>

// Displays a QByteArray as a classic hex dump: one line per 16 bytes, each
// showing a hex offset, those 16 bytes as two-digit hex values, and the same
// 16 bytes again as their raw CPC character glyphs - using the same pixel
// font and yellow-on-blue colour scheme as BasicViewer (see glyphs.h).
//
// Unlike BasicViewer, lines are a fixed width and never wrap; the widget's
// size is fixed to fit its content, so a containing QScrollArea (without
// setWidgetResizable()) can scroll both ways instead.
class HexViewer : public QWidget
{
public:
    explicit HexViewer(QWidget *parent = nullptr);

    void setData(const QByteArray &data);

    QSize sizeHint() const override { return m_image.size(); }
    QSize minimumSizeHint() const override { return m_image.size(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildImage();

    QByteArray m_data;
    QImage m_image;
};
