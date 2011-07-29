/*
Gwenview: an image viewer
Copyright 2007 Aurélien Gâteau <agateau@kde.org>

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
#include "thumbnailview.moc"

// Std
#include <math.h>

// Qt
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPainter>
#include <QPointer>
#include <QQueue>
#include <QScrollBar>
#include <QTimeLine>
#include <QTimer>
#include <QToolTip>

// KDE
#include <kdebug.h>
#include <kdirmodel.h>
#include <kicon.h>
#include <kiconloader.h>
#include <kglobalsettings.h>
#include <kpixmapsequence.h>

// Local
#include "abstractdocumentinfoprovider.h"
#include "abstractthumbnailviewhelper.h"
#include "archiveutils.h"
#include "mimetypeutils.h"
#include "thumbnailloadjob.h"

namespace Gwenview {

#undef ENABLE_LOG
#undef LOG
//#define ENABLE_LOG
#ifdef ENABLE_LOG
#define LOG(x) kDebug() << x
#else
#define LOG(x) ;
#endif

/** How many msec to wait before starting to smooth thumbnails */
const int SMOOTH_DELAY = 500;

const int DRAG_THUMB_SIZE = KIconLoader::SizeHuge;
const int DRAG_THUMB_SPACING = 4;

const int WHEEL_ZOOM_MULTIPLIER = 4;

static KFileItem fileItemForIndex(const QModelIndex& index) {
	if (!index.isValid()) {
		LOG("Invalid index");
		return KFileItem();
	}
	QVariant data = index.data(KDirModel::FileItemRole);
	return qvariant_cast<KFileItem>(data);
}


static KUrl urlForIndex(const QModelIndex& index) {
	KFileItem item = fileItemForIndex(index);
	return item.isNull() ? KUrl() : item.url();
}

struct Thumbnail {
	Thumbnail(const QPersistentModelIndex& index_, const KDateTime& mtime)
	: mIndex(index_)
	, mModificationTime(mtime)
	, mRough(true)
	, mWaitingForThumbnail(true) {}

	Thumbnail()
	: mRough(true)
	, mWaitingForThumbnail(true) {}

	/**
	 * Init the thumbnail based on a icon
	 */
	void initAsIcon(const QPixmap& pix) {
		mGroupPix = pix;
		int largeGroupSize = ThumbnailGroup::pixelSize(ThumbnailGroup::Large);
		mFullSize = QSize(largeGroupSize, largeGroupSize);
	}

	bool isGroupPixAdaptedForSize(int size) const {
		if (mWaitingForThumbnail) {
			return false;
		}
		if (mGroupPix.isNull()) {
			return false;
		}
		const int groupSize = qMax(mGroupPix.width(), mGroupPix.height());
		if (groupSize >= size) {
			return true;
		}

		// groupSize is less than size, but this may be because the full image
		// is the same size as groupSize
		return groupSize == qMax(mFullSize.width(), mFullSize.height());
	}

	void prepareForRefresh(const KDateTime& mtime) {
		mModificationTime = mtime;
		mGroupPix = QPixmap();
		mAdjustedPix = QPixmap();
		mFullSize = QSize();
		mRealFullSize = QSize();
		mRough = true;
		mWaitingForThumbnail = true;
	}

	QPersistentModelIndex mIndex;
	KDateTime mModificationTime;
	/// The pix loaded from .thumbnails/{large,normal}
	QPixmap mGroupPix;
	/// Scaled version of mGroupPix, adjusted to ThumbnailView::thumbnailSize
	QPixmap mAdjustedPix;
	/// Size of the full image
	QSize mFullSize;
	/// Real size of the full image, invalid unless the thumbnail
	/// represents a raster image (not an icon)
	QSize mRealFullSize;
	/// Whether mAdjustedPix represents has been scaled using fast or smooth
	//transformation
	bool mRough;
	/// Set to true if mGroupPix should be replaced with a real thumbnail
	bool mWaitingForThumbnail;
};

