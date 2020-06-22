/* === This file is part of Calamares - <https://calamares.io> ===
 *
 *   SPDX-FileCopyrightText: 2014-2017 Teo Mrnjavac <teo@kde.org>
 *   SPDX-FileCopyrightText: 2017-2018 Adriaan de Groot <groot@kde.org>
 *   SPDX-FileCopyrightText: 2018-2019 Collabora Ltd <arnaud.ferraris@collabora.com>
 *   SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   Calamares is Free Software: see the License-Identifier above.
 *
 */

#include "GlobalStorage.h"
#include "JobQueue.h"

#include "utils/Logger.h"

#include "core/PartitionLayout.h"

#include "core/KPMHelpers.h"
#include "core/PartUtils.h"
#include "core/PartitionActions.h"
#include "core/PartitionInfo.h"

#include "utils/Variant.h"

#include <kpmcore/core/device.h>
#include <kpmcore/core/partition.h>
#include <kpmcore/fs/filesystem.h>

static FileSystem::Type
getDefaultFileSystemType()
{
    Calamares::GlobalStorage* gs = Calamares::JobQueue::instance()->globalStorage();
    FileSystem::Type defaultFS = FileSystem::Ext4;

    if ( gs->contains( "defaultFileSystemType" ) )
    {
        PartUtils::findFS( gs->value( "defaultFileSystemType" ).toString(), &defaultFS );
        if ( defaultFS == FileSystem::Unknown )
        {
            defaultFS = FileSystem::Ext4;
        }
    }

    return defaultFS;
}

PartitionLayout::PartitionLayout()
    : m_defaultFsType( getDefaultFileSystemType() )
{
}

PartitionLayout::PartitionLayout( const PartitionLayout& layout )
    : m_defaultFsType( layout.m_defaultFsType )
    , m_partLayout( layout.m_partLayout )
{
}

PartitionLayout::~PartitionLayout() {}

PartitionLayout::PartitionEntry::PartitionEntry()
    : partAttributes( 0 )
{
}

PartitionLayout::PartitionEntry::PartitionEntry( const QString& mountPoint, const QString& size, const QString& minSize, const QString& maxSize )
    : partAttributes( 0 )
    , partMountPoint( mountPoint )
    , partSize( size )
    , partMinSize( minSize )
    , partMaxSize( maxSize )
{
}

PartitionLayout::PartitionEntry::PartitionEntry( const QString& label,
                                                 const QString& uuid,
                                                 const QString& type,
                                                 quint64 attributes,
                                                 const QString& mountPoint,
                                                 const QString& fs,
                                                 const QVariantMap& features,
                                                 const QString& size,
                                                 const QString& minSize,
                                                 const QString& maxSize )
    : partLabel( label )
    , partUUID( uuid )
    , partType( type )
    , partAttributes( attributes )
    , partMountPoint( mountPoint )
    , partFeatures( features )
    , partSize( size )
    , partMinSize( minSize )
    , partMaxSize( maxSize )
{
    PartUtils::findFS( fs, &partFileSystem );
}

bool
PartitionLayout::addEntry( const PartitionEntry& entry )
{
    if ( !entry.isValid() )
    {
        return false;
    }

    m_partLayout.append( entry );

    return true;
}

void
PartitionLayout::init( const QVariantList& config )
{
    bool ok;

    m_partLayout.clear();

    for ( const auto& r : config )
    {
        QVariantMap pentry = r.toMap();

        if ( !pentry.contains( "name" ) || !pentry.contains( "mountPoint" ) || !pentry.contains( "filesystem" )
             || !pentry.contains( "size" ) )
        {
            cError() << "Partition layout entry #" << config.indexOf( r )
                     << "lacks mandatory attributes, switching to default layout.";
            m_partLayout.clear();
            break;
        }

        if ( !addEntry( { CalamaresUtils::getString( pentry, "name" ),
                          CalamaresUtils::getString( pentry, "uuid" ),
                          CalamaresUtils::getString( pentry, "type" ),
                          CalamaresUtils::getUnsignedInteger( pentry, "attributes", 0 ),
                          CalamaresUtils::getString( pentry, "mountPoint" ),
                          CalamaresUtils::getString( pentry, "filesystem" ),
                          CalamaresUtils::getSubMap( pentry, "features", ok ),
                          CalamaresUtils::getString( pentry, "size", QStringLiteral("0") ),
                          CalamaresUtils::getString( pentry, "minSize", QStringLiteral("0") ),
                          CalamaresUtils::getString( pentry, "maxSize", QStringLiteral("0") ) } ) )
        {
            cError() << "Partition layout entry #" << config.indexOf( r ) << "is invalid, switching to default layout.";
            m_partLayout.clear();
            break;
        }
    }

    if ( !m_partLayout.count() )
    {
        addEntry( { QString( "/" ), QString( "100%" ) } );
    }
}

