#include "detokenizer.h"

#include <QHash>
#include <cmath>

namespace {

// Locomotive BASIC token tables, as used by AMSDOS/the CPC firmware ROM.
// Codes 0x80-0xFE are "primary" one-byte tokens for statements, functions and
// operators; 0xFF is a prefix introducing a "secondary" token (another byte,
// looked up in a separate table) for the remaining built-in functions.

const QHash<quint8, QString> &primaryTokens()
{
    static const QHash<quint8, QString> table = {
        { 0x80, "AFTER" }, { 0x81, "AUTO" }, { 0x82, "BORDER" }, { 0x83, "CALL" },
        { 0x84, "CAT" }, { 0x85, "CHAIN" }, { 0x86, "CLEAR" }, { 0x87, "CLG" },
        { 0x88, "CLOSEIN" }, { 0x89, "CLOSEOUT" }, { 0x8A, "CLS" }, { 0x8B, "CONT" },
        { 0x8C, "DATA" }, { 0x8D, "DEF" }, { 0x8E, "DEFINT" }, { 0x8F, "DEFREAL" },
        { 0x90, "DEFSTR" }, { 0x91, "DEG" }, { 0x92, "DELETE" }, { 0x93, "DIM" },
        { 0x94, "DRAW" }, { 0x95, "DRAWR" }, { 0x96, "EDIT" }, { 0x97, "ELSE" },
        { 0x98, "END" }, { 0x99, "ENT" }, { 0x9A, "ENV" }, { 0x9B, "ERASE" },
        { 0x9C, "ERROR" }, { 0x9D, "EVERY" }, { 0x9E, "FOR" }, { 0x9F, "GOSUB" },
        { 0xA0, "GOTO" }, { 0xA1, "IF" }, { 0xA2, "INK" }, { 0xA3, "INPUT" },
        { 0xA4, "KEY" }, { 0xA5, "LET" }, { 0xA6, "LINE" }, { 0xA7, "LIST" },
        { 0xA8, "LOAD" }, { 0xA9, "LOCATE" }, { 0xAA, "MEMORY" }, { 0xAB, "MERGE" },
        { 0xAC, "MID$" }, { 0xAD, "MODE" }, { 0xAE, "MOVE" }, { 0xAF, "MOVER" },
        { 0xB0, "NEXT" }, { 0xB1, "NEW" }, { 0xB2, "ON" }, { 0xB3, "ON BREAK" },
        { 0xB4, "ON ERROR GOTO" }, { 0xB5, "ON SQ" }, { 0xB6, "OPENIN" },
        { 0xB7, "OPENOUT" }, { 0xB8, "ORIGIN" }, { 0xB9, "OUT" }, { 0xBA, "PAPER" },
        { 0xBB, "PEN" }, { 0xBC, "PLOT" }, { 0xBD, "PLOTR" }, { 0xBE, "POKE" },
        { 0xBF, "PRINT" }, { 0xC0, "'" }, { 0xC1, "RAD" }, { 0xC2, "RANDOMIZE" },
        { 0xC3, "READ" }, { 0xC4, "RELEASE" }, { 0xC5, "REM" }, { 0xC6, "RENUM" },
        { 0xC7, "RESTORE" }, { 0xC8, "RESUME" }, { 0xC9, "RETURN" }, { 0xCA, "RUN" },
        { 0xCB, "SAVE" }, { 0xCC, "SOUND" }, { 0xCD, "SPEED" }, { 0xCE, "STOP" },
        { 0xCF, "SYMBOL" }, { 0xD0, "TAG" }, { 0xD1, "TAGOFF" }, { 0xD2, "TROFF" },
        { 0xD3, "TRON" }, { 0xD4, "WAIT" }, { 0xD5, "WEND" }, { 0xD6, "WHILE" },
        { 0xD7, "WIDTH" }, { 0xD8, "WINDOW" }, { 0xD9, "WRITE" }, { 0xDA, "ZONE" },
        { 0xDB, "DI" }, { 0xDC, "EI" }, { 0xDD, "FILL" }, { 0xDE, "GRAPHICS" },
        { 0xDF, "MASK" }, { 0xE0, "FRAME" }, { 0xE1, "CURSOR" }, { 0xE3, "ERL" },
        { 0xE4, "FN" }, { 0xE5, "SPC" }, { 0xE6, "STEP" }, { 0xE7, "SWAP" },
        { 0xEA, "TAB" }, { 0xEB, "THEN" }, { 0xEC, "TO" }, { 0xED, "USING" },
        { 0xEE, ">" }, { 0xEF, "=" }, { 0xF0, ">=" }, { 0xF1, "<" }, { 0xF2, "<>" },
        { 0xF3, "<=" }, { 0xF4, "+" }, { 0xF5, "-" }, { 0xF6, "*" }, { 0xF7, "/" },
        { 0xF8, "^" }, { 0xF9, "\\" }, { 0xFA, "AND" }, { 0xFB, "MOD" },
        { 0xFC, "OR" }, { 0xFD, "XOR" }, { 0xFE, "NOT" },
    };
    return table;
}

const QHash<quint8, QString> &secondaryTokens()
{
    static const QHash<quint8, QString> table = {
        { 0x00, "ABS" }, { 0x01, "ASC" }, { 0x02, "ATN" }, { 0x03, "CHR$" },
        { 0x04, "CINT" }, { 0x05, "COS" }, { 0x06, "CREAL" }, { 0x07, "EXP" },
        { 0x08, "FIX" }, { 0x09, "FRE" }, { 0x0A, "INKEY" }, { 0x0B, "INP" },
        { 0x0C, "INT" }, { 0x0D, "JOY" }, { 0x0E, "LEN" }, { 0x0F, "LOG" },
        { 0x10, "LOG10" }, { 0x11, "LOWER$" }, { 0x12, "PEEK" }, { 0x13, "REMAIN" },
        { 0x14, "SGN" }, { 0x15, "SIN" }, { 0x16, "SPACE$" }, { 0x17, "SQ" },
        { 0x18, "SQR" }, { 0x19, "STR$" }, { 0x1A, "TAN" }, { 0x1B, "UNT" },
        { 0x1C, "UPPER$" }, { 0x1D, "VAL" },
        { 0x40, "EOF" }, { 0x41, "ERR" }, { 0x42, "HIMEM" }, { 0x43, "INKEY$" },
        { 0x44, "PI" }, { 0x45, "RND" }, { 0x46, "TIME" }, { 0x47, "XPOS" },
        { 0x48, "YPOS" }, { 0x49, "DERR" },
        { 0x71, "BIN$" }, { 0x72, "DEC$" }, { 0x73, "HEX$" }, { 0x74, "INSTR" }, { 0x75, "LEFT$" },
        { 0x76, "MAX" }, { 0x77, "MIN" }, { 0x78, "POS" }, { 0x79, "RIGHT$" },
        { 0x7A, "ROUND" }, { 0x7B, "STRING$" }, { 0x7C, "TEST" }, { 0x7D, "TESTR" },
        { 0x7E, "COPYCHR$" }, { 0x7F, "VPOS" },
    };
    return table;
}

// A character that a keyword/number boundary should not be glued to without
// a separating space (letters, digits and the variable suffix characters).
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == '$' || c == '%' || c == '!';
}