typedef QHash<QUrl, Thumbnail> ThumbnailForUrl;
typedef QQueue<KUrl> UrlQueue;
typedef QSet<QPersistentModelIndex> PersistentModelIndexSet;

struct ThumbnailViewPrivate {
	ThumbnailView* that;
	int mThumbnailSize;
	AbstractDocumentInfoProvider* mDocumentInfoProvider;
	AbstractThumbnailViewHelper* mThumbnailViewHelper;
	ThumbnailForUrl mThumbnailForUrl;
	QTimer mScheduledThumbnailGenerationTimer;

	UrlQueue mSmoothThumbnailQueue;
	QTimer mSmoothThumbnailTimer;

	QPixmap mWaitingThumbnail;
	QPointer<ThumbnailLoadJob> mThumbnailLoadJob;

	PersistentModelIndexSet mBusyIndexSet;
	KPixmapSequence mBusySequence;
	QTimeLine* mBusyAnimationTimeLine;

	void setupBusyAnimation() {
		mBusySequence = KPixmapSequence("process-working", 22);
		mBusyAnimationTimeLine = new QTimeLine(100 * mBusySequence.frameCount(), that);
		mBusyAnimationTimeLine->setCurveShape(QTimeLine::LinearCurve);
		mBusyAnimationTimeLine->setEndFrame(mBusySequence.frameCount() - 1);
		mBusyAnimationTimeLine->setLoopCount(0);
		QObject::connect(mBusyAnimationTimeLine, SIGNAL(frameChanged(int)),
			that, SLOT(updateBusyIndexes()));
	}

	void scheduleThumbnailGenerationForVisibleItems() {
		if (mThumbnailLoadJob) {
			mThumbnailLoadJob->removeItems(mThumbnailLoadJob->pendingItems());
		}
		mSmoothThumbnailQueue.clear();
		mScheduledThumbnailGenerationTimer.start();
	}

	void updateThumbnailForModifiedDocument(const QModelIndex& index) {
		Q_ASSERT(mDocumentInfoProvider);
		KFileItem item = fileItemForIndex(index);
		KUrl url = item.url();
		ThumbnailGroup::Enum group = ThumbnailGroup::fromPixelSize(mThumbnailSize);
		QPixmap pix;
		QSize fullSize;
		mDocumentInfoProvider->thumbnailForDocument(url, group, &pix, &fullSize);
		mThumbnailForUrl[url] = Thumbnail(QPersistentModelIndex(index), KDateTime::currentLocalDateTime());
		that->setThumbnail(item, pix, fullSize);
	}

	void generateThumbnailsForItems(const KFileItemList& list) {
		ThumbnailGroup::Enum group = ThumbnailGroup::fromPixelSize(mThumbnailSize);
		if (!mThumbnailLoadJob) {
			mThumbnailLoadJob = new ThumbnailLoadJob(list, group);
			QObject::connect(mThumbnailLoadJob, SIGNAL(thumbnailLoaded(KFileItem,QPixmap,QSize)),
				that, SLOT(setThumbnail(KFileItem,QPixmap,QSize)));
			QObject::connect(mThumbnailLoadJob, SIGNAL(thumbnailLoadingFailed(KFileItem)),
				that, SLOT(setBrokenThumbnail(KFileItem)));
			mThumbnailLoadJob->start();
		} else {
			mThumbnailLoadJob->setThumbnailGroup(group);
			Q_FOREACH(const KFileItem& item, list) {
				mThumbnailLoadJob->appendItem(item);
			}
		}
	}

	void roughAdjustThumbnail(Thumbnail* thumbnail) {
		const QPixmap& mGroupPix = thumbnail->mGroupPix;
		const int groupSize = qMax(mGroupPix.width(), mGroupPix.height());
		const int fullSize = qMax(thumbnail->mFullSize.width(), thumbnail->mFullSize.height());
		if (fullSize == groupSize && groupSize <= mThumbnailSize) {
			thumbnail->mAdjustedPix = mGroupPix;
			thumbnail->mRough = false;
		} else {
			thumbnail->mAdjustedPix = mGroupPix.scaled(mThumbnailSize, mThumbnailSize, Qt::KeepAspectRatio);
			thumbnail->mRough = true;
		}
	}

