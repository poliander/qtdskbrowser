#include "hexviewer.h"
#include "glyphs.h"

#include <QPainter>

namespace {

// Each glyph in glyphTable() is an 8x8 pixel grid, scaled up by an
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
// (blue) background, framing the dump on all sides.
const int kMarginX = kScaleX;
const int kMarginY = kScaleY;

// The Amstrad CPC's classic default screen colours: yellow text on blue.
const QColor kPaperColor(0, 0, 170);
const QColor kPenColor(255, 255, 0);

const int kBytesPerLine = 16;

// Formats one line of the dump: the offset, the 16 bytes starting there as
// two-digit hex values, and those same bytes again as raw characters -
// drawn via glyphTable() further down, so any byte value (0x00-0xFF)
// renders as its actual CPC ROM glyph rather than needing a placeholder for
// "unprintable" bytes.
QString formatLine(const QByteArray &data, int offset, int offsetDigits)
{
    QString line = QString("%1  ").arg(offset, offsetDigits, 16, QChar('0')).toUpper();

    QString chars;
    int lineLength = qMin(kBytesPerLine, data.size() - offset);
    for (int i = 0; i < kBytesPerLine; ++i) {
        if (i < lineLength) {
            quint8 byte = static_cast<quint8>(data.at(offset + i));
            line += QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
            chars += QChar(byte);
        } else {
            line += "   "; // pad a short final line so the character column still lines up
            chars += ' ';
        }
    }

    return line + " " + chars;
}

} // namespace

HexViewer::HexViewer(QWidget *parent)
    : QWidget(parent)
{
    rebuildImage();
}

void HexViewer::setData(const QByteArray &data)
{
    m_data = data;
    rebuildImage();
    updateGeometry();
    update();
}

void HexViewer::rebuildImage()
{
    int lineCount = qMax(1, (m_data.size() + kBytesPerLine - 1) / kBytesPerLine);
    int offsetDigits = qMax(4, QString::number(qMax(0, m_data.size() - 1), 16).size());

    QStringList lines;
    for (int i = 0; i < lineCount; ++i)
        lines << formatLine(m_data, i * kBytesPerLine, offsetDigits);

    int columns = 0;
    for (const QString &line : lines)
        columns = qMax(columns, line.size());

    int imageWidth = columns * kCellWidth + 2 * kMarginX;
    int imageHeight = lines.size() * kCellHeight + 2 * kMarginY;

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

void HexViewer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(0, 0, m_image);
}