QList< Partition* >
PartitionLayout::execute( Device* dev,
                          qint64 firstSector,
                          qint64 lastSector,
                          QString luksPassphrase,
                          PartitionNode* parent,
                          const PartitionRole& role )
{
    QList< Partition* > partList;
    qint64 currentSector = firstSector;
    qint64 availableSectors = lastSector - firstSector + 1;

    // TODO: Refine partition sizes to make sure there is room for every partition
    // Use a default (200-500M ?) minimum size for partition without minSize

    foreach ( const PartitionLayout::PartitionEntry& part, m_partLayout )
    {
        Partition* currentPartition = nullptr;
        qint64 sectors, minSectors = 0, maxSectors = availableSectors;

        // Calculate partition size (in sectors)
        sectors = part.size.toSectors( availableSectors, dev->logicalSize() );
        if ( sectors == -1 )
        {
            cWarning() << "Partition" << part.mountPoint << "size is invalid, ignoring...";
            continue;
        }
        minSectors = part.minSize.toSectors( availableSectors, dev->logicalSize() );
        if ( minSectors == -1 )
        {
            minSectors = 0;
        }
        maxSectors = part.maxSize.toSectors( availableSectors, dev->logicalSize() );
        if ( maxSectors == -1 )
        {
            maxSectors = availableSectors;
        }

        // Make sure we never go under min once converted to sectors
        maxSectors = std::max(maxSectors, minSectors);

        // Adjust partition size based on user-defined boundaries and available space
        sectors = std::max(sectors, minSectors);
        sectors = std::min(sectors, maxSectors);
        sectors = std::min(sectors, availableSectors);

        if ( luksPassphrase.isEmpty() )
        {
            currentPartition = KPMHelpers::createNewPartition(
                parent, *dev, role, part.fileSystem, currentSector, currentSector + sectors - 1, KPM_PARTITION_FLAG( None ) );
        }
        else
        {
            currentPartition = KPMHelpers::createNewEncryptedPartition(
                parent, *dev, role, part.fileSystem, currentSector, currentSector + sectors - 1, luksPassphrase, KPM_PARTITION_FLAG( None ) );
        }
        PartitionInfo::setFormat( currentPartition, true );
        PartitionInfo::setMountPoint( currentPartition, part.partMountPoint );
        if ( !part.partLabel.isEmpty() )
        {
            currentPartition->setLabel( part.partLabel );
            currentPartition->fileSystem().setLabel( part.partLabel );
        }
        if ( !part.partUUID.isEmpty() )
        {
            currentPartition->setUUID( part.partUUID );
        }
        if ( !part.partType.isEmpty() )
        {
#if defined( WITH_KPMCORE42API )
            currentPartition->setType( part.partType );
#else
            cWarning() << "Ignoring type; requires KPMcore >= 4.2.0.";
#endif
        }
        if ( part.partAttributes )
        {
#if defined( WITH_KPMCORE42API )
            currentPartition->setAttributes( part.partAttributes );
#else
            cWarning() << "Ignoring attributes; requires KPMcore >= 4.2.0.";
#endif
        }
        if ( !part.partFeatures.isEmpty() )
        {
#if defined( WITH_KPMCORE42API )
            for ( const auto& k : part.partFeatures.keys() )
            {
                currentPartition->fileSystem().addFeature( k, part.partFeatures.value( k ) );
            }
#else
            cWarning() << "Ignoring features; requires KPMcore >= 4.2.0.";
#endif
        }
        // Some buggy (legacy) BIOSes test if the bootflag of at least one partition is set.
        // Otherwise they ignore the device in boot-order, so add it here.
        partList.append( currentPartition );
        currentSector += sectors;
        availableSectors -= sectors;
    }

    return partList;
}
