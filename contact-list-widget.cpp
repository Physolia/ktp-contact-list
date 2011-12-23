/*
 *  Contact List Widget
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


#include "contact-list-widget.h"
#include "contact-list-widget_p.h"

#include <TelepathyQt/AccountManager>
#include <TelepathyQt/PendingChannelRequest>
#include <TelepathyQt/PendingReady>

#include <KTp/Models/accounts-model.h>
#include <KTp/Models/groups-model.h>
#include <KTp/Models/accounts-filter-model.h>
#include <KTp/Models/contact-model-item.h>
#include <KTp/Models/accounts-model-item.h>
#include <KTp/Models/groups-model-item.h>

#include <KGlobal>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KDebug>
#include <KMessageBox>
#include <KLocalizedString>
#include <KDialog>
#include <KFileDialog>
#include <KSettings/Dialog>

#include <QHeaderView>
#include <QLabel>

#include "contact-delegate.h"
#include "contact-delegate-compact.h"
#include "contact-overlays.h"

#define PREFERRED_TEXTCHAT_HANDLER "org.freedesktop.Telepathy.Client.KDE.TextUi"
#define PREFERRED_FILETRANSFER_HANDLER "org.freedesktop.Telepathy.Client.KDE.FileTransfer"
#define PREFERRED_AUDIO_VIDEO_HANDLER "org.freedesktop.Telepathy.Client.KDE.CallUi"
#define PREFERRED_RFB_HANDLER "org.freedesktop.Telepathy.Client.krfb_rfb_handler"

ContactListWidget::ContactListWidget(QWidget *parent)
    : QTreeView(parent),
      d_ptr(new ContactListWidgetPrivate)
{
    Q_D(ContactListWidget);

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");

    Tp::AccountFactoryPtr  accountFactory = Tp::AccountFactory::create(QDBusConnection::sessionBus(),
                                                                       Tp::Features() << Tp::Account::FeatureCore
                                                                       << Tp::Account::FeatureAvatar
                                                                       << Tp::Account::FeatureCapabilities
                                                                       << Tp::Account::FeatureProtocolInfo
                                                                       << Tp::Account::FeatureProfile);

    Tp::ConnectionFactoryPtr connectionFactory = Tp::ConnectionFactory::create(QDBusConnection::sessionBus(),
                                                                               Tp::Features() << Tp::Connection::FeatureCore
                                                                               << Tp::Connection::FeatureRosterGroups
                                                                               << Tp::Connection::FeatureRoster
                                                                               << Tp::Connection::FeatureSelfContact);

    Tp::ContactFactoryPtr contactFactory = Tp::ContactFactory::create(Tp::Features()  << Tp::Contact::FeatureAlias
                                                                      << Tp::Contact::FeatureAvatarData
                                                                      << Tp::Contact::FeatureSimplePresence
                                                                      << Tp::Contact::FeatureCapabilities);

    Tp::ChannelFactoryPtr channelFactory = Tp::ChannelFactory::create(QDBusConnection::sessionBus());

    d->accountManager = Tp::AccountManager::create(QDBusConnection::sessionBus(),
                                                  accountFactory,
                                                  connectionFactory,
                                                  channelFactory,
                                                  contactFactory);

    connect(d->accountManager->becomeReady(), SIGNAL(finished(Tp::PendingOperation*)),
            this, SLOT(onAccountManagerReady(Tp::PendingOperation*)));

    connect(d->accountManager.data(), SIGNAL(newAccount(Tp::AccountPtr)),
            this, SLOT(onNewAccountAdded(Tp::AccountPtr)));

    d->delegate = new ContactDelegate(this);
    d->compactDelegate = new ContactDelegateCompact(this);

    header()->hide();
    setRootIsDecorated(false);
    setSortingEnabled(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setIndentation(0);
    setMouseTracking(true);
    setExpandsOnDoubleClick(false); //the expanding/collapsing is handled manually
    setDragEnabled(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);

    if (guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("compact")) {
        setItemDelegate(d->compactDelegate);
    } else {
        setItemDelegate(d->delegate);
    }

    addOverlayButtons();
    emit enableOverlays(guiConfigGroup.readEntry("selected_delegate", "full") == QLatin1String("full"));

    connect(this, SIGNAL(clicked(QModelIndex)),
            this, SLOT(onContactListClicked(QModelIndex)));

    connect(this, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(onContactListDoubleClicked(QModelIndex)));

    connect(d->delegate, SIGNAL(repaintItem(QModelIndex)),
            this->viewport(), SLOT(repaint())); //update(QModelIndex)
}


ContactListWidget::~ContactListWidget()
{

}

void ContactListWidget::onAccountManagerReady(Tp::PendingOperation* op)
{
    Q_D(ContactListWidget);

    if (op->isError()) {
        kDebug() << op->errorName();
        kDebug() << op->errorMessage();

        KMessageBox::error(this,
                           i18n("Something unexpected happened to the core part of your Instant Messaging system "
                           "and it couldn't be initialized. Try restarting the Contact List."),
                           i18n("IM system failed to initialize"));

                           return;
    }

    d->model = new AccountsModel(d->accountManager, this);
    d->groupsModel = new GroupsModel(d->model, this);
    d->modelFilter = new AccountsFilterModel(this);
    d->modelFilter->setDynamicSortFilter(true);
    d->modelFilter->clearFilterString();
    d->modelFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    d->modelFilter->setSortRole(Qt::DisplayRole);

    setModel(d->modelFilter);
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);

    connect(d->modelFilter, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(onNewGroupModelItemsInserted(QModelIndex,int,int)));

    connect(d->groupsModel, SIGNAL(operationFinished(Tp::PendingOperation*)),
            this, SIGNAL(genericOperationFinished(Tp::PendingOperation*)));

    QList<Tp::AccountPtr> accounts = d->accountManager->allAccounts();

    if(accounts.count() == 0) {
        if (KMessageBox::questionYesNo(this,
                                       i18n("You have no IM accounts configured. Would you like to do that now?"),
                                       i18n("No Accounts Found")) == KMessageBox::Yes) {

            showSettingsKCM();
        }
    }

    foreach (const Tp::AccountPtr &account, accounts) {
        onNewAccountAdded(account);
    }

    expandAll();

    emit accountManagerReady(op);
}

const Tp::AccountManagerPtr ContactListWidget::accountManager() const
{
    Q_D(const ContactListWidget);

    return d->accountManager;
}

AccountsModel* ContactListWidget::accountsModel()
{
    Q_D(ContactListWidget);

    return d->model;
}

void ContactListWidget::showSettingsKCM()
{
    KSettings::Dialog *dialog = new KSettings::Dialog(this);

    KService::Ptr tpAccKcm = KService::serviceByDesktopName("kcm_ktp_accounts");

    if (!tpAccKcm) {
        KMessageBox::error(this,
                           i18n("It appears you do not have the IM Accounts control module installed. Please install telepathy-accounts-kcm package."),
                           i18n("IM Accounts KCM Plugin Is Not Installed"));
    }

    dialog->addModule("kcm_ktp_accounts");

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

void ContactListWidget::onAccountConnectionStatusChanged(Tp::ConnectionStatus status)
{
    Q_D(ContactListWidget);

    kDebug() << "Connection status is" << status;

    Tp::AccountPtr account(qobject_cast< Tp::Account* >(sender()));
    QModelIndex index = d->model->index(qobject_cast<AccountsModelItem*>(d->model->accountItemForId(account->uniqueIdentifier())));

    switch (status) {
        case Tp::ConnectionStatusConnected:
            setExpanded(index, true);
            break;
        case Tp::ConnectionStatusConnecting:
            setExpanded(index, false);
        default:
            break;
    }
}

void ContactListWidget::onNewAccountAdded(const Tp::AccountPtr& account)
{
    Q_ASSERT(account->isReady(Tp::Account::FeatureCore));

    connect(account.data(),
            SIGNAL(connectionStatusChanged(Tp::ConnectionStatus)),
            this, SLOT(onAccountConnectionStatusChanged(Tp::ConnectionStatus)));

    //FIXME get rid of that thing already
//     m_avatarButton->loadAvatar(account);
//     KSharedConfigPtr config = KGlobal::config();
//     KConfigGroup avatarGroup(config, "Avatar");
//     if (avatarGroup.readEntry("method", QString()) == QLatin1String("account")) {
//         //this also updates the avatar if it was changed somewhere else
//         m_avatarButton->selectAvatarFromAccount(avatarGroup.readEntry("source", QString()));
//     }
}

void ContactListWidget::onContactListClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    if (index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<AccountsModelItem*>()
        || index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<GroupsModelItem*>()) {

        KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktelepathyrc"));
        KConfigGroup groupsConfig = config->group("GroupsState");

        if (isExpanded(index)) {
            collapse(index);
            groupsConfig.writeEntry(index.data(AccountsModel::IdRole).toString(), false);
        } else {
            expand(index);
            groupsConfig.writeEntry(index.data(AccountsModel::IdRole).toString(), true);
        }

        groupsConfig.config()->sync();
    }
}

void ContactListWidget::onContactListDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    if (index.data(AccountsModel::ItemRole).userType() == qMetaTypeId<ContactModelItem*>()) {
        kDebug() << "Text chat requested for index" << index;
        startTextChannel(index.data(AccountsModel::ItemRole).value<ContactModelItem*>());
    }
}

void ContactListWidget::addOverlayButtons()
{
    Q_D(ContactListWidget);

    TextChannelContactOverlay*  textOverlay  = new TextChannelContactOverlay(this);
    AudioChannelContactOverlay* audioOverlay = new AudioChannelContactOverlay(this);
    VideoChannelContactOverlay* videoOverlay = new VideoChannelContactOverlay(this);

    FileTransferContactOverlay* fileOverlay  = new FileTransferContactOverlay(this);
    DesktopSharingContactOverlay *desktopOverlay = new DesktopSharingContactOverlay(this);

    d->delegate->installOverlay(textOverlay);
    d->delegate->installOverlay(audioOverlay);
    d->delegate->installOverlay(videoOverlay);
    d->delegate->installOverlay(fileOverlay);
    d->delegate->installOverlay(desktopOverlay);

    textOverlay->setView(this);
    textOverlay->setActive(true);

    audioOverlay->setView(this);
    audioOverlay->setActive(true);

    videoOverlay->setView(this);
    videoOverlay->setActive(true);

    fileOverlay->setView(this);
    fileOverlay->setActive(true);

    desktopOverlay->setView(this);
    desktopOverlay->setActive(true);

    connect(textOverlay, SIGNAL(overlayActivated(QModelIndex)),
            d->delegate, SLOT(hideStatusMessageSlot(QModelIndex)));

    connect(textOverlay, SIGNAL(overlayHidden()),
            d->delegate, SLOT(reshowStatusMessageSlot()));


    connect(textOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startTextChannel(ContactModelItem*)));

    connect(fileOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startFileTransferChannel(ContactModelItem*)));

    connect(audioOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startAudioChannel(ContactModelItem*)));

    connect(videoOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startVideoChannel(ContactModelItem*)));

    connect(desktopOverlay, SIGNAL(activated(ContactModelItem*)),
            this, SLOT(startDesktopSharing(ContactModelItem*)));


    connect(this, SIGNAL(enableOverlays(bool)),
            textOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            audioOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            videoOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            fileOverlay, SLOT(setActive(bool)));

    connect(this, SIGNAL(enableOverlays(bool)),
            desktopOverlay, SLOT(setActive(bool)));
}

void ContactListWidget::toggleGroups(bool show)
{
    Q_D(ContactListWidget);

    if (show) {
        d->modelFilter->setSourceModel(d->groupsModel);
    } else {
        d->modelFilter->setSourceModel(d->model);
    }
}

void ContactListWidget::toggleOfflineContacts(bool show)
{
    Q_D(ContactListWidget);

    d->modelFilter->setShowOfflineUsers(show);
}

void ContactListWidget::toggleSortByPresence(bool sort)
{
    Q_D(ContactListWidget);

    d->modelFilter->setSortByPresence(sort);
}

void ContactListWidget::startTextChannel(ContactModelItem *contactItem)
{
    Q_D(ContactListWidget);

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting chat for contact" << contact->alias();

    Tp::AccountPtr account = d->model->accountForContactItem(contactItem);

    Tp::ChannelRequestHints hints;
    hints.setHint("org.kde.telepathy","forceRaiseWindow", QVariant(true));

    Tp::PendingChannelRequest *channelRequest = account->ensureTextChat(contact,
                                                                        QDateTime::currentDateTime(),
                                                                        PREFERRED_TEXTCHAT_HANDLER,
                                                                        hints);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            SIGNAL(genericOperationFinished(Tp::PendingOperation*)));
}

void ContactListWidget::startAudioChannel(ContactModelItem *contactItem)
{
    Q_D(ContactListWidget);

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting audio for contact" << contact->alias();

    Tp::AccountPtr account = d->model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest *channelRequest = account->ensureStreamedMediaAudioCall(contact,
                                                                                      QDateTime::currentDateTime(),
                                                                                      PREFERRED_AUDIO_VIDEO_HANDLER);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            SIGNAL(genericOperationFinished(Tp::PendingOperation*)));
}

void ContactListWidget::startVideoChannel(ContactModelItem *contactItem)
{
    Q_D(ContactListWidget);

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting video for contact" << contact->alias();

    Tp::AccountPtr account = d->model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest* channelRequest = account->ensureStreamedMediaVideoCall(contact, true,
                                                                                      QDateTime::currentDateTime(),
                                                                                      PREFERRED_AUDIO_VIDEO_HANDLER);
    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            SIGNAL(genericOperationFinished(Tp::PendingOperation*)));
}

void ContactListWidget::startDesktopSharing(ContactModelItem* contactItem)
{
    Q_D(ContactListWidget);

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting desktop sharing for contact" << contact->alias();

    Tp::AccountPtr account = d->model->accountForContactItem(contactItem);

    Tp::PendingChannelRequest* channelRequest = account->createStreamTube(contact,
                                                                          QLatin1String("rfb"),
                                                                          QDateTime::currentDateTime(),
                                                                          PREFERRED_RFB_HANDLER);

    connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
            SIGNAL(genericOperationFinished(Tp::PendingOperation*)));
}

void ContactListWidget::startFileTransferChannel(ContactModelItem *contactItem)
{
    Q_D(ContactListWidget);

    Q_ASSERT(contactItem);
    Tp::ContactPtr contact = contactItem->contact();

    kDebug() << "Requesting file transfer for contact" << contact->alias();

    Tp::AccountPtr account = d->model->accountForContactItem(contactItem);

    QStringList filenames = KFileDialog::getOpenFileNames(KUrl("kfiledialog:///FileTransferLastDirectory"),
                                                          QString(),
                                                          this,
                                                          i18n("Choose files to send to %1", contact->alias()));

    if (filenames.isEmpty()) { // User hit cancel button
        return;
    }

    QDateTime now = QDateTime::currentDateTime();
    Q_FOREACH (const QString &filename, filenames) {
        QFileInfo fileinfo(filename);

        kDebug() << "Filename:" << filename;
        kDebug() << "Content type:" << KMimeType::findByFileContent(filename)->name();

        Tp::FileTransferChannelCreationProperties fileTransferProperties(filename,
                                                                        KMimeType::findByFileContent(filename)->name());

        Tp::PendingChannelRequest* channelRequest = account->createFileTransfer(contact,
                                                                                fileTransferProperties,
                                                                                now,
                                                                                PREFERRED_FILETRANSFER_HANDLER);

        connect(channelRequest, SIGNAL(finished(Tp::PendingOperation*)),
                SIGNAL(genericOperationFinished(Tp::PendingOperation*)));
    }
}

void ContactListWidget::onNewGroupModelItemsInserted(const QModelIndex& index, int start, int end)
{
    Q_UNUSED(start);
    Q_UNUSED(end);
    if (!index.isValid()) {
        return;
    }

    //if there is no parent, we deal with top-level item that we want to expand/collapse, ie. group or account
    if (!index.parent().isValid()) {
        KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktelepathyrc"));
        KConfigGroup groupsConfig = config->group("GroupsState");

        //we're probably dealing with group item, so let's check if it is expanded first
        if (!isExpanded(index)) {
            //if it's not expanded, check the config if we should expand it or not
            if (groupsConfig.readEntry(index.data(AccountsModel::IdRole).toString(), false)) {
                expand(index);
            }
        }
    }
}

void ContactListWidget::onSwitchToFullView()
{
    Q_D(ContactListWidget);

    setItemDelegate(d->delegate);
    doItemsLayout();

    emit enableOverlays(true);

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");
    guiConfigGroup.writeEntry("selected_delegate", "full");
    guiConfigGroup.config()->sync();
}

void ContactListWidget::onSwitchToCompactView()
{
    Q_D(ContactListWidget);

    setItemDelegate(d->compactDelegate);
    doItemsLayout();

    emit enableOverlays(false);

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup guiConfigGroup(config, "GUI");
    guiConfigGroup.writeEntry("selected_delegate", "compact");
    guiConfigGroup.config()->sync();
}

void ContactListWidget::setFilterString(const QString& string)
{
    Q_D(ContactListWidget);

    d->modelFilter->setShowOfflineUsers(!string.isEmpty());
    d->modelFilter->setFilterString(string);
}
