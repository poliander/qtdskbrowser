#include "basicviewer.h"
#include "glyphs.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>

namespace {

// Each glyph in glyphTable() is an 8x8 pixel grid. It is scaled up by an
// integer factor on every axis - never through font/image interpolation -
// so each source pixel stays a crisp, uniform block. The vertical factor is
// double the horizontal one, matching the CPC's own non-square pixel rows.
const int kGlyphColumns = 8;
const int kGlyphRows = 8;
const int kScaleX = 2;
const int kScaleY = 4;
const int kCellWidth = kGlyphColumns * kScaleX;
const int kCellHeight = kGlyphRows * kScaleY;

// A margin the width/height of a single CPC pixel, left showing the paper
// (blue) background, framing the text on all sides.
const int kMarginX = kScaleX;
const int kMarginY = kScaleY;

// The Amstrad CPC's classic default screen colours: yellow text on blue.
const QColor kPaperColor(0, 0, 170);
const QColor kPenColor(255, 255, 0);

// Splits text into display rows, wrapping any line longer than maxColumns
// onto additional rows instead of letting it run off the edge.
QStringList wrapLines(const QString &text, int maxColumns)
{
    QStringList rows;
    for (const QString &line : text.split('\n')) {
        if (line.isEmpty()) {
            rows << line;
            continue;
        }
        for (int pos = 0; pos < line.size(); pos += maxColumns)
            rows << line.mid(pos, maxColumns);
    }
    return rows;
}

// Orders two text positions (as (column, row) points) by document order,
// i.e. by row first, then column within the row.
void normalizedSelection(QPoint a, QPoint b, QPoint *start, QPoint *end)
{
    if (a.y() > b.y() || (a.y() == b.y() && a.x() > b.x()))
        std::swap(a, b);
    *start = a;
    *end = b;
}

} // namespace

BasicViewer::BasicViewer(QWidget *parent)
    : QWidget(parent)
{
    QSizePolicy policy = sizePolicy();
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setFocusPolicy(Qt::ClickFocus);
    setCursor(Qt::IBeamCursor);

    rebuildImage(QSize(0, 0));
}

int BasicViewer::heightForWidth(int width) const
{
    if (width <= 0)
        return -1;
    int columns = qMax(1, (width - 2 * kMarginX) / kCellWidth);
    int rows = qMax(1, wrapLines(m_text, columns).size());
    return rows * kCellHeight + 2 * kMarginY;
}

void BasicViewer::setText(const QString &text)
{
    m_text = text;
    rebuildImage(size());
    updateGeometry();
    update();
}

void BasicViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildImage(event->size());
    updateGeometry();
    update();
}

void BasicViewer::rebuildImage(QSize widgetSize)
{
    QStringList lines;
    int columns;
    if (widgetSize.width() > 0) {
        // As many columns as fit the available width; the background and
        // the text wrapping both use this same width so the background is
        // never narrower than the text and never cut off.
        columns = qMax(1, (widgetSize.width() - 2 * kMarginX) / kCellWidth);
        lines = wrapLines(m_text, columns);
    } else {
        // No real width yet (before the first layout pass): show the
        // content unwrapped, sized to fit it.
        lines = m_text.split('\n');
        columns = 1;
        for (const QString &line : lines)
            columns = qMax(columns, line.size());
    }

    m_lines = lines;
    m_columns = columns;

    int contentRows = qMax(1, lines.size());
    int rows = qMax(contentRows, widgetSize.height() / kCellHeight);

    // At least as big as the widget itself, so the blue background always
    // reaches the window's right and bottom edges, even past a part-column
    // or part-row remainder and even when the listing is short or narrow.
    int imageWidth = qMax(columns * kCellWidth + 2 * kMarginX, widgetSize.width());
    int imageHeight = qMax(rows * kCellHeight + 2 * kMarginY, widgetSize.height());

    m_image = QImage(imageWidth, imageHeight, QImage::Format_RGB32);
    m_image.fill(kPaperColor);

    QPainter painter(&m_image);
    const QHash<char32_t, QVector<quint8>> &glyphs = glyphTable();

    for (int row = 0; row < lines.size(); ++row) {
        const QString &line = lines.at(row);
        for (int col = 0; col < line.size(); ++col) {
            auto it = glyphs.constFind(line.at(col).unicode());
            if (it == glyphs.constEnd())
                continue;

            int originX = kMarginX + col * kCellWidth;
            int originY = kMarginY + row * kCellHeight;
            for (int gy = 0; gy < kGlyphRows; ++gy) {
                quint8 bits = it.value().at(gy);
                for (int gx = 0; gx < kGlyphColumns; ++gx) {
                    if (bits & (1 << (7 - gx))) {
                        painter.fillRect(originX + gx * kScaleX, originY + gy * kScaleY,
                            kScaleX, kScaleY, kPenColor);
                    }
                }
            }
        }
    }
}