	QPixmap dragPixmapForIndex(const QModelIndex& index) const {
		KUrl url = urlForIndex(index);
		QPixmap pix = mThumbnailForUrl.value(url).mAdjustedPix;
		if (qMax(pix.width(), pix.height()) > DRAG_THUMB_SIZE) {
			return pix.scaled(DRAG_THUMB_SIZE, DRAG_THUMB_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		} else {
			return pix;
		}
	}

	QPixmap createDragPixmap(const QModelIndexList& indexes) {
		const int MAX_THUMBS = 3;

		int thumbCount;
		bool more;
		if (indexes.count() > MAX_THUMBS) {
			thumbCount = MAX_THUMBS;
			more = true;
		} else {
			thumbCount = indexes.count();
			more = false;
		}
		QList<QPixmap> thumbs;
		int width = 0;
		int height = 0;
		for (int row=0; row<thumbCount; ++row) {
			QModelIndex index;
			if (row == thumbCount-1 && more) {
				QString text = "(...)";
				QPixmap pix(that->fontMetrics().boundingRect(text).size());
				pix.fill(Qt::transparent);
				{
					QPainter painter(&pix);
					painter.drawText(pix.rect(), Qt::AlignHCenter | Qt::AlignBottom, text);
				}
				index = indexes.last();
				width += pix.width();
				thumbs << pix;
			} else {
				index = indexes[row];
			}
			QPixmap thumb = dragPixmapForIndex(index);
			height = qMax(height, thumb.height());
			width += thumb.width();
			thumbs << thumb;
		}

		QPixmap pix(
			width + (thumbs.count() + 1) * DRAG_THUMB_SPACING,
			height + 2 * DRAG_THUMB_SPACING
			);
		pix.fill(QToolTip::palette().color(QPalette::Inactive, QPalette::ToolTipBase));
		QPainter painter(&pix);

		int x = DRAG_THUMB_SPACING;
		Q_FOREACH(const QPixmap& thumb, thumbs) {
			painter.drawPixmap(x, (pix.height() - thumb.height()) / 2, thumb);
			x += thumb.width() + DRAG_THUMB_SPACING;
		}
		return pix;
	}
};


ThumbnailView::ThumbnailView(QWidget* parent)
: QListView(parent)
, d(new ThumbnailViewPrivate) {
	d->that = this;
	d->mThumbnailViewHelper = 0;
	d->mDocumentInfoProvider = 0;
	// Init to some stupid value so that the first call to setThumbnailSize()
	// is not ignored (do not use 0 in case someone try to divide by
	// mThumbnailSize...)
	d->mThumbnailSize = 1;

	setFrameShape(QFrame::NoFrame);
	setViewMode(QListView::IconMode);
	setResizeMode(QListView::Adjust);
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setUniformItemSizes(true);
	setEditTriggers(QAbstractItemView::EditKeyPressed);

	d->setupBusyAnimation();

	viewport()->setMouseTracking(true);
	// Set this attribute, otherwise the item delegate won't get the
	// State_MouseOver state
	viewport()->setAttribute(Qt::WA_Hover);

	setVerticalScrollMode(ScrollPerPixel);
	setHorizontalScrollMode(ScrollPerPixel);

	d->mScheduledThumbnailGenerationTimer.setSingleShot(true);
	d->mScheduledThumbnailGenerationTimer.setInterval(500);
	connect(&d->mScheduledThumbnailGenerationTimer, SIGNAL(timeout()),
		SLOT(generateThumbnailsForVisibleItems()) );

	d->mSmoothThumbnailTimer.setSingleShot(true);
	connect(&d->mSmoothThumbnailTimer, SIGNAL(timeout()),
		SLOT(smoothNextThumbnail()) );

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, SIGNAL(customContextMenuRequested(QPoint)),
		SLOT(showContextMenu()) );

