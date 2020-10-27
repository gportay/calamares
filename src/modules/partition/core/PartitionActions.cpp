/* === This file is part of Calamares - <https://calamares.io> ===
 *
 *   SPDX-FileCopyrightText: 2014-2017 Teo Mrnjavac <teo@kde.org>
 *   SPDX-FileCopyrightText: 2017-2019 Adriaan de Groot <groot@kde.org>
 *   SPDX-FileCopyrightText: 2019 Collabora Ltd <arnaud.ferraris@collabora.com>
 *   SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   Calamares is Free Software: see the License-Identifier above.
 *
 */

#include "PartitionActions.h"

#include "core/KPMHelpers.h"
#include "core/PartUtils.h"
#include "core/PartitionCoreModule.h"
#include "core/PartitionInfo.h"

#include "utils/CalamaresUtilsSystem.h"
#include "utils/NamedEnum.h"
#include "utils/Units.h"

#include "GlobalStorage.h"
#include "JobQueue.h"
#include "utils/Logger.h"

#include <kpmcore/core/device.h>
#include <kpmcore/core/partition.h>

#include <QDir>

namespace PartitionActions
{
using CalamaresUtils::operator""_GiB;
using CalamaresUtils::operator""_MiB;

qint64
swapSuggestion( const qint64 availableSpaceB, Config::SwapChoice swap )
{
    if ( ( swap != Config::SwapChoice::SmallSwap ) && ( swap != Config::SwapChoice::FullSwap ) )
    {
        return 0;
    }

    // See partition.conf for explanation
    qint64 suggestedSwapSizeB = 0;
    auto memory = CalamaresUtils::System::instance()->getTotalMemoryB();
    qint64 availableRamB = memory.first;
    qreal overestimationFactor = memory.second;

    bool ensureSuspendToDisk = swap == Config::SwapChoice::FullSwap;

    // Ramp up quickly to 8GiB, then follow memory size
    if ( availableRamB <= 4_GiB )
    {
        suggestedSwapSizeB = availableRamB * 2;
    }
    else if ( availableRamB <= 8_GiB )
    {
        suggestedSwapSizeB = 8_GiB;
    }
    else
    {
        suggestedSwapSizeB = availableRamB;
    }

    // .. top out at 8GiB if we don't care about suspend
    if ( !ensureSuspendToDisk )
    {
        suggestedSwapSizeB = qMin( 8_GiB, suggestedSwapSizeB );
    }


    // Allow for a fudge factor
    suggestedSwapSizeB *= overestimationFactor;

    // don't use more than 10% of available space
    if ( !ensureSuspendToDisk )
    {
        suggestedSwapSizeB = qMin( suggestedSwapSizeB, qint64( 0.10 * availableSpaceB ) );
    }

    cDebug() << "Suggested swap size:" << suggestedSwapSizeB / 1024. / 1024. / 1024. << "GiB";

    return suggestedSwapSizeB;
}

void
doAutopartition( PartitionCoreModule* core, Device* dev, Choices::AutoPartitionOptions o )
{
    Calamares::GlobalStorage* gs = Calamares::JobQueue::instance()->globalStorage();

    bool isEfi = PartUtils::isEfiSystem();

    // Partition sizes are expressed in MiB, should be multiples of
    // the logical sector size (usually 512B). EFI starts with 2MiB
    // empty and a EFI boot partition, while BIOS starts at
    // the 1MiB boundary (usually sector 2048).
    int empty_space_sizeB = isEfi ? 2_MiB : 1_MiB;

    // Since sectors count from 0, if the space is 2048 sectors in size,
    // the first free sector has number 2048 (and there are 2048 sectors
    // before that one, numbered 0..2047).
    qint64 firstFreeSector = CalamaresUtils::bytesToSectors( empty_space_sizeB, dev->logicalSize() );

    PartitionTable::TableType partType = PartitionTable::nameToTableType( o.defaultPartitionTableType );
    if ( partType == PartitionTable::unknownTableType )
    {
        partType = isEfi ? PartitionTable::gpt : PartitionTable::msdos;
    }

    core->createPartitionTable( dev, partType );

    if ( isEfi )
    {
        core->layoutAddEntry( gs->contains( "efiSystemPartitionName" ) ? gs->value( "efiSystemPartitionName" ).toString() : QString( "efi" ),
                              QString( "" ),
                              QString( "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" ),
                              0,
                              gs->contains( "efiSystemPartition" ) ? gs->value( "efiSystemPartition" ).toString() : QString( "/boot/efi" ),
                              QString( "FAT32" ),
                              { },
                              gs->contains( "efiSystemPartitionSize" ) ? gs->value( "efiSystemPartitionSize" ).toString() : QString( "300MiB" ),
                              QString( "0" ),
                              QString( "0" ),
                              false );
    }

    qint64 lastSectorForRoot = dev->totalLogical() - 1; // last sector of the device

    bool shouldCreateHome = gs->contains( "reuseHome" ).toBool();
    QString size = gs->contains( "homePartitionSize" ).toString();

    qint64 homeSizeB = 5_GiB;

    if ( shouldCreateHome )
    {
        lastSectorForRoot -= homeSizeB / dev->logicalSize() + 1;
    }

    const bool mayCreateSwap
        = ( o.swap == Config::SwapChoice::SmallSwap ) || ( o.swap == Config::SwapChoice::FullSwap );
    bool shouldCreateSwap = false;
    qint64 suggestedSwapSizeB = 0;

    if ( mayCreateSwap )
    {
        qint64 availableSpaceB = ( dev->totalLogical() - firstFreeSector ) * dev->logicalSize();
        suggestedSwapSizeB = swapSuggestion( availableSpaceB, o.swap );
        // Space required by this installation is what the distro claims is needed
        // (via global configuration) plus the swap size plus a fudge factor of
        // 0.6GiB (this was 2.1GiB up to Calamares 3.2.2).
        qint64 requiredSpaceB = o.requiredSpaceB + 600_MiB + suggestedSwapSizeB;

        // If there is enough room for ESP + root + home + swap, create swap, otherwise don't.
        shouldCreateSwap = availableSpaceB > requiredSpaceB;
    }

    if ( shouldCreateSwap )
    {
        lastSectorForRoot -= suggestedSwapSizeB / dev->logicalSize() + 1;
    }

    qint64 lastSectorOfLayout  = core->layoutApply( dev, firstFreeSector, lastSectorForRoot, o.luksPassphrase );
    if ( lastSectorOfLayout < lastSectorForRoot )
    {
    
    }

    if ( shouldCreateHome )
    {
        Partition* homePartition = KPMHelpers::createNewPartition( dev->partitionTable(),
                                                                  *dev,
                                                                  PartitionRole( PartitionRole::Primary ),
                                                                  "ext4",
                                                                  lastSectorForRoot + 1,
                                                                  dev->totalLogical() - 1,
                                                                  KPM_PARTITION_FLAG( None ) );
        PartitionInfo::setFormat( efiPartition, true );
        PartitionInfo::setMountPoint( efiPartition, o.efiPartitionMountPoint );
        name = gs->contains( "homePartitionName" ).toString();
        if ( !name.isEmpty() )
        {
            homePartition->setLabel( gs->value( "homePartitionName" ).toString() );
        }
#if defined( WITH_KPMCORE42API )
        homePartition->setType( "933ac7e1-2eb4-4f13-b844-0e14e2aef915" );
#endif
        core->createPartition( dev, homePartition );
        firstFreeSector = lastSector + 1;
    }

    if ( shouldCreateSwap )
    {
        core->layoutAddEntry( gs->contains( "swapPartitionName" ) ? gs->value( "swapPartitionName" ).toString() : QString( "efi" ),
                              QString( "" ),
                              QString( "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f" ),
                              -1,
                              gs->contains( "swapPartition" ) ? gs->value( "swapPartition" ).toString() : QString( "/boot/efi" ),
                              QString( "linuxswap" ),
                              { },
                              gs->contains( "swapPartitionSize" ) ? gs->value( "swapPartitionSize" ).toString() : QString( "300MiB" ),
                              QString( "0" ),
                              QString( "0" ) );
    }

    core->layoutApply( dev, firstFreeSector, lastSectorForRoot, o.luksPassphrase );

    core->dumpQueue();
}


void
doReplacePartition( PartitionCoreModule* core, Device* dev, Partition* partition, Choices::ReplacePartitionOptions o )
{
    qint64 firstSector, lastSector;

    cDebug() << "doReplacePartition for device" << partition->partitionPath();

    PartitionRole newRoles( partition->roles() );
    if ( partition->roles().has( PartitionRole::Extended ) )
    {
        newRoles = PartitionRole( PartitionRole::Primary );
    }

    if ( partition->roles().has( PartitionRole::Unallocated ) )
    {
        newRoles = PartitionRole( PartitionRole::Primary );
        cWarning() << "selected partition is free space";
        if ( partition->parent() )
        {
            Partition* parent = dynamic_cast< Partition* >( partition->parent() );
            if ( parent && parent->roles().has( PartitionRole::Extended ) )
            {
                newRoles = PartitionRole( PartitionRole::Logical );
            }
        }
    }

    // Save the first and last sector values as the partition will be deleted
    firstSector = partition->firstSector();
    lastSector = partition->lastSector();
    if ( !partition->roles().has( PartitionRole::Unallocated ) )
    {
        core->deletePartition( dev, partition );
    }

    core->layoutApply( dev, firstSector, lastSector, o.luksPassphrase );

    core->dumpQueue();
}

}  // namespace PartitionActions
