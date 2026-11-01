#pragma once

#include <QImage>
#include <QWidget>

// Displays text using the CPC464 pixel font's own glyph bitmaps (see
// cpcglyphs.h), drawn directly pixel by pixel rather than through font
// rasterisation, each source pixel scaled up by an integer factor.
//
// Text wraps to fit however many columns the available width allows, with
// no fixed limit. The blue background always extends to fill at least the
// widget's current size, so it reaches the bottom of the window even when
// the listing itself is shorter. heightForWidth() lets a containing
// QScrollArea (with setWidgetResizable(true)) learn how tall the wrapped
// content needs to be for a given width *before* committing to a size, so
// it can show a vertical scrollbar instead of clipping overflowing content.
class CpcListingView : public QWidget
{
public:
    explicit CpcListingView(QWidget *parent = nullptr);

    void setText(const QString &text);

    QSize sizeHint() const override { return m_image.size(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuildImage(QSize widgetSize);
    QPoint textPositionAt(const QPoint &pixelPos) const;
    QString selectedText() const;
    void copySelectionToClipboard();

    QString m_text;
    QImage m_image;
    QStringList m_lines;
    int m_columns = 1;

    // The current selection, as (column, row) positions into m_lines.
    // m_selecting is true only while the mouse button is held down.
    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selectionAnchor;
    QPoint m_selectionEnd;
};
