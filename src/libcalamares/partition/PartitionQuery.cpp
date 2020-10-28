/* === This file is part of Calamares - <https://calamares.io> ===
 *
 *   SPDX-FileCopyrightText: 2014 Aurélien Gâteau <agateau@kde.org>
 *   SPDX-FileCopyrightText: 2015-2016 Teo Mrnjavac <teo@kde.org>
 *   SPDX-FileCopyrightText: 2018-2019 Adriaan de Groot <groot@kde.org>
 *   SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   Calamares is Free Software: see the License-Identifier above.
 *
 *
 */

#include "PartitionQuery.h"

#include "PartitionIterator.h"

#include <kpmcore/core/device.h>
#include <kpmcore/core/partition.h>
#include <kpmcore/core/partitiontable.h>

namespace CalamaresUtils
{
namespace Partition
{

// Types from KPMCore
using ::Device;
using ::Partition;

static const QMap< const QString, const QString > gptTypes =
{
    { QString("C12A7328-F81F-11D2-BA4B-00A0C93EC93B"), QString("EFI System") },
    { QString("4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709"), QString("Linux root (x86-64)") },
    { QString("933AC7E1-2EB4-4F13-B844-0E14E2AEF915"), QString("Linux home") },
    { QString("BC13C2FF-59E6-4262-A352-B275FD6F7172"), QString("Linux extended boot") },
    { QString("3B8F8425-20E0-4F3B-907F-1A25A76F98E8"), QString("Linux server data") },
    { QString("4D21B016-B534-45C2-A9FB-5C16E091FD2D"), QString("Linux variable data") },
    { QString("7EC6F557-3BC5-4ACA-B293-16EF5DF639D1"), QString("Linux temporary data") },
}

const QString& getType( Partition* partition )
{
    if ( getPartitionTable( m_partition )->type() == PartitionTable::TableType::gpt )
    {
	 gptTypes.find( partitions->type );
    }

    return partition.type();
}

const PartitionTable*
getPartitionTable( const Partition* partition )
{
    const PartitionNode* root = partition;
    while ( root && !root->isRoot() )
    {
        root = root->parent();
    }

    return dynamic_cast< const PartitionTable* >( root );
}


bool
isPartitionFreeSpace( const Partition* partition )
{
    return partition->roles().has( PartitionRole::Unallocated );
}


bool
isPartitionNew( const Partition* partition )
{
#if defined( WITH_KPMCORE4API )
    constexpr auto NewState = Partition::State::New;
#else
    constexpr auto NewState = Partition::StateNew;
#endif
    return partition->state() == NewState;
}


Partition*
findPartitionByCurrentMountPoint( const QList< Device* >& devices, const QString& mountPoint )
{
    for ( auto device : devices )
        for ( auto it = PartitionIterator::begin( device ); it != PartitionIterator::end( device ); ++it )
            if ( ( *it )->mountPoint() == mountPoint )
            {
                return *it;
            }
    return nullptr;
}


Partition*
findPartitionByPath( const QList< Device* >& devices, const QString& path )
{
    if ( path.simplified().isEmpty() )
    {
        return nullptr;
    }

    for ( auto device : devices )
    {
        for ( auto it = PartitionIterator::begin( device ); it != PartitionIterator::end( device ); ++it )
        {
            if ( ( *it )->partitionPath() == path.simplified() )
            {
                return *it;
            }
        }
    }
    return nullptr;
}


QList< Partition* >
findPartitions( const QList< Device* >& devices, std::function< bool( Partition* ) > criterionFunction )
{
    QList< Partition* > results;
    for ( auto device : devices )
    {
        for ( auto it = PartitionIterator::begin( device ); it != PartitionIterator::end( device ); ++it )
        {
            if ( criterionFunction( *it ) )
            {
                results.append( *it );
            }
        }
    }
    return results;
}


}  // namespace Partition
}  // namespace CalamaresUtils