	if (KGlobalSettings::singleClick()) {
		connect(this, SIGNAL(clicked(QModelIndex)),
			SLOT(emitIndexActivatedIfNoModifiers(QModelIndex)) );
	} else {
		connect(this, SIGNAL(doubleClicked(QModelIndex)),
			SLOT(emitIndexActivatedIfNoModifiers(QModelIndex)) );
	}
}


ThumbnailView::~ThumbnailView() {
	delete d;
}


void ThumbnailView::setModel(QAbstractItemModel* newModel) {
	if (model()) {
		disconnect(model(), 0, this, 0);
	}
	QListView::setModel(newModel);
	connect(model(), SIGNAL(rowsRemoved(QModelIndex,int,int)),
		SIGNAL(rowsRemovedSignal(QModelIndex,int,int)));
}


void ThumbnailView::setThumbnailSize(int value) {
	if (d->mThumbnailSize == value) {
		return;
	}
	d->mThumbnailSize = value;

	// mWaitingThumbnail
	int waitingThumbnailSize;
	if (value > 64) {
		waitingThumbnailSize = 48;
	} else {
		waitingThumbnailSize = 32;
	}
	if (d->mWaitingThumbnail.width() != waitingThumbnailSize) {
		QPixmap icon = DesktopIcon("chronometer", waitingThumbnailSize);
		QPixmap pix(icon.size());
		pix.fill(Qt::transparent);
		QPainter painter(&pix);
		painter.setOpacity(0.5);
		painter.drawPixmap(0, 0, icon);
		painter.end();
		d->mWaitingThumbnail = pix;
	}

	// Stop smoothing
	d->mSmoothThumbnailTimer.stop();
	d->mSmoothThumbnailQueue.clear();

	// Clear adjustedPixes
	ThumbnailForUrl::iterator
		it = d->mThumbnailForUrl.begin(),
		end = d->mThumbnailForUrl.end();
	for (; it!=end; ++it) {
		it.value().mAdjustedPix = QPixmap();
	}

	thumbnailSizeChanged(value);
	d->scheduleThumbnailGenerationForVisibleItems();
}


int ThumbnailView::thumbnailSize() const {
	return d->mThumbnailSize;
}


void ThumbnailView::setThumbnailViewHelper(AbstractThumbnailViewHelper* helper) {
	d->mThumbnailViewHelper = helper;
}

AbstractThumbnailViewHelper* ThumbnailView::thumbnailViewHelper() const {
	return d->mThumbnailViewHelper;
}


void ThumbnailView::setDocumentInfoProvider(AbstractDocumentInfoProvider* provider) {
	d->mDocumentInfoProvider = provider;
	if (provider) {
		connect(provider, SIGNAL(busyStateChanged(QModelIndex,bool)),
			SLOT(updateThumbnailBusyState(QModelIndex,bool)));
		connect(provider, SIGNAL(documentChanged(QModelIndex)),
			SLOT(updateThumbnail(QModelIndex)));
	}
}

AbstractDocumentInfoProvider* ThumbnailView::documentInfoProvider() const {
	return d->mDocumentInfoProvider;
}


void ThumbnailView::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) {
	QListView::rowsAboutToBeRemoved(parent, start, end);

	// Remove references to removed items
	KFileItemList itemList;
	for (int pos=start; pos<=end; ++pos) {
		QModelIndex index = model()->index(pos, 0, parent);
		KFileItem item = fileItemForIndex(index);
		if (item.isNull()) {
			kDebug() << "Skipping invalid item!" << index.data().toString();
			continue;
		}

		QUrl url = item.url();
		d->mThumbnailForUrl.remove(url);
		d->mSmoothThumbnailQueue.removeAll(url);

		itemList.append(item);
	}

	if (d->mThumbnailLoadJob) {
		d->mThumbnailLoadJob->removeItems(itemList);
	}

	// Update current index if it is among the deleted rows
	const int row = currentIndex().row();
	if (start <= row && row <= end) {
		QModelIndex index;
		if (end < model()->rowCount() - 1) {
			index = model()->index(end + 1, 0);
		} else if (start > 0) {
			index = model()->index(start - 1, 0);
		}
		setCurrentIndex(index);
	}

	// Removing rows might make new images visible, make sure their thumbnail
	// is generated
	d->mScheduledThumbnailGenerationTimer.start();
}


