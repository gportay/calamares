/* === This file is part of Calamares - <https://github.com/calamares> ===
 *
 *   Copyright (c) 2017, Kyle Robbertze <kyle@aims.ac.za>
 *   Copyright 2017, 2020, Adriaan de Groot <groot@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PACKAGETREEITEM_H
#define PACKAGETREEITEM_H

#include <QList>
#include <QStandardItem>
#include <QVariant>

class PackageTreeItem : public QStandardItem
{
public:
    using List = QList< PackageTreeItem* >;

    // explicit PackageTreeItem( const ItemData& data, PackageTreeItem* parent = nullptr );
    explicit PackageTreeItem( const QString& packageName, PackageTreeItem* parent = nullptr );
    explicit PackageTreeItem( PackageTreeItem* parent );
    explicit PackageTreeItem();  // The root of the tree; always selected, named <root>
    ~PackageTreeItem() override;

    void appendChild( PackageTreeItem* child );
    PackageTreeItem* child( int row );
    int childCount() const;
    QVariant data( int column ) const override;
    int row() const;

    PackageTreeItem* parentItem() { return m_parentItem; }
    const PackageTreeItem* parentItem() const { return m_parentItem; }

    QString prettyName() const { return m_name; }  // Not sure why pretty
    QString description() const { return m_description; }
    QString packageName() const { return m_packageName; }

    bool hasScript() const { return !m_preScript.isEmpty() || !m_postScript.isEmpty(); }
    QString preScript() const { return m_preScript; }
    QString postScript() const { return m_postScript; }

    bool isHidden() const { return m_isHidden; }
    void setHidden( bool isHidden );  // TODO: remove this

    /**
     * @brief Is this hidden item, considered "selected"?
     *
     * This asserts when called on a non-hidden item.
     * A hidden item has its own selected state, but really
     * falls under the selectedness of the parent item.
     */
    bool hiddenSelected() const;

    bool isCritical() const { return m_isCritical; }
    void setCritical( bool isCritical );  // TODO: remove this

    Qt::CheckState isSelected() const { return m_selected; }
    void setSelected( Qt::CheckState isSelected );
    void setChildrenSelected( Qt::CheckState isSelected );
    int type() const override;

    /** @brief Turns this item into a variant for PackageOperations use
     *
     * For "plain" items, this is just the package name; items with
     * scripts return a map. See the package module for how it's interpreted.
     */
    QVariant toOperation() const;

private:
    PackageTreeItem* m_parentItem = nullptr;
    List m_childItems;

    QString m_name;
    QString m_description;
    QString m_packageName;
    QString m_preScript;
    QString m_postScript;
    bool m_isCritical = false;
    bool m_isHidden = false;
    Qt::CheckState m_selected = Qt::Unchecked;
};

#endif  // PACKAGETREEITEM_H