// Appends `text` to `line`, inserting a single space first if omitting it
// would otherwise glue two word-like runs together (e.g. "FOR" and "I", or
// "TO" and "10"). This reproduces the spacing that LISTing a tokenised
// program on a real CPC produces, without needing to special-case every
// keyword.
void appendToken(QString &line, const QString &text)
{
    if (text.isEmpty())
        return;
    if (!line.isEmpty() && isWordChar(line.back()) && isWordChar(text.front()))
        line += ' ';
    line += text;
}

quint16 readUInt16(const QByteArray &data, int offset)
{
    return static_cast<quint8>(data.at(offset)) | (static_cast<quint8>(data.at(offset + 1)) << 8);
}

// Decodes a CPC BASIC 5-byte floating point number: 4 bytes of normalized
// mantissa (bytes 0-3, least significant byte first) with the sign in the
// top bit of byte 3 in place of the mantissa's implicit leading one, and a
// byte-4 exponent biased by 128.
double decodeFloat(const QByteArray &data, int offset)
{
    quint8 b0 = static_cast<quint8>(data.at(offset));
    quint8 b1 = static_cast<quint8>(data.at(offset + 1));
    quint8 b2 = static_cast<quint8>(data.at(offset + 2));
    quint8 b3 = static_cast<quint8>(data.at(offset + 3));
    quint8 b4 = static_cast<quint8>(data.at(offset + 4));

    if (b4 == 0)
        return 0.0;

    bool negative = (b3 & 0x80) != 0;
    quint32 mantissa = (static_cast<quint32>(b3 | 0x80) << 24) | (static_cast<quint32>(b2) << 16)
                      | (static_cast<quint32>(b1) << 8) | static_cast<quint32>(b0);
    int exponent = static_cast<int>(b4) - 128;

    double value = (static_cast<double>(mantissa) / 4294967296.0) * std::pow(2.0, exponent);
    return negative ? -value : value;
}