void ThumbnailView::rowsInserted(const QModelIndex& parent, int start, int end) {
	QListView::rowsInserted(parent, start, end);
	d->mScheduledThumbnailGenerationTimer.start();
	rowsInsertedSignal(parent, start, end);
}


void ThumbnailView::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) {
	QListView::dataChanged(topLeft, bottomRight);
	bool thumbnailsNeedRefresh = false;
	for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
		QModelIndex index = model()->index(row, 0);
		KFileItem item = fileItemForIndex(index);
		if (item.isNull()) {
			kWarning() << "Invalid item for index" << index << ". This should not happen!";
			continue;
		}

		ThumbnailForUrl::Iterator it = d->mThumbnailForUrl.find(item.url());
		if (it != d->mThumbnailForUrl.end()) {
			// All thumbnail views are connected to the model, so
			// ThumbnailView::dataChanged() is called for all of them. As a
			// result this method will also be called for views which are not
			// currently visible, and do not yet have a thumbnail for the
			// modified url.
			KDateTime mtime = item.time(KFileItem::ModificationTime);
			if (it->mModificationTime != mtime) {
				// dataChanged() is called when the file changes but also when
				// the model fetched additional data such as semantic info. To
				// avoid needless refreshes, we only trigger a refresh if the
				// modification time changes.
				thumbnailsNeedRefresh = true;
				it->prepareForRefresh(mtime);
			}
		}
	}
	if (thumbnailsNeedRefresh) {
		d->mScheduledThumbnailGenerationTimer.start();
	}
}


void ThumbnailView::showContextMenu() {
	d->mThumbnailViewHelper->showContextMenu(this);
}


void ThumbnailView::emitIndexActivatedIfNoModifiers(const QModelIndex& index) {
	if (QApplication::keyboardModifiers() == Qt::NoModifier) {
		emit indexActivated(index);
	}
}


void ThumbnailView::setThumbnail(const KFileItem& item, const QPixmap& pixmap, const QSize& size) {
	ThumbnailForUrl::iterator it = d->mThumbnailForUrl.find(item.url());
	if (it == d->mThumbnailForUrl.end()) {
		return;
	}
	Thumbnail& thumbnail = it.value();
	thumbnail.mGroupPix = pixmap;
	thumbnail.mAdjustedPix = QPixmap();
	int largeGroupSize = ThumbnailGroup::pixelSize(ThumbnailGroup::Large);
	thumbnail.mFullSize = size.isValid() ? size : QSize(largeGroupSize, largeGroupSize);
	thumbnail.mRealFullSize = size;
	thumbnail.mWaitingForThumbnail = false;

	update(thumbnail.mIndex);
}


void ThumbnailView::setBrokenThumbnail(const KFileItem& item) {
	ThumbnailForUrl::iterator it = d->mThumbnailForUrl.find(item.url());
	if (it == d->mThumbnailForUrl.end()) {
		return;
	}
	Thumbnail& thumbnail = it.value();
	MimeTypeUtils::Kind kind = MimeTypeUtils::fileItemKind(item);
	if (kind == MimeTypeUtils::KIND_VIDEO) {
		// Special case for videos because our kde install may come without
		// support for video thumbnails so we show the mimetype icon instead of
		// a broken image icon
		ThumbnailGroup::Enum group = ThumbnailGroup::fromPixelSize(d->mThumbnailSize);
		QPixmap pix = item.pixmap(ThumbnailGroup::pixelSize(group));
		thumbnail.initAsIcon(pix);
	} else if (kind == MimeTypeUtils::KIND_DIR) {
		// Special case for folders because ThumbnailLoadJob does not return a
		// thumbnail if there is no images
		thumbnail.mWaitingForThumbnail = false;
		return;
	} else {
		thumbnail.initAsIcon(DesktopIcon("image-missing", 48));
		thumbnail.mFullSize = thumbnail.mGroupPix.size();
	}
	update(thumbnail.mIndex);
}