void BasicViewer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(0, 0, m_image);

    if (!m_hasSelection)
        return;

    QPoint start, end;
    normalizedSelection(m_selectionAnchor, m_selectionEnd, &start, &end);

    QColor highlight = palette().color(QPalette::Highlight);
    highlight.setAlpha(120);

    if (start.y() == end.y()) {
        painter.fillRect(kMarginX + start.x() * kCellWidth, kMarginY + start.y() * kCellHeight,
            (end.x() - start.x()) * kCellWidth, kCellHeight, highlight);
    } else {
        painter.fillRect(kMarginX + start.x() * kCellWidth, kMarginY + start.y() * kCellHeight,
            (m_columns - start.x()) * kCellWidth, kCellHeight, highlight);
        for (int row = start.y() + 1; row < end.y(); ++row)
            painter.fillRect(kMarginX, kMarginY + row * kCellHeight, m_columns * kCellWidth, kCellHeight, highlight);
        painter.fillRect(kMarginX, kMarginY + end.y() * kCellHeight, end.x() * kCellWidth, kCellHeight, highlight);
    }
}

QPoint BasicViewer::textPositionAt(const QPoint &pixelPos) const
{
    if (m_lines.isEmpty())
        return QPoint(0, 0);

    int row = qBound(0, (pixelPos.y() - kMarginY) / kCellHeight, m_lines.size() - 1);
    int col = qBound(0, (pixelPos.x() - kMarginX) / kCellWidth, m_lines.at(row).size());
    return QPoint(col, row);
}

QString BasicViewer::selectedText() const
{
    if (!m_hasSelection)
        return QString();

    QPoint start, end;
    normalizedSelection(m_selectionAnchor, m_selectionEnd, &start, &end);

    if (start.y() == end.y())
        return m_lines.at(start.y()).mid(start.x(), end.x() - start.x());

    QStringList result;
    result << m_lines.at(start.y()).mid(start.x());
    for (int row = start.y() + 1; row < end.y(); ++row)
        result << m_lines.at(row);
    result << m_lines.at(end.y()).left(end.x());
    return result.join('\n');
}

void BasicViewer::copySelectionToClipboard()
{
    QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void BasicViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus();
    m_selecting = true;
    m_hasSelection = false;
    m_selectionAnchor = textPositionAt(event->pos());
    m_selectionEnd = m_selectionAnchor;
    update();
}

void BasicViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    m_selectionEnd = textPositionAt(event->pos());
    m_hasSelection = (m_selectionAnchor != m_selectionEnd);
    update();
}

void BasicViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_selecting) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_selecting = false;
    m_selectionEnd = textPositionAt(event->pos());
    m_hasSelection = (m_selectionAnchor != m_selectionEnd);
    update();
}

void BasicViewer::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        if (!m_lines.isEmpty()) {
            m_selectionAnchor = QPoint(0, 0);
            m_selectionEnd = QPoint(m_lines.last().size(), m_lines.size() - 1);
            m_hasSelection = true;
            update();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