QString formatFloat(double value)
{
    if (value == 0.0)
        return "0";
    return QString::number(value, 'g', 10);
}

// Reads a bit7-terminated run of bytes starting at tokens[start]: each byte
// is masked with 0x7F and appended, stopping after the first byte whose top
// bit is set. Returns false (leaving `name` partially filled) if the run
// runs off the end of `tokens` without finding a terminator.
bool readBitTerminatedName(const QByteArray &tokens, int start, QString &name, int &next)
{
    int j = start;
    while (j < tokens.size()) {
        quint8 ch = static_cast<quint8>(tokens.at(j));
        name += QChar(ch & 0x7F);
        ++j;
        if (ch & 0x80) {
            next = j;
            return true;
        }
    }
    return false;
}

// Decodes the tokenised content of a single BASIC line (the part after its
// 2-byte length and 2-byte line number) into readable text.
QString detokenizeLine(const QByteArray &tokens)
{
    QString line;
    QString rawRun; // consecutive plain-ASCII characters (identifiers, string/REM text)

    auto flushRawRun = [&]() {
        if (!rawRun.isEmpty()) {
            appendToken(line, rawRun);
            rawRun.clear();
        }
    };

    bool inString = false; // between an opening and closing '"': bytes are never tokens

    int i = 0;
    while (i < tokens.size()) {
        quint8 code = static_cast<quint8>(tokens.at(i));

        if (inString) {
            rawRun += QChar(code);
            ++i;
            if (code == '"')
                inString = false;
            continue;
        }

        if (code == 0x7C) {
            // The "|" RSX command prefix: a 1-byte offset (to the tokens
            // following the command, not needed for display) is followed by
            // the command name, bit7-terminated like a variable name.
            if (i + 1 >= tokens.size())
                break;
            QString name;
            int next;
            if (!readBitTerminatedName(tokens, i + 2, name, next))
                break;
            flushRawRun();
            appendToken(line, "|" + name);
            i = next;
            continue;
        }

        if (code >= 0x20 && code <= 0x7E) {
            rawRun += QChar(code);
            if (code == '"')
                inString = true;
            ++i;
            continue;
        }

        if (code == 0x00) {
            break; // end of line
        }
        if (code == 0x01) {
            // The "'" comment token (0xC0) and ELSE (0x97) are always stored
            // with this separator immediately before them, even when no
            // colon was actually typed (e.g. a comment as a line's only
            // statement, or IF...THEN...ELSE); LIST does not reproduce that
            // implicit colon, so skip it too.
            quint8 next = i + 1 < tokens.size() ? static_cast<quint8>(tokens.at(i + 1)) : 0;
            if (next == 0xC0 || next == 0x97) {
                ++i;
            } else {
                flushRawRun();
                appendToken(line, ":");
                ++i;
            }
        } else if (code == 0x09) {
            flushRawRun();
            line += QChar(0x09);
            ++i;
        } else if ((code >= 0x02 && code <= 0x04) || (code >= 0x0B && code <= 0x0D)) {
            // A variable reference: a 2-byte runtime address cache (always 0 in
            // a saved, never-yet-run program, and not needed for display) is
            // followed by the variable's name, one character per byte, with the
            // top bit of its last character set to mark where the name ends.
            // Codes 0x02-0x04 additionally mark an explicit %/$/! type suffix.
            if (i + 2 >= tokens.size())
                break;
            QString name;
            int j;
            if (!readBitTerminatedName(tokens, i + 3, name, j))
                break; // truncated/corrupt data
            if (code == 0x02)
                name += '%';
            else if (code == 0x03)
                name += '$';
            else if (code == 0x04)
                name += '!';
            flushRawRun();
            appendToken(line, name);
            i = j;
        } else if (code >= 0x0E && code <= 0x18) {
            flushRawRun();
            appendToken(line, QString::number(code - 0x0E));
            ++i;
        } else if (code == 0x19) {
            if (i + 1 >= tokens.size())
                break;
            quint8 value = static_cast<quint8>(tokens.at(i + 1));
            flushRawRun();
            appendToken(line, QString::number(value));
            i += 2;
        } else if (code == 0x1A || code == 0x1E) {
            if (i + 2 >= tokens.size())
                break;
            quint16 value = readUInt16(tokens, i + 1);
            flushRawRun();
            appendToken(line, QString::number(value));
            i += 3;
        } else if (code == 0x1B) {
            if (i + 2 >= tokens.size())
                break;
            quint16 value = readUInt16(tokens, i + 1);
            flushRawRun();
            appendToken(line, "&X" + QString::number(value, 2).toUpper());
            i += 3;
        } else if (code == 0x1C) {
            if (i + 2 >= tokens.size())
                break;
            quint16 value = readUInt16(tokens, i + 1);
            flushRawRun();
            appendToken(line, "&" + QString::number(value, 16).toUpper());
            i += 3;
        } else if (code == 0x1D) {
            // A BASIC program line's memory address, standing in for a line
            // number reference; this only occurs when tokens come from a raw
            // memory snapshot rather than a normal save, and resolving it back
            // to a line number needs that snapshot's memory layout, which
            // isn't available here, so nothing is rendered for it.
            i += 3;
        } else if (code == 0x1F) {
            if (i + 5 >= tokens.size())
                break;
            double value = decodeFloat(tokens, i + 1);
            flushRawRun();
            appendToken(line, formatFloat(value));
            i += 6;
        } else if (code == 0xFF) {
            if (i + 1 >= tokens.size())
                break;
            quint8 secondary = static_cast<quint8>(tokens.at(i + 1));
            flushRawRun();
            appendToken(line, secondaryTokens().value(secondary, QString("<FF %1>").arg(secondary, 2, 16, QChar('0'))));
            i += 2;
        } else if (code == 0xC5 || code == 0xC0) {
            // REM or the "'" shorthand: the rest of the line is a verbatim
            // comment, never tokenised, not even if it contains byte values
            // that would otherwise look like keyword tokens.
            flushRawRun();
            appendToken(line, primaryTokens().value(code));
            ++i;
            while (i < tokens.size())
                line += QChar(static_cast<quint8>(tokens.at(i++)));
        } else if (code >= 0x80) {
            flushRawRun();
            appendToken(line, primaryTokens().value(code, QString("<%1>").arg(code, 2, 16, QChar('0'))));
            ++i;
        } else {
            // Undocumented/unused control code; skip it rather than guessing
            // at a meaning that could misrepresent the program.
            ++i;
        }
    }

    flushRawRun();
    return line;
}

} // namespace

QString detokenizeBasic(const QByteArray &data)
{
    QStringList lines;
    int pos = 0;

    while (pos + 4 <= data.size()) {
        quint16 lineLength = readUInt16(data, pos);
        if (lineLength == 0)
            break; // end of program marker

        quint16 lineNumber = readUInt16(data, pos + 2);
        int lineEnd = pos + lineLength;
        if (lineEnd > data.size() || lineEnd <= pos + 4)
            break; // truncated/corrupt data

        QByteArray tokens = data.mid(pos + 4, lineEnd - (pos + 4) - 1); // minus the terminator byte
        lines << QString::number(lineNumber) + " " + detokenizeLine(tokens);

        pos = lineEnd;
    }

    return lines.join('\n');
}