QPixmap ThumbnailView::thumbnailForIndex(const QModelIndex& index, QSize* fullSize) {
	KFileItem item = fileItemForIndex(index);
	if (item.isNull()) {
		LOG("Invalid item");
		if (fullSize) {
			*fullSize = QSize();
		}
		return QPixmap();
	}
	KUrl url = item.url();

	// Find or create Thumbnail instance
	ThumbnailForUrl::Iterator it = d->mThumbnailForUrl.find(url);
	if (it == d->mThumbnailForUrl.end()) {
		Thumbnail thumbnail = Thumbnail(QPersistentModelIndex(index), item.time(KFileItem::ModificationTime));
		it = d->mThumbnailForUrl.insert(url, thumbnail);
	}
	Thumbnail& thumbnail = it.value();

	// If dir or archive, generate a thumbnail from fileitem pixmap
	MimeTypeUtils::Kind kind = MimeTypeUtils::fileItemKind(item);
	if (kind == MimeTypeUtils::KIND_ARCHIVE || kind == MimeTypeUtils::KIND_DIR) {
		int groupSize = ThumbnailGroup::pixelSize(ThumbnailGroup::fromPixelSize(d->mThumbnailSize));
		if (thumbnail.mGroupPix.isNull() || thumbnail.mGroupPix.width() < groupSize) {
			QPixmap pix = item.pixmap(groupSize);
			thumbnail.initAsIcon(pix);
			if (kind == MimeTypeUtils::KIND_ARCHIVE) {
				// No thumbnails for archives
				thumbnail.mWaitingForThumbnail = false;
			} else {
				// set mWaitingForThumbnail to true (necessary in the case
				// 'thumbnail' already existed before, but with a too small
				// mGroupPix)
				thumbnail.mWaitingForThumbnail = true;
			}
		}
	}

	if (thumbnail.mGroupPix.isNull()) {
		if (fullSize) {
			*fullSize = QSize();
		}
		return d->mWaitingThumbnail;
	}

	// Adjust thumbnail
	if (thumbnail.mAdjustedPix.isNull()) {
		d->roughAdjustThumbnail(&thumbnail);
	}
	if (thumbnail.mRough && !d->mSmoothThumbnailQueue.contains(url)) {
		d->mSmoothThumbnailQueue.enqueue(url);
		if (!d->mSmoothThumbnailTimer.isActive()) {
			d->mSmoothThumbnailTimer.start(SMOOTH_DELAY);
		}
	}
	if (fullSize) {
		*fullSize = thumbnail.mRealFullSize;
	}
	return thumbnail.mAdjustedPix;
}


bool ThumbnailView::isModified(const QModelIndex& index) const {
	if (!d->mDocumentInfoProvider) {
		return false;
	}
	KUrl url = urlForIndex(index);
	return d->mDocumentInfoProvider->isModified(url);
}


bool ThumbnailView::isBusy(const QModelIndex& index) const {
	if (!d->mDocumentInfoProvider) {
		return false;
	}
	KUrl url = urlForIndex(index);
	return d->mDocumentInfoProvider->isBusy(url);
}


void ThumbnailView::startDrag(Qt::DropActions supportedActions) {
	QModelIndexList indexes = selectionModel()->selectedIndexes();
	if (indexes.isEmpty()) {
		return;
	}
	QDrag* drag = new QDrag(this);
	drag->setMimeData(model()->mimeData(indexes));
	QPixmap pix = d->createDragPixmap(indexes);
	drag->setPixmap(pix);
	drag->exec(supportedActions, Qt::CopyAction);
}


void ThumbnailView::dragEnterEvent(QDragEnterEvent* event) {
	QAbstractItemView::dragEnterEvent(event);
	if (event->mimeData()->hasUrls()) {
		event->acceptProposedAction();
	}
}


void ThumbnailView::dragMoveEvent(QDragMoveEvent* event) {
	// Necessary, otherwise we don't reach dropEvent()
	QAbstractItemView::dragMoveEvent(event);
	event->acceptProposedAction();
}


