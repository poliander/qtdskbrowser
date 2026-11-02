#include "dskimage.h"

#include <QMap>
#include <algorithm>

namespace {

// AMSDOS "protected" BASIC (SAVE ",p") scrambles the tokenised program with a
// position-dependent XOR of two fixed tables, taken from the AMSDOS ROM at
// &D252. The same operation applied twice restores the original bytes.
void unprotectBasic(QByteArray &data)
{
    static const quint8 table1[] = { 0xe2, 0x9d, 0xdb, 0x1a, 0x42, 0x29, 0x39,
                                      0xc6, 0xb3, 0xc6, 0x90, 0x45, 0x8a };
    static const quint8 table2[] = { 0x49, 0xb1, 0x36, 0xf0, 0x2e, 0x1e, 0x06,
                                      0x2a, 0x28, 0x19, 0xea };

    for (int i = 0; i < data.size(); ++i) {
        int index = i & 0x7f;
        quint8 byte = static_cast<quint8>(data.at(i));
        byte ^= table1[index % 13] ^ table2[index % 11];
        data[i] = static_cast<char>(byte);
    }
}

} // namespace

bool DskImage::load(const QByteArray &rawData, QString *errorMessage)
{
    m_tracks.clear();
    m_files.clear();

    const QByteArray standardSignature = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    const QByteArray extendedSignature = "EXTENDED CPC DSK File\r\nDisk-Info\r\n";

    if (rawData.size() < 256) {
        if (errorMessage)
            *errorMessage = "The file is too small to be a valid DSK disk image.";
        return false;
    }

    if (rawData.startsWith(standardSignature)) {
        m_extended = false;
    } else if (rawData.startsWith(extendedSignature)) {
        m_extended = true;
    } else {
        if (errorMessage)
            *errorMessage = "This is not a recognized DSK disk image (signature missing).";
        return false;
    }

    m_trackCount = static_cast<quint8>(rawData.at(0x30));
    m_sideCount = static_cast<quint8>(rawData.at(0x31));
    if (m_sideCount == 0)
        m_sideCount = 1;

    const int trackTableSize = m_trackCount * m_sideCount;
    QVector<quint16> trackSizes;
    trackSizes.reserve(trackTableSize);

    if (m_extended) {
        for (int i = 0; i < trackTableSize; ++i) {
            quint8 highByte = static_cast<quint8>(rawData.at(0x34 + i));
            trackSizes.append(static_cast<quint16>(highByte) * 256);
        }
    } else {
        quint16 size = static_cast<quint8>(rawData.at(0x32))
                      | (static_cast<quint8>(rawData.at(0x33)) << 8);
        for (int i = 0; i < trackTableSize; ++i)
            trackSizes.append(size);
    }

    qint64 offset = 256;
    for (quint16 size : trackSizes) {
        if (size == 0)
            continue; // unformatted track, has no data in the file

        if (offset + size > rawData.size())
            break;

        QByteArray trackBlock = rawData.mid(offset, size);
        offset += size;

        if (!trackBlock.startsWith("Track-Info\r\n"))
            continue;

        Track track;
        track.trackNumber = static_cast<quint8>(trackBlock.at(0x10));
        track.side = static_cast<quint8>(trackBlock.at(0x11));
        quint8 sectorSizeCode = static_cast<quint8>(trackBlock.at(0x14));
        quint8 sectorCount = static_cast<quint8>(trackBlock.at(0x15));
        quint16 defaultSectorSize = 128 << sectorSizeCode;

        qint64 sectorDataOffset = 0x100;
        for (int s = 0; s < sectorCount; ++s) {
            int entryOffset = 0x18 + s * 8;
            if (entryOffset + 8 > trackBlock.size())
                break;

            quint8 sectorId = static_cast<quint8>(trackBlock.at(entryOffset + 2));
            quint16 actualLength = defaultSectorSize;
            if (m_extended) {
                actualLength = static_cast<quint8>(trackBlock.at(entryOffset + 6))
                              | (static_cast<quint8>(trackBlock.at(entryOffset + 7)) << 8);
            }

            if (sectorDataOffset + actualLength > trackBlock.size())
                break;

            Sector sector;
            sector.id = sectorId;
            sector.data = trackBlock.mid(sectorDataOffset, actualLength);
            track.sectors.append(sector);

            sectorDataOffset += actualLength;
        }

        m_tracks.append(track);
    }

    // The directory normally starts on track 0. Disks formatted with two reserved
    // system tracks (for AMSDOS/CP/M) instead keep it on track 2, so that is tried
    // as a fallback. A track that is entirely erased (0xE5 fill bytes) parses as a
    // trivially "valid" but empty directory, so that candidate is kept only if no
    // later candidate turns up actual files.
    bool directoryFound = false;
    for (int candidateTrack : {0, 2}) {
        QByteArray data = trackData(candidateTrack, 0);
        if (data.isEmpty())
            continue;
        if (parseDirectory(data.left(2048))) {
            directoryFound = true;
            m_directoryTrack = candidateTrack;
            if (!m_files.isEmpty())
                break;
        }
    }

    if (directoryFound) {
        buildDataArea();
        detectProtectedFiles();
        return true;
    }

    if (errorMessage)
        *errorMessage = "Could not find a readable directory on this disk image.";
    return false;
}

