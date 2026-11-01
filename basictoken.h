#pragma once

#include <QByteArray>
#include <QString>

// Converts a tokenised Locomotive BASIC program (as stored by AMSDOS on an
// Amstrad CPC disk, with any AMSDOS header already removed) back into
// readable BASIC source text, one line per program line.
QString detokenizeBasic(const QByteArray &data);