void ThumbnailView::dropEvent(QDropEvent* event) {
	const KUrl::List urlList = KUrl::List::fromMimeData(event->mimeData());
	if (urlList.isEmpty()) {
		return;
	}

	QModelIndex destIndex = indexAt(event->pos());
	if (destIndex.isValid()) {
		KFileItem item = fileItemForIndex(destIndex);
		if (item.isDir()) {
			KUrl destUrl = item.url();
			d->mThumbnailViewHelper->showMenuForUrlDroppedOnDir(this, urlList, destUrl);
			return;
		}
	}

	d->mThumbnailViewHelper->showMenuForUrlDroppedOnViewport(this, urlList);

	event->acceptProposedAction();
}


void ThumbnailView::keyPressEvent(QKeyEvent* event) {
	QListView::keyPressEvent(event);
	if (event->key() == Qt::Key_Return) {
		const QModelIndex index = selectionModel()->currentIndex();
		if (index.isValid() && selectionModel()->selectedIndexes().count() == 1) {
			emit indexActivated(index);
		}
	}
}


void ThumbnailView::resizeEvent(QResizeEvent* event) {
	QListView::resizeEvent(event);
	d->scheduleThumbnailGenerationForVisibleItems();
}


void ThumbnailView::showEvent(QShowEvent* event) {
	QListView::showEvent(event);
	d->scheduleThumbnailGenerationForVisibleItems();
	QTimer::singleShot(0, this, SLOT(scrollToSelectedIndex()));
}


void ThumbnailView::wheelEvent(QWheelEvent* event) {
	// If we don't adjust the single step, the wheel scroll exactly one item up
	// and down, giving the impression that the items do not move but only
	// their label changes.
	// For some reason it is necessary to set the step here: setting it in
	// setThumbnailSize() does not work
	//verticalScrollBar()->setSingleStep(d->mThumbnailSize / 5);
	if (event->modifiers() == Qt::ControlModifier) {
		int size = d->mThumbnailSize + (event->delta() > 0 ? 1 : -1) * WHEEL_ZOOM_MULTIPLIER;
		size = qMax(int(MinThumbnailSize), qMin(size, int(MaxThumbnailSize)));
		setThumbnailSize(size);
	} else {
		QListView::wheelEvent(event);
	}
}


void ThumbnailView::scrollToSelectedIndex() {
	QModelIndexList list = selectedIndexes();
	if (list.count() >= 1) {
		scrollTo(list.first(), PositionAtCenter);
	}
}


void ThumbnailView::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
	QListView::selectionChanged(selected, deselected);
	emit selectionChangedSignal(selected, deselected);
}


void ThumbnailView::scrollContentsBy(int dx, int dy) {
	QListView::scrollContentsBy(dx, dy);
	d->scheduleThumbnailGenerationForVisibleItems();
}


void ThumbnailView::generateThumbnailsForVisibleItems() {
	if (!isVisible() || !model()) {
		return;
	}
	KFileItemList list;
	QRect visibleRect = viewport()->rect();
	// Adjust visibleRect so that next invisible rows of thumbnails
	// get generated too
	if (isWrapping()) {
		visibleRect.adjust(0, 0, 0, d->mThumbnailSize * 2);
	} else {
		visibleRect.adjust(0, 0, visibleRect.width() / 2, 0);
	}

	for (int row=0; row < model()->rowCount(); ++row) {
		QModelIndex index = model()->index(row, 0);
		KFileItem item = fileItemForIndex(index);
		QUrl url = item.url();

		// Filter out invisible items
		QRect rect = visualRect(index);
		if (!visibleRect.intersects(rect)) {
			continue;
		}

		// Filter out archives
		MimeTypeUtils::Kind kind = MimeTypeUtils::fileItemKind(item);
		if (kind == MimeTypeUtils::KIND_ARCHIVE) {
			continue;
		}

		// Immediately update modified items
		if (d->mDocumentInfoProvider && d->mDocumentInfoProvider->isModified(url)) {
			d->updateThumbnailForModifiedDocument(index);
			continue;
		}

		// Filter out items which already have a thumbnail
		ThumbnailForUrl::ConstIterator it = d->mThumbnailForUrl.constFind(url);
		if (it != d->mThumbnailForUrl.constEnd() && it.value().isGroupPixAdaptedForSize(d->mThumbnailSize)) {
			continue;
		}

		// Add the item to our list
		list << item;

		// Insert the thumbnail in mThumbnailForUrl, so that
		// setThumbnail() can find the item to update
		if (it == d->mThumbnailForUrl.constEnd()) {
			Thumbnail thumbnail = Thumbnail(QPersistentModelIndex(index), item.time(KFileItem::ModificationTime));
			d->mThumbnailForUrl.insert(url, thumbnail);
		}
	}

	if (!list.empty()) {
		d->generateThumbnailsForItems(list);
	}
}