void DskImage::buildDataArea()
{
    QVector<Track> matching;
    for (const Track &track : m_tracks) {
        if (track.side == 0 && track.trackNumber >= m_directoryTrack)
            matching.append(track);
    }
    std::sort(matching.begin(), matching.end(), [](const Track &a, const Track &b) {
        return a.trackNumber < b.trackNumber;
    });

    m_dataArea.clear();
    for (const Track &track : matching)
        m_dataArea += trackData(track.trackNumber, track.side);
}

QByteArray DskImage::rawFileContent(int index) const
{
    if (index < 0 || index >= m_fileBlocks.size())
        return QByteArray();

    // Each allocation block is 1 KiB; this matches the standard CPC disk formats
    // that this parser otherwise assumes (single-byte allocation numbers).
    static const qint64 blockSize = 1024;

    QByteArray content;
    for (quint8 block : m_fileBlocks.at(index)) {
        qint64 blockOffset = static_cast<qint64>(block) * blockSize;
        if (blockOffset + blockSize > m_dataArea.size())
            break;
        content += m_dataArea.mid(blockOffset, blockSize);
    }

    quint32 size = m_files.at(index).size;
    if (static_cast<quint32>(content.size()) > size)
        content.truncate(size);

    return content;
}

void DskImage::detectProtectedFiles()
{
    for (int i = 0; i < m_files.size(); ++i) {
        QByteArray content = rawFileContent(i);
        if (content.size() < 128)
            continue;

        quint32 checksum = 0;
        for (int j = 0; j < 67; ++j)
            checksum += static_cast<quint8>(content.at(j));
        quint16 storedChecksum = static_cast<quint8>(content.at(67))
                                | (static_cast<quint8>(content.at(68)) << 8);
        if ((checksum & 0xFFFF) == storedChecksum && static_cast<quint8>(content.at(18)) == 1)
            m_files[i].protectedFile = true;
    }
}

QByteArray DskImage::fileContent(int index, bool *isTokenizedBasic) const
{
    if (isTokenizedBasic)
        *isTokenizedBasic = false;

    QByteArray content = rawFileContent(index);

    // Files saved as tokenized BASIC or binary carry a 128-byte AMSDOS header in
    // front of the actual data. Its presence is reliably detected via the header's
    // own checksum (the sum of its first 67 bytes, stored at offset 67-68).
    if (content.size() >= 128) {
        quint32 checksum = 0;
        for (int i = 0; i < 67; ++i)
            checksum += static_cast<quint8>(content.at(i));
        quint16 storedChecksum = static_cast<quint8>(content.at(67))
                                | (static_cast<quint8>(content.at(68)) << 8);
        if ((checksum & 0xFFFF) == storedChecksum) {
            // AMSDOS file type at offset 18: 0 = tokenised BASIC, 1 = protected
            // BASIC, 2 = binary. Only those two BASIC types hold tokens.
            quint8 fileType = static_cast<quint8>(content.at(18));
            if (isTokenizedBasic)
                *isTokenizedBasic = (fileType == 0 || fileType == 1);
            content.remove(0, 128);
            if (fileType == 1)
                unprotectBasic(content);
        }
    }

    return content;
}

quint32 DskImage::usedSpace() const
{
    quint32 used = 0;
    for (const QVector<quint8> &blocks : m_fileBlocks)
        used += static_cast<quint32>(blocks.size()) * 1024;
    return used;
}

