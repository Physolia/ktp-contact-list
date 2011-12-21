/*
 * This file is part of telepathy-contactslist-prototype
 *
 * Copyright (C) 2009-2010 Collabora Ltd. <info@collabora.co.uk>
 *   @Author George Goldberg <george.goldberg@collabora.co.uk>
 * Copyright (C) 2011 Martin Klapetek <martin.klapetek@gmail.com>
 * Copyright (C) 2011 Keith Rusler <xzekecomax@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "main-widget.h"

#include <QtGui/QSortFilterProxyModel>
#include <QtGui/QPainter>
#include <QtGui/QMenu>
#include <QtGui/QToolButton>
#include <QtCore/QWeakPointer>
#include <QWidgetAction>
#include <QCloseEvent>

#include <TelepathyQt/PendingChannelRequest>
#include <TelepathyQt/PendingContacts>
#include <TelepathyQt/ContactManager>

#include <KTelepathy/Models/accounts-model.h>
#include <KTelepathy/Models/contact-model-item.h>
#include <KTelepathy/Models/groups-model-item.h>

#include <KDebug>
#include <KDialog>
#include <KIO/Job>
#include <KUser>
#include <KMenu>
#include <KMessageBox>
#include <KProtocolInfo>
#include <KSettings/Dialog>
#include <KSharedConfig>
#include <KStandardShortcut>
#include <KNotification>

#include "ui_main-widget.h"
#include "account-buttons-panel.h"
#include "fetch-avatar-job.h"
#include "contact-list-application.h"
#include "dialogs/add-contact-dialog.h"
#include "dialogs/join-chat-room-dialog.h"
#include "tooltips/tooltipmanager.h"
#include "context-menu.h"

#define PREFERRED_TEXTCHAT_HANDLER "org.freedesktop.Telepathy.Client.KDE.TextUi"

bool kde_tp_filter_contacts_by_publication_status(const Tp::ContactPtr &contact)
{
    return contact->publishState() == Tp::Contact::PresenceStateAsk;
}

MainWidget::MainWidget(QWidget *parent)
    : KMainWindow(parent)
{
    Tp::registerTypes();
    KUser user;

    setupUi(this);
    m_filterBar->hide();
    setWindowIcon(KIcon("telepathy-kde"));
    setAutoSaveSettings();

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");

    m_userAccountNameLabel->setText(user.property(KUser::FullName).isNull() ?
        user.loginName() : user.property(KUser::FullName).toString()
    );

    m_avatarButton->setPopupMode(QToolButton::InstantPopup);

    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_addContactAction = new KAction(KIcon("list-add-user"), i18n("Add new contacts..."), this);

    m_toolBar->addAction(m_addContactAction);

    m_groupContactsAction = new KAction(KIcon("user-group-properties"), i18n("Show/Hide groups"), this);
    m_groupContactsAction->setCheckable(true);
    m_groupContactsAction->setChecked(true);
    //TODO: Toggle the tooltip with the button? eg. once its Show, after click its Hide .. ?

    m_toolBar->addAction(m_groupContactsAction);

    m_showOfflineAction = new KAction(KIcon("meeting-attending-tentative"), i18n("Hide/Show offline users"), this);
    m_showOfflineAction->setCheckable(true);
    m_showOfflineAction->setChecked(false);

    m_toolBar->addAction(m_showOfflineAction);

    m_sortByPresenceAction = new KDualAction(i18n("Sort by presence"), i18n("Sort by name"), this);
    m_sortByPresenceAction->setActiveIcon(KIcon("user-online"));
    m_sortByPresenceAction->setInactiveIcon(KIcon("view-sort-ascending"));

    m_toolBar->addAction(m_sortByPresenceAction);

    m_searchContactAction = new KAction(KIcon("edit-find-user"), i18n("Find contact"), this );
    m_searchContactAction->setShortcut(KStandardShortcut::find());
    m_searchContactAction->setCheckable(true);
    m_searchContactAction->setChecked(false);

    m_toolBar->addAction(m_searchContactAction);

    QWidget *toolBarSpacer = new QWidget(this);
    toolBarSpacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    m_toolBar->addWidget(toolBarSpacer);

    QToolButton *settingsButton = new QToolButton(this);
    settingsButton->setIcon(KIcon("configure"));
    settingsButton->setPopupMode(QToolButton::InstantPopup);

    KMenu *settingsButtonMenu = new KMenu(settingsButton);
    settingsButtonMenu->addAction(i18n("Configure accounts..."), m_contactsListView, SLOT(showSettingsKCM()));

    QActionGroup *delegateTypeGroup = new QActionGroup(this);
    delegateTypeGroup->setExclusive(true);

    KMenu *setDelegateTypeMenu = new KMenu(settingsButtonMenu);
    setDelegateTypeMenu->setTitle(i18n("Contact list type"));
    delegateTypeGroup->addAction(setDelegateTypeMenu->addAction(i18n("Use full list"),
                                                                m_contactsListView, SLOT(onSwitchToFullView())));
    delegateTypeGroup->actions().last()->setCheckable(true);

    if (guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("full")) {
        delegateTypeGroup->actions().last()->setChecked(true);
    }

    delegateTypeGroup->addAction(setDelegateTypeMenu->addAction(i18n("Use compact list"),
                                                                m_contactsListView, SLOT(onSwitchToCompactView())));
    delegateTypeGroup->actions().last()->setCheckable(true);

    if (guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("compact")) {
        delegateTypeGroup->actions().last()->setChecked(true);
    }

    settingsButtonMenu->addMenu(setDelegateTypeMenu);

    if (guiConfigGroup.readEntry("selected_presence_chooser", "global") == QLatin1String("global")) {
//         //hide account buttons and show global presence
         onUseGlobalPresenceTriggered();
    }

    settingsButtonMenu->addAction(i18n("Join chat room"), this, SLOT(onJoinChatRoomRequested()));
    settingsButtonMenu->addSeparator();
    settingsButtonMenu->addMenu(helpMenu());

    settingsButton->setMenu(settingsButtonMenu);

    m_toolBar->addWidget(settingsButton);

    m_contextMenu = new ContextMenu(m_contactsListView);

    new ToolTipManager(m_contactsListView);

    connect(m_contactsListView, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(onCustomContextMenuRequested(QPoint)));

    connect(m_addContactAction, SIGNAL(triggered(bool)),
            this, SLOT(onAddContactRequest()));

    connect(m_groupContactsAction, SIGNAL(triggered(bool)),
            m_contactsListView, SLOT(toggleGroups(bool)));

    connect(m_searchContactAction, SIGNAL(triggered(bool)),
            this, SLOT(toggleSearchWidget(bool)));

    connect(m_avatarButton, SIGNAL(operationFinished(Tp::PendingOperation*)),
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));

    connect(m_contactsListView, SIGNAL(accountManagerReady(Tp::PendingOperation*)),
            this, SLOT(onAccountManagerReady(Tp::PendingOperation*)));

    if (guiConfigGroup.readEntry("pin_filterbar", true)) {
        toggleSearchWidget(true);
        m_searchContactAction->setChecked(true);
    }
}

MainWidget::~MainWidget()
{
    //save the state of the filter bar, pinned or not
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup configGroup(config, "GUI");
    configGroup.writeEntry("pin_filterbar", m_searchContactAction->isChecked());
    configGroup.writeEntry("use_groups", m_groupContactsAction->isChecked());
    configGroup.writeEntry("show_offline", m_showOfflineAction->isChecked());
    configGroup.writeEntry("sort_by_presence", m_sortByPresenceAction->isActive());
    configGroup.config()->sync();
}

void MainWidget::onAccountManagerReady(Tp::PendingOperation* op)
{
    Q_UNUSED(op)

    connect(m_showOfflineAction, SIGNAL(toggled(bool)),
            m_contactsListView, SLOT(toggleOfflineContacts(bool)));

    connect(m_filterBar, SIGNAL(filterChanged(QString)),
            m_contactsListView, SLOT(setFilterString(QString)));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_filterBar, SLOT(hide()));

    connect(m_filterBar, SIGNAL(closeRequest()),
            m_searchContactAction, SLOT(trigger()));

    connect(m_sortByPresenceAction, SIGNAL(activeChanged(bool)),
            m_contactsListView, SLOT(toggleSortByPresence(bool)));

    connect(m_contactsListView, SIGNAL(genericOperationFinished(Tp::PendingOperation*)),
            this, SLOT(onGenericOperationFinished(Tp::PendingOperation*)));

    m_avatarButton->initialize(m_contactsListView->accountsModel(), m_contactsListView->accountManager());
    m_accountButtons->setAccountManager(m_contactsListView->accountManager());
    m_presenceChooser->setAccountManager(m_contactsListView->accountManager());

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");

    bool useGroups = guiConfigGroup.readEntry("use_groups", true);
    m_groupContactsAction->setChecked(useGroups);

    bool showOffline = guiConfigGroup.readEntry("show_offline", false);
    m_showOfflineAction->setChecked(showOffline);

    bool sortByPresence = guiConfigGroup.readEntry("sort_by_presence", true);
    m_sortByPresenceAction->setActive(sortByPresence);

    m_contactsListView->toggleGroups(useGroups);
    m_contactsListView->toggleOfflineContacts(showOffline);
    m_contactsListView->toggleSortByPresence(sortByPresence);
}

void MainWidget::showMessageToUser(const QString& text, const MainWidget::SystemMessageType type)
{
    //The pointer is automatically deleted when the event is closed
    KNotification *notification;
    if (type == MainWidget::SystemMessageError) {
        notification = new KNotification("telepathyError", this);
    } else {
        notification = new KNotification("telepathyInfo", this);
    }

    KAboutData aboutData("ktelepathy",0,KLocalizedString(),0);
    notification->setComponentData(KComponentData(aboutData));

    notification->setText(text);
    notification->sendEvent();
}

void MainWidget::onAddContactRequest() {
    QWeakPointer<AddContactDialog> dialog = new AddContactDialog(m_contactsListView->accountsModel(), this);
    if (dialog.data()->exec() == QDialog::Accepted) {
        Tp::AccountPtr account = dialog.data()->account();
        if (account.isNull()) {
            KMessageBox::error(this,
                               i18n("Seems like you forgot to select an account. Also do not forget to connect it first."),
                               i18n("No Account Selected"));
        }
        else if (account->connection().isNull()) {
            KMessageBox::error(this,
                               i18n("An error we did not anticipate just happened and so the contact could not be added. Sorry."),
                               i18n("Account Error"));
        } else {
            QStringList identifiers = QStringList() << dialog.data()->screenName();
            Tp::PendingContacts* pendingContacts = account->connection()->contactManager()->contactsForIdentifiers(identifiers);
            connect(pendingContacts, SIGNAL(finished(Tp::PendingOperation*)), SLOT(onAddContactRequestFoundContacts(Tp::PendingOperation*)));
        }
    }
    delete dialog.data();
}

void MainWidget::onAddContactRequestFoundContacts(Tp::PendingOperation *operation) {
    Tp::PendingContacts *pendingContacts = qobject_cast<Tp::PendingContacts*>(operation);

    if (! pendingContacts->isError()) {
        //request subscription
        pendingContacts->manager()->requestPresenceSubscription(pendingContacts->contacts());
    }
    else {
        kDebug() << pendingContacts->errorName();
        kDebug() << pendingContacts->errorMessage();
    }
}

void MainWidget::onCustomContextMenuRequested(const QPoint &pos)
{
    QModelIndex index = m_contactsListView->indexAt(pos);

    if (!index.isValid()) {
        return;
    }

    Tp::ContactPtr contact;
    QVariant item = index.data(AccountsModel::ItemRole);

    KMenu *menu = 0;

    if (item.userType() == qMetaTypeId<ContactModelItem*>()) {
        menu = m_contextMenu->contactContextMenu(index);
    } else if (item.userType() == qMetaTypeId<GroupsModelItem*>()) {
        menu = m_contextMenu->groupContextMenu(index);
    }

    if (menu) {
        menu->exec(QCursor::pos());
        menu->deleteLater();
    }
}

void MainWidget::onGenericOperationFinished(Tp::PendingOperation* operation)
{
    if (operation->isError()) {
        QString errorMsg(operation->errorName() + ": " + operation->errorMessage());
        showMessageToUser(errorMsg, SystemMessageError);
    }
}

void MainWidget::onJoinChatRoomRequested()
{
    QWeakPointer<JoinChatRoomDialog> dialog = new JoinChatRoomDialog(m_contactsListView->accountManager());

    if (dialog.data()->exec() == QDialog::Accepted) {
        Tp::AccountPtr account = dialog.data()->selectedAccount();

        // check account validity. Should NEVER be invalid
        if (!account.isNull()) {
            // ensure chat room
            Tp::ChannelRequestHints hints;
            hints.setHint("org.kde.telepathy","forceRaiseWindow", QVariant(true));

            Tp::PendingChannelRequest *channelRequest = account->ensureTextChatroom(dialog.data()->selectedChatRoom(),
                                                                                    QDateTime::currentDateTime(),
                                                                                    PREFERRED_TEXTCHAT_HANDLER,
                                                                                    hints);

            connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)), SLOT(onGenericOperationFinished(Tp::PendingOperation*)));
        }
    }

    delete dialog.data();
}

void MainWidget::closeEvent(QCloseEvent* e)
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup generalConfigGroup(config, "General");
    KConfigGroup notifyConigGroup(config, "Notification Messages");

    ContactListApplication *app = qobject_cast<ContactListApplication*>(kapp);
    if (!app->isShuttingDown()) {
        //the standard KMessageBox control saves "true" if you select the checkbox, therefore the reversed var name
        bool dontCheckForPlasmoid = notifyConigGroup.readEntry("dont_check_for_plasmoid", false);

        if (isAnyAccountOnline() && !dontCheckForPlasmoid) {
            if (!isPresencePlasmoidPresent()) {
                switch (KMessageBox::warningYesNoCancel(this,
                        i18n("You do not have any other presence controls active (a Presence widget for example).\n"
                            "Do you want to stay online or would you rather go offline?"),
                        i18n("No Other Presence Controls Found"),
                        KGuiItem(i18n("Stay Online"), KIcon("user-online")),
                        KGuiItem(i18n("Go Offline"), KIcon("user-offline")),
                        KStandardGuiItem::cancel(),
                        QString("dont_check_for_plasmoid"))) {

                    case KMessageBox::No:
                        generalConfigGroup.writeEntry("go_offline_when_closing", true);
                        goOffline();
                        break;
                    case KMessageBox::Cancel:
                        e->ignore();
                        return;
                }
            }
        } else if (isAnyAccountOnline() && dontCheckForPlasmoid) {
            bool shouldGoOffline = generalConfigGroup.readEntry("go_offline_when_closing", false);
            if (shouldGoOffline) {
                goOffline();
            }
        }

        generalConfigGroup.config()->sync();
    }

    KMainWindow::closeEvent(e);
}

bool MainWidget::isPresencePlasmoidPresent() const
{
    QDBusInterface plasmoidOnDbus("org.kde.Telepathy.PresenceAppletActive", "/PresenceAppletActive");

    if (plasmoidOnDbus.isValid()) {
        return true;
    } else {
        return false;
    }
}

void MainWidget::goOffline()
{
    //FIXME use global presence
    kDebug() << "Setting all accounts offline...";
    foreach (const Tp::AccountPtr &account, m_contactsListView->accountManager()->allAccounts()) {
        if (account->isEnabled() && account->isValid()) {
            account->setRequestedPresence(Tp::Presence::offline());
        }
    }
}

bool MainWidget::isAnyAccountOnline() const
{
    foreach (const Tp::AccountPtr &account, m_contactsListView->accountManager()->allAccounts()) {
        if (account->isEnabled() && account->isValid() && account->isOnline()) {
            return true;
        }
    }

    return false;
}

void MainWidget::onUseGlobalPresenceTriggered()
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup configGroup(config, "GUI");

    m_presenceChooser->show();
    m_accountButtons->hide();

    configGroup.writeEntry("selected_presence_chooser", "global");

    configGroup.config()->sync();
}

void MainWidget::onUsePerAccountPresenceTriggered()
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup configGroup(config, "GUI");

    m_presenceChooser->hide();
    m_accountButtons->show();

    configGroup.writeEntry("selected_presence_chooser", "per-account");

    configGroup.config()->sync();
}

void MainWidget::toggleSearchWidget(bool show)
{
        if(show) {
            m_filterBar->show();
        } else {
            m_contactsListView->setFilterString(QString());
            m_filterBar->clear();
            m_filterBar->hide();
        }
}

#include "main-widget.moc"
