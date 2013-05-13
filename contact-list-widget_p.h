/*
 *  Contact List Widget Private class
 *  Copyright (C) 2011  Martin Klapetek <martin.klapetek@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef CONTACT_LIST_WIDGET_P_H
#define CONTACT_LIST_WIDGET_P_H

#include <TelepathyQt/Types>
#include <KTp/Models/contacts-model.h>

class KPixmapSequenceWidget;
class KTpTranslationProxy;
class PersonsModel;
class ContextMenu;
class ContactDelegate;
class ContactDelegateCompact;

namespace KTp {
    class ContactsFilterModel;
    class GroupsTreeProxyModel;
}

class ContactListWidgetPrivate {
public:
    ContactListWidgetPrivate()
    : model(0),
      compactDelegate(0),
      shouldDrag(false),
      showOffline(false) {}

    PersonsModel           *model;
    ContactDelegate        *delegate;
    ContactDelegateCompact *compactDelegate;
    KTpTranslationProxy    *translationProxy;
    QRect                   dropIndicatorRect;
    QPoint                  dragStartPosition;
    QString                 dragSourceGroup;
    bool                    shouldDrag;
    bool                    showOffline;
    QHash<QString, bool>    groupStates;
    KTp::ContactsFilterModel *modelFilter;
    KTp::GroupsTreeProxyModel *groupsProxy;
    ContextMenu              *contextMenu;
    KPixmapSequenceWidget    *busyWidget;

    ContactListWidget::SelectedItemType listSelection;
};

#endif //CONTACT_LIST_WIDGET_P_H