QByteArray DskImage::trackData(int trackNumber, int side) const
{
    for (const Track &track : m_tracks) {
        if (track.trackNumber != trackNumber || track.side != side)
            continue;

        QVector<Sector> sorted = track.sectors;
        std::sort(sorted.begin(), sorted.end(), [](const Sector &a, const Sector &b) {
            return a.id < b.id;
        });

        QByteArray result;
        for (const Sector &sector : sorted)
            result += sector.data;
        return result;
    }
    return QByteArray();
}

bool DskImage::parseDirectory(const QByteArray &directoryData)
{
    struct ExtentInfo
    {
        quint8 user;
        QString name;
        QString extension;
        bool readOnly;
        bool system;
        quint32 size;
        QMap<int, quint8> blocks; // allocation block numbers, keyed by position in the file
    };

    QVector<ExtentInfo> extents;
    QMap<QString, int> indexByKey;

    const int entryCount = directoryData.size() / 32;
    int plausibleEntries = 0;

    for (int i = 0; i < entryCount; ++i) {
        const char *raw = directoryData.constData() + i * 32;
        quint8 user = static_cast<quint8>(raw[0]);

        if (user == 0xE5) {
            // Empty/deleted directory slot.
            plausibleEntries++;
            continue;
        }
        if (user > 15)
            return false; // not a plausible directory entry

        QString name;
        QString extension;
        bool readOnly = false;
        bool system = false;

        for (int c = 0; c < 8; ++c) {
            quint8 ch = static_cast<quint8>(raw[1 + c]) & 0x7F;
            if (ch != ' ' && (ch < 0x20 || ch > 0x7E))
                return false;
            name += QChar(ch);
        }
        for (int c = 0; c < 3; ++c) {
            quint8 rawCh = static_cast<quint8>(raw[9 + c]);
            quint8 ch = rawCh & 0x7F;
            if (ch != ' ' && (ch < 0x20 || ch > 0x7E))
                return false;
            extension += QChar(ch);
            if (c == 0)
                readOnly = rawCh & 0x80;
            else if (c == 1)
                system = rawCh & 0x80;
        }

        name = name.trimmed();
        extension = extension.trimmed();

        quint8 extentLow = static_cast<quint8>(raw[12]);
        quint8 extentHigh = static_cast<quint8>(raw[14]);
        quint8 recordCount = static_cast<quint8>(raw[15]);

        int extentNumber = extentLow + extentHigh * 32;
        // Each directory extent covers up to 16 KiB (128 records of 128 bytes); the
        // final extent of a file carries the exact record count, so the largest
        // value found for a given file is its total size.
        quint32 estimatedSize = static_cast<quint32>(extentNumber) * 16384
                               + static_cast<quint32>(recordCount) * 128;

        // Allocation block numbers (AL0-AL15) used by this extent; how many of them
        // actually hold data follows from the record count (8 records = 1 KiB block).
        int blocksUsed = std::min(16, (recordCount + 7) / 8);

        QString key = QString("%1|%2|%3").arg(user).arg(name, extension);
        auto it = indexByKey.find(key);
        int entryIndex;
        if (it == indexByKey.end()) {
            indexByKey.insert(key, extents.size());
            entryIndex = extents.size();
            extents.append({ user, name, extension, readOnly, system, estimatedSize, {} });
        } else {
            entryIndex = it.value();
            extents[entryIndex].size = std::max(extents[entryIndex].size, estimatedSize);
        }

        for (int b = 0; b < blocksUsed; ++b) {
            quint8 blockNumber = static_cast<quint8>(raw[16 + b]);
            extents[entryIndex].blocks.insert(extentNumber * 16 + b, blockNumber);
        }

        plausibleEntries++;
    }

    if (entryCount == 0 || plausibleEntries == 0)
        return false;

    std::sort(extents.begin(), extents.end(), [](const ExtentInfo &a, const ExtentInfo &b) {
        return (a.name + a.extension) < (b.name + b.extension);
    });

    m_files.clear();
    m_fileBlocks.clear();
    for (const ExtentInfo &info : extents) {
        DskFileEntry entry;
        entry.user = info.user;
        entry.name = info.name;
        entry.extension = info.extension;
        entry.readOnly = info.readOnly;
        entry.system = info.system;
        entry.size = info.size;
        m_files.append(entry);
        QVector<quint8> blocks;
        for (quint8 block : info.blocks)
            blocks.append(block);
        m_fileBlocks.append(blocks);
    }

    return true;
}
