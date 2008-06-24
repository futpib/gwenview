/*
Gwenview: an image viewer
Copyright 2007 Aurélien Gâteau <aurelien.gateau@free.fr>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#include "infocontextmanageritem.moc"

// Qt
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPair>
#include <QPushButton>
#include <QVBoxLayout>

// KDE
#include <kfileitem.h>
#include <klocale.h>
#include <ksqueezedtextlabel.h>
#include <kwordwrap.h>

// Local
#include "contextmanager.h"
#include "imagemetainfodialog.h"
#include "sidebar.h"
#include <lib/archiveutils.h>
#include <lib/gwenviewconfig.h>
#include <lib/preferredimagemetainfomodel.h>
#include <lib/imageviewpart.h>
#include <lib/document/document.h>
#include <lib/document/documentfactory.h>

namespace Gwenview {

#undef ENABLE_LOG
#undef LOG
//#define ENABLE_LOG
#ifdef ENABLE_LOG
#define LOG(x) kDebug() << x
#else
#define LOG(x) ;
#endif


/**
 * This widget is capable of showing multiple lines of key/value pairs.
 */
class KeyValueWidget : public QWidget {
public:
	KeyValueWidget(QWidget* parent)
	: QWidget(parent)
	, mLayout(new QFormLayout(this)) {
		mLayout->setLabelAlignment(Qt::AlignRight);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	}

	void addRow(const QString& key, const QString& value) {
		QString keyString = i18nc(
				"@item:intext %1 is a key, we append a colon to it. A value is displayed after",
				"%1: ", key);
		// FIXME: Call "trimmed()" to avoid breaking string freeze
		keyString = keyString.trimmed();

		QLabel* keyLabel = new QLabel;
		keyLabel->setText(keyString);

		KSqueezedTextLabel* valueLabel = new KSqueezedTextLabel;
		valueLabel->setTextElideMode(Qt::ElideRight);
		valueLabel->setText(value);
		valueLabel->setFocusPolicy(Qt::NoFocus);

		mLayout->addRow(keyLabel, valueLabel);
	}

	void clear() {
		Q_FOREACH(QWidget* child, findChildren<QWidget*>()) {
			child->deleteLater();
		}
	}

private:
	QFormLayout* mLayout;
};


struct InfoContextManagerItemPrivate {
	SideBar* mSideBar;
	SideBarGroup* mGroup;

	// One selection fields
	QWidget* mOneFileWidget;
	KeyValueWidget* mKeyValueWidget;
	KFileItem mFileItem;
	Document::Ptr mDocument;

	// Multiple selection fields
	QLabel* mMultipleFilesLabel;

	QPointer<ImageMetaInfoDialog> mImageMetaInfoDialog;

	void updateMetaInfoDialog() {
		if (!mImageMetaInfoDialog) {
			return;
		}
		ImageMetaInfoModel* model = mDocument ? mDocument->metaInfo() : 0;
		mImageMetaInfoDialog->setMetaInfo(model, GwenviewConfig::preferredMetaInfoKeyList());
	}
};


InfoContextManagerItem::InfoContextManagerItem(ContextManager* manager)
: AbstractContextManagerItem(manager)
, d(new InfoContextManagerItemPrivate) {
	d->mSideBar = 0;
	connect(contextManager(), SIGNAL(selectionChanged()),
		SLOT(updateSideBarContent()) );
}

InfoContextManagerItem::~InfoContextManagerItem() {
	delete d;
}


void InfoContextManagerItem::setSideBar(SideBar* sideBar) {
	d->mSideBar = sideBar;
	connect(sideBar, SIGNAL(aboutToShow()),
		SLOT(updateSideBarContent()) );
	d->mOneFileWidget = new QWidget();

	d->mKeyValueWidget = new KeyValueWidget(d->mOneFileWidget);

	QLabel* moreLabel = new QLabel(d->mOneFileWidget);
	moreLabel->setText(QString("<a href='#'>%1</a>").arg(i18nc("@action show more image meta info", "More...")));
	moreLabel->setAlignment(Qt::AlignRight);

	QVBoxLayout* layout = new QVBoxLayout(d->mOneFileWidget);
	layout->setMargin(2);
	layout->setSpacing(2);
	layout->addWidget(d->mKeyValueWidget);
	layout->addWidget(moreLabel);

	d->mMultipleFilesLabel = new QLabel();

	d->mGroup = sideBar->createGroup(i18nc("@title:group", "Information"));
	d->mGroup->addWidget(d->mOneFileWidget);
	d->mGroup->addWidget(d->mMultipleFilesLabel);

	d->mGroup->hide();

	connect(moreLabel, SIGNAL(linkActivated(const QString&)), SLOT(showMetaInfoDialog()) );
}


