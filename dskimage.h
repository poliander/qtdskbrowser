#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// One file as listed in the disk's CP/M-style directory.
struct DskFileEntry
{
    quint8 user = 0;
    QString name;
    QString extension;
    bool readOnly = false;
    bool system = false;
    bool protectedFile = false; // saved with AMSDOS's SAVE ",p" (tokens are scrambled)
    quint32 size = 0; // in bytes, estimated from the directory extents
};

// Reads the "MV - CPCEMU Disk-File" / "EXTENDED CPC DSK File" image format used by
// Amstrad CPC emulators and extracts the files listed in its directory, including
// their actual content.
class DskImage
{
public:
    bool load(const QByteArray &rawData, QString *errorMessage = nullptr);

    quint8 trackCount() const { return m_trackCount; }
    quint8 sideCount() const { return m_sideCount; }
    const QVector<DskFileEntry> &files() const { return m_files; }

    // Reconstructs the content of files().at(index) by following its allocation
    // blocks, with a leading AMSDOS header stripped off if one is present. If
    // isTokenizedBasic is given, it is set to whether the AMSDOS header marks
    // this file as a tokenised (not ASCII-saved) BASIC program.
    QByteArray fileContent(int index, bool *isTokenizedBasic = nullptr) const;

private:
    struct Sector
    {
        quint8 id = 0;
        QByteArray data;
    };

    struct Track
    {
        quint8 trackNumber = 0;
        quint8 side = 0;
        QVector<Sector> sectors;
    };

    QByteArray trackData(int trackNumber, int side) const;
    bool parseDirectory(const QByteArray &directoryData);
    void buildDataArea();
    QByteArray rawFileContent(int index) const;
    void detectProtectedFiles();

    quint8 m_trackCount = 0;
    quint8 m_sideCount = 0;
    bool m_extended = false;
    int m_directoryTrack = 0;
    QVector<Track> m_tracks;
    QVector<DskFileEntry> m_files;
    QVector<QVector<quint8>> m_fileBlocks; // allocation block numbers, parallel to m_files
    QByteArray m_dataArea; // concatenated side-0 track data, starting at the directory track
};