void ThumbnailView::updateThumbnail(const QModelIndex& index) {
	KFileItem item = fileItemForIndex(index);
	KUrl url = item.url();
	if (d->mDocumentInfoProvider && d->mDocumentInfoProvider->isModified(url)) {
		d->updateThumbnailForModifiedDocument(index);
	} else {
		KFileItemList list;
		list << item;
		d->generateThumbnailsForItems(list);
	}
}


void ThumbnailView::updateThumbnailBusyState(const QModelIndex& _index, bool busy) {
	QPersistentModelIndex index(_index);
	if (busy && !d->mBusyIndexSet.contains(index)) {
		d->mBusyIndexSet << index;
		update(index);
		if (d->mBusyAnimationTimeLine->state() != QTimeLine::Running) {
			d->mBusyAnimationTimeLine->start();
		}
	} else if (!busy && d->mBusyIndexSet.remove(index)) {
		update(index);
		if (d->mBusyIndexSet.isEmpty()) {
			d->mBusyAnimationTimeLine->stop();
		}
	}
}


void ThumbnailView::updateBusyIndexes() {
	Q_FOREACH(const QPersistentModelIndex& index, d->mBusyIndexSet) {
		update(index);
	}
}


QPixmap ThumbnailView::busySequenceCurrentPixmap() const {
	return d->mBusySequence.frameAt(d->mBusyAnimationTimeLine->currentFrame());
}


void ThumbnailView::smoothNextThumbnail() {
	if (d->mSmoothThumbnailQueue.isEmpty()) {
		return;
	}

	if (d->mThumbnailLoadJob) {
		// give mThumbnailLoadJob priority over smoothing
		d->mSmoothThumbnailTimer.start(SMOOTH_DELAY);
		return;
	}

	KUrl url = d->mSmoothThumbnailQueue.dequeue();
	ThumbnailForUrl::Iterator it = d->mThumbnailForUrl.find(url);
	if (it == d->mThumbnailForUrl.end()) {
		kWarning() <<  url << " not in mThumbnailForUrl. This should not happen!";
		return;
	}

	Thumbnail& thumbnail = it.value();
	thumbnail.mAdjustedPix = thumbnail.mGroupPix.scaled(d->mThumbnailSize, d->mThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	thumbnail.mRough = false;

	if (thumbnail.mIndex.isValid()) {
		update(thumbnail.mIndex);
	} else {
		kWarning() << "index for" << url << "is invalid. This should not happen!";
	}

	if (!d->mSmoothThumbnailQueue.isEmpty()) {
		d->mSmoothThumbnailTimer.start(0);
	}
}


void ThumbnailView::reloadThumbnail(const QModelIndex& index) {
	KUrl url = urlForIndex(index);
	if (!url.isValid()) {
		kWarning() << "Invalid url for index" << index;
		return;
	}
	ThumbnailLoadJob::deleteImageThumbnail(url);
	ThumbnailForUrl::Iterator it = d->mThumbnailForUrl.find(url);
	if (it == d->mThumbnailForUrl.end()) {
		return;
	}
	d->mThumbnailForUrl.erase(it);
	generateThumbnailsForVisibleItems();
}


} // namespace