void InfoContextManagerItem::updateSideBarContent() {
	LOG("updateSideBarContent");
	if (!d->mSideBar->isVisible()) {
		LOG("updateSideBarContent: not visible, not updating");
		return;
	}
	LOG("updateSideBarContent: really updating");

	KFileItemList itemList = contextManager()->selection();
	if (itemList.count() == 0) {
		d->mGroup->hide();
		// "Garbage collect" document
		d->mDocument = 0;
		d->updateMetaInfoDialog();
		return;
	}

	d->mGroup->show();
	KFileItem item = itemList.first();
	if (itemList.count() == 1 && !ArchiveUtils::fileItemIsDirOrArchive(item)) {
		fillOneFileGroup(item);
	} else {
		fillMultipleItemsGroup(itemList);
	}
	d->updateMetaInfoDialog();
}

void InfoContextManagerItem::fillOneFileGroup(const KFileItem& item) {
	d->mFileItem = item;
	d->mOneFileWidget->show();
	d->mMultipleFilesLabel->hide();

	d->mDocument = DocumentFactory::instance()->load(item.url());
	connect(d->mDocument.data(), SIGNAL(metaDataUpdated()),
		SLOT(updateOneFileInfo()) );

	d->updateMetaInfoDialog();
	updateOneFileInfo();
}

void InfoContextManagerItem::fillMultipleItemsGroup(const KFileItemList& itemList) {
	// "Garbage collect" document
	d->mDocument = 0;

	int folderCount = 0, fileCount = 0;
	Q_FOREACH(const KFileItem& item, itemList) {
		if (item.isDir()) {
			folderCount++;
		} else {
			fileCount++;
		}
	}

	if (folderCount == 0) {
		d->mMultipleFilesLabel->setText(i18nc("@label", "%1 files selected", fileCount));
	} else if (fileCount == 0) {
		d->mMultipleFilesLabel->setText(i18ncp("@label", "One folder selected", "%1 folders selected", folderCount));
	} else {
		d->mMultipleFilesLabel->setText(i18nc("@label", "%1 folders and %2 files selected", folderCount, fileCount));
	}
	d->mOneFileWidget->hide();
	d->mMultipleFilesLabel->show();
}


void InfoContextManagerItem::updateOneFileInfo() {
	if (!d->mSideBar) {
		// Not initialized yet
		return;
	}

	if (!d->mDocument) {
		return;
	}

	QStringList list;
	d->mKeyValueWidget->clear();
	ImageMetaInfoModel* metaInfoModel = d->mDocument->metaInfo();
	Q_FOREACH(const QString& key, GwenviewConfig::preferredMetaInfoKeyList()) {
		QString label;
		QString value;
		metaInfoModel->getInfoForKey(key, &label, &value);

		if (!label.isEmpty() && !value.isEmpty()) {
			d->mKeyValueWidget->addRow(label, value);
		}
	}

	d->mKeyValueWidget->show();
}


void InfoContextManagerItem::showMetaInfoDialog() {
	if (!d->mImageMetaInfoDialog) {
		d->mImageMetaInfoDialog = new ImageMetaInfoDialog(d->mOneFileWidget);
		d->mImageMetaInfoDialog->setAttribute(Qt::WA_DeleteOnClose, true);
		connect(d->mImageMetaInfoDialog, SIGNAL(preferredMetaInfoKeyListChanged(const QStringList&)),
			SLOT(slotPreferredMetaInfoKeyListChanged(const QStringList&)) );
	}
	d->mImageMetaInfoDialog->setMetaInfo(d->mDocument->metaInfo(), GwenviewConfig::preferredMetaInfoKeyList());
	d->mImageMetaInfoDialog->show();
}


void InfoContextManagerItem::slotPreferredMetaInfoKeyListChanged(const QStringList& list) {
	GwenviewConfig::setPreferredMetaInfoKeyList(list);
	GwenviewConfig::self()->writeConfig();
	updateOneFileInfo();
}


} // namespace
