/*
Gwenview: an image viewer
Copyright 2007 Aurélien Gâteau

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
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QToolTip>

// KDE
#include <kdebug.h>
#include <kdirmodel.h>
#include <kglobalsettings.h>

// Local
#include "archiveutils.h"
#include "abstractthumbnailviewhelper.h"
#include "paintutils.h"

namespace Gwenview {

/**
 * Space between the item outer rect and the content, and between the
 * thumbnail and the caption
 */
const int ITEM_MARGIN = 5;

/** How darker is the border line around selection */
const int SELECTION_BORDER_DARKNESS = 140;

/** Radius of the selection rounded corners, in pixels */
const int SELECTION_RADIUS = 10;

/** Border around gadget icons */
const int GADGET_MARGIN = 2;

/** Radius of the gadget frame, in pixels */
const int GADGET_RADIUS = 6;

/** How many pixels between items */
const int SPACING = 11;

/** How dark is the shadow, 0 is invisible, 255 is as dark as possible */
const int SHADOW_STRENGTH = 128;

/** How many pixels around the thumbnail are shadowed */
const int SHADOW_SIZE = 4;


/**
 * A frame with a rounded semi-opaque background. Since it's not possible (yet)
 * to define non-opaque colors in Qt stylesheets, we do it the old way: by
 * reimplementing paintEvent().
 */
class GlossyFrame : public QFrame {
public:
	GlossyFrame(QWidget* parent = 0)
	: QFrame(parent)
	, mOpaque(false)
	{}

	void setOpaque(bool value) {
		if (value != mOpaque) {
			mOpaque = value;
			update();
		}
	}

	void setBackgroundColor(const QColor& color) {
		QPalette pal = palette();
		pal.setColor(backgroundRole(), color);
		setPalette(pal);
	}

protected:
	virtual void paintEvent(QPaintEvent* /*event*/) {
		QColor color = palette().color(backgroundRole());
		QColor borderColor;
		QRectF rectF = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		QPainterPath path = PaintUtils::roundedRectangle(rectF, GADGET_RADIUS);

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		if (mOpaque) {
			painter.fillPath(path, color);
			borderColor = color;
		} else {
			QLinearGradient gradient(rect().topLeft(), rect().bottomLeft());
			gradient.setColorAt(0, PaintUtils::alphaAdjustedF(color, 0.9));
			gradient.setColorAt(1, PaintUtils::alphaAdjustedF(color, 0.7));
			painter.fillPath(path, gradient);
			borderColor = color.dark(SELECTION_BORDER_DARKNESS);
		}
		painter.setPen(borderColor);
		painter.drawPath(path);
	}

private:
	bool mOpaque;
};


static KFileItem fileItemForIndex(const QModelIndex& index) {
	Q_ASSERT(index.isValid());
	QVariant data = index.data(KDirModel::FileItemRole);
	return qvariant_cast<KFileItem>(data);
}


static KUrl urlForIndex(const QModelIndex& index) {
	KFileItem item = fileItemForIndex(index);
	return item.url();
}


static QToolButton* createFrameButton(QWidget* parent, const char* iconName) {
	int size = KIconLoader::global()->currentSize(KIconLoader::Small);
	QToolButton* button = new QToolButton(parent);
	button->setIcon(SmallIcon(iconName));
	button->setIconSize(QSize(size, size));
	button->setAutoRaise(true);

	return button;
}


ThumbnailView::Thumbnail::Thumbnail(const QPixmap& pixmap)
: mPixmap(pixmap) {
	if (mPixmap.isNull()) {
		mOpaque = true;
		return;
	}
	QImage img = mPixmap.toImage();
	int a1 = qAlpha(img.pixel(0, 0));
	int a2 = qAlpha(img.pixel(img.width() - 1, 0));
	int a3 = qAlpha(img.pixel(0, img.height() - 1));
	int a4 = qAlpha(img.pixel(img.width() - 1, img.height() - 1));
	mOpaque = a1 + a2 + a3 + a4 == 4*255;
}


ThumbnailView::Thumbnail::Thumbnail() {}


ThumbnailView::Thumbnail::Thumbnail(const ThumbnailView::Thumbnail& other)
: mPixmap(other.mPixmap)
, mOpaque(other.mOpaque) {}


/**
 * An ItemDelegate which generates thumbnails for images. It also makes sure
 * all items are of the same size.
 */
class PreviewItemDelegate : public QAbstractItemDelegate {
public:
	PreviewItemDelegate(ThumbnailView* view)
	: QAbstractItemDelegate(view)
	, mView(view)
	{
		QColor bgColor = mView->palette().highlight().color();
		QColor borderColor = bgColor.dark(SELECTION_BORDER_DARKNESS);

		QString styleSheet =
			"QFrame {"
			"	padding: 1px;"
			"}"

			"QToolButton {"
			"	padding: 2px;"
			"	border-radius: 4px;"
			"}"

			"QToolButton:hover {"
			"	border: 1px solid %2;"
			"}"

			"QToolButton:pressed {"
			"	background-color:"
			"		qlineargradient(x1:0, y1:0, x2:0, y2:1,"
			"		stop:0 %2, stop:1 %1);"
			"	border: 1px solid %2;"
			"}";
		styleSheet = styleSheet.arg(bgColor.name()).arg(borderColor.name());

		// Button frame
		mButtonFrame = new GlossyFrame(mView->viewport());
		mButtonFrame->setStyleSheet(styleSheet);
		mButtonFrame->setBackgroundColor(bgColor);
		mButtonFrame->hide();

		QToolButton* fullScreenButton = createFrameButton(mButtonFrame, "view-fullscreen");
		connect(fullScreenButton, SIGNAL(clicked()),
			mView, SLOT(slotFullScreenClicked()) );

		QToolButton* rotateLeftButton = createFrameButton(mButtonFrame, "object-rotate-left");
		connect(rotateLeftButton, SIGNAL(clicked()),
			mView, SLOT(slotRotateLeftClicked()) );

		QToolButton* rotateRightButton = createFrameButton(mButtonFrame, "object-rotate-right");
		connect(rotateRightButton, SIGNAL(clicked()),
			mView, SLOT(slotRotateRightClicked()) );

		QHBoxLayout* layout = new QHBoxLayout(mButtonFrame);
		layout->setMargin(0);
		layout->setSpacing(0);
		layout->addWidget(fullScreenButton);
		layout->addWidget(rotateLeftButton);
		layout->addWidget(rotateRightButton);

		// Save button frame
		mSaveButtonFrame = new GlossyFrame(mView->viewport());
		mSaveButtonFrame->setStyleSheet(styleSheet);
		mSaveButtonFrame->setBackgroundColor(bgColor);
		mSaveButtonFrame->hide();

		QToolButton* saveButton = createFrameButton(mSaveButtonFrame, "document-save");
		connect(saveButton, SIGNAL(clicked()),
			mView, SLOT(slotSaveClicked()) );

		layout = new QHBoxLayout(mSaveButtonFrame);
		layout->setMargin(0);
		layout->setSpacing(0);
		layout->addWidget(saveButton);

		initSaveButtonFramePixmap();
	}

	void initSaveButtonFramePixmap() {
		// Necessary otherwise we won't see the save button itself
		mSaveButtonFrame->adjustSize();

		// This is hackish.
		// Show/hide the frame to make sure mSaveButtonFrame->render produces
		// something coherent.
		mSaveButtonFrame->show();
		mSaveButtonFrame->repaint();
		mSaveButtonFrame->hide();

		mSaveButtonFramePixmap = QPixmap(mSaveButtonFrame->size());
		mSaveButtonFramePixmap.fill(Qt::transparent);
		mSaveButtonFrame->render(&mSaveButtonFramePixmap, QPoint(), QRegion(), QWidget::DrawChildren);
	}


	void clearElidedTextMap() {
		mElidedTextMap.clear();
	}


	virtual QSize sizeHint( const QStyleOptionViewItem & /*option*/, const QModelIndex & /*index*/ ) const {
		return QSize( mView->itemWidth(), mView->itemHeight() );
	}


	virtual bool eventFilter(QObject* object, QEvent* event) {
		if (event->type() == QEvent::ToolTip) {
			QAbstractItemView* view = static_cast<QAbstractItemView*>(object->parent());
			QHelpEvent* helpEvent = static_cast<QHelpEvent*>(event);
			showToolTip(view, helpEvent);
			return true;

		} else if (event->type() == QEvent::HoverMove) {
			QHoverEvent* hoverEvent = static_cast<QHoverEvent*>(event);
			return hoverEventFilter(hoverEvent);
		}
		return false;
	}


	bool hoverEventFilter(QHoverEvent* event) {
		QModelIndex index = mView->indexAt(event->pos());
		if (index == mIndexUnderCursor) {
			// Same index, nothing to do
			return false;
		}
		mIndexUnderCursor = index;

		bool showButtonFrames = false;
		if (mIndexUnderCursor.isValid()) {
			KFileItem item = fileItemForIndex(mIndexUnderCursor);
			showButtonFrames = !ArchiveUtils::fileItemIsDirOrArchive(item);
		}

		if (showButtonFrames) {
			QRect rect = mView->visualRect(mIndexUnderCursor);
			mButtonFrame->adjustSize();
			updateButtonFrameOpacity();
			int posX = rect.x() + (rect.width() - mButtonFrame->width()) / 2;
			int posY = rect.y() + GADGET_MARGIN;
			mButtonFrame->move(posX, posY);
			mButtonFrame->show();

			if (mView->isModified(mIndexUnderCursor)) {
				showSaveButtonFrame(rect);
			} else {
				mSaveButtonFrame->hide();
			}

		} else {
			mButtonFrame->hide();
			mSaveButtonFrame->hide();
		}
		return false;
	}


	virtual void paint( QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index ) const {
		int thumbnailSize = mView->thumbnailSize();
		ThumbnailView::Thumbnail thumbnail = mView->thumbnailForIndex(index);
		QPixmap thumbnailPix = thumbnail.mPixmap;
		if (thumbnailPix.width() > thumbnailSize || thumbnailPix.height() > thumbnailSize) {
			thumbnailPix = thumbnailPix.scaled(thumbnailSize, thumbnailSize, Qt::KeepAspectRatio);
		}
		QRect rect = option.rect;

#ifdef DEBUG_RECT
		painter->setPen(Qt::red);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(rect);
#endif

		// Crop text
		QString fullText = index.data(Qt::DisplayRole).toString();
		QString text;
		QMap<QString, QString>::const_iterator it = mElidedTextMap.find(fullText);
		if (it == mElidedTextMap.constEnd()) {
			text = elidedText(option.fontMetrics, rect.width() - 2*ITEM_MARGIN, Qt::ElideRight, fullText);
			mElidedTextMap[fullText] = text;
		} else {
			text = it.value();
		}

		int textWidth = option.fontMetrics.width(text);

		// Select color group
		QPalette::ColorGroup cg;

		if ( (option.state & QStyle::State_Enabled) && (option.state & QStyle::State_Active) ) {
			cg = QPalette::Normal;
		} else if ( (option.state & QStyle::State_Enabled)) {
			cg = QPalette::Inactive;
		} else {
			cg = QPalette::Disabled;
		}

		// Select colors
		QColor bgColor, borderColor, fgColor;
		if (option.state & QStyle::State_Selected) {
			bgColor = option.palette.color(cg, QPalette::Highlight);
			borderColor = bgColor.dark(SELECTION_BORDER_DARKNESS);
			fgColor = option.palette.color(cg, QPalette::HighlightedText);
		} else {
			QWidget* viewport = mView->viewport();
			bgColor = viewport->palette().color(viewport->backgroundRole());
			fgColor = viewport->palette().color(viewport->foregroundRole());

			if (bgColor.value() < 128) {
				borderColor = bgColor.dark(200);
			} else {
				borderColor = bgColor.light(200);
			}
		}

		// Draw background
		if (option.state & QStyle::State_Selected) {
			drawBackground(painter, rect, bgColor, borderColor);
		}

		// Draw thumbnail
		if (!thumbnailPix.isNull()) {
			QRect thumbnailRect = QRect(
				rect.left() + (rect.width() - thumbnailPix.width())/2,
				rect.top() + (thumbnailSize - thumbnailPix.height())/2 + ITEM_MARGIN,
				thumbnailPix.width(),
				thumbnailPix.height());

			if (!(option.state & QStyle::State_Selected) && thumbnail.mOpaque) {
				drawShadow(painter, thumbnailRect);
			}

			if (thumbnail.mOpaque) {
				painter->setPen(borderColor);
				painter->setRenderHint(QPainter::Antialiasing, false);
				QRect borderRect = thumbnailRect.adjusted(-1, -1, 0, 0);
				painter->drawRect(borderRect);
			}
			painter->drawPixmap(thumbnailRect.left(), thumbnailRect.top(), thumbnailPix);
		}

		// Draw modified indicator
		bool isModified = mView->isModified(index);
		if (isModified) {
			// Draws a pixmap of the save button frame, as an indicator that
			// the image has been modified
			QPoint framePosition = saveButtonFramePosition(rect);
			painter->drawPixmap(framePosition, mSaveButtonFramePixmap);
		}

		if (index == mIndexUnderCursor) {
			if (isModified) {
				// If we just rotated the image with the buttons from the
				// button frame, we need to show the save button frame right now.
				showSaveButtonFrame(rect);
			} else {
				mSaveButtonFrame->hide();
			}
		}

		// Draw text
		painter->setPen(fgColor);

		painter->drawText(
			rect.left() + (rect.width() - textWidth) / 2,
			rect.top() + ITEM_MARGIN + thumbnailSize + ITEM_MARGIN + option.fontMetrics.ascent(),
			text);
	}


	QModelIndex indexUnderCursor() const {
		return mIndexUnderCursor;
	}


	void updateButtonFrameOpacity() {
		bool isSelected = mView->selectionModel()->isSelected(mIndexUnderCursor);
		mButtonFrame->setOpaque(isSelected);
		mSaveButtonFrame->setOpaque(isSelected);
	}


private:
	QPoint saveButtonFramePosition(const QRect& itemRect) const {
		QSize frameSize = mSaveButtonFrame->sizeHint();
		int textHeight = mView->fontMetrics().height();
		int posX = itemRect.right() - GADGET_MARGIN - frameSize.width();
		int posY = itemRect.bottom() - GADGET_MARGIN - textHeight - frameSize.height();

		return QPoint(posX, posY);
	}


	void showSaveButtonFrame(const QRect& itemRect) const {
		mSaveButtonFrame->move(saveButtonFramePosition(itemRect));
		mSaveButtonFrame->show();
	}


	void drawBackground(QPainter* painter, const QRect& rect, const QColor& bgColor, const QColor& borderColor) const {
		painter->setRenderHint(QPainter::Antialiasing);

		QRectF rectF = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);

		QPainterPath path = PaintUtils::roundedRectangle(rectF, SELECTION_RADIUS);
		painter->fillPath(path, bgColor);
		painter->setPen(borderColor);
		painter->drawPath(path);
	}


	void drawShadow(QPainter* painter, const QRect& rect) const {
		const QPoint shadowOffset(-SHADOW_SIZE, -SHADOW_SIZE + 1);

		int key = rect.height() * 1000 + rect.width();

		ShadowCache::Iterator it = mShadowCache.find(key);
		if (it == mShadowCache.end()) {
			QSize size = QSize(rect.width() + 2*SHADOW_SIZE, rect.height() + 2*SHADOW_SIZE);
			QColor color(0, 0, 0, SHADOW_STRENGTH);
			QPixmap shadow = PaintUtils::generateFuzzyRect(size, color, SHADOW_SIZE);
			it = mShadowCache.insert(key, shadow);
		}
		painter->drawPixmap(rect.topLeft() + shadowOffset, it.value());
	}


	/**
	 * Show a tooltip only if the item has been elided.
	 * This function places the tooltip over the item text.
	 */
	void showToolTip(QAbstractItemView* view, QHelpEvent* helpEvent) {
		QModelIndex index = view->indexAt(helpEvent->pos());
		if (!index.isValid()) {
			return;
		}

		QString fullText = index.data().toString();
		QMap<QString, QString>::const_iterator it = mElidedTextMap.find(fullText);
		if (it == mElidedTextMap.constEnd()) {
			return;
		}
		QString elidedText = it.value();
		if (elidedText.length() == fullText.length()) {
			// text and tooltip are the same, don't show tooltip
			fullText = QString();
		}
		QRect rect = view->visualRect(index);
		QPoint pos(rect.left() + ITEM_MARGIN, rect.top() + mView->thumbnailSize() + ITEM_MARGIN);
		QToolTip::showText(view->mapToGlobal(pos), fullText, view);
		return;
	}


	/**
	 * Maps full text to elided text.
	 */
	mutable QMap<QString, QString> mElidedTextMap;

	// Key is height * 1000 + width
	typedef QMap<int, QPixmap> ShadowCache;
	mutable ShadowCache mShadowCache;

	ThumbnailView* mView;
	GlossyFrame* mButtonFrame;
	GlossyFrame* mSaveButtonFrame;
	QPixmap mSaveButtonFramePixmap;
	QModelIndex mIndexUnderCursor;
};


struct ThumbnailViewPrivate {
	int mThumbnailSize;
	PreviewItemDelegate* mItemDelegate;
	AbstractThumbnailViewHelper* mThumbnailViewHelper;
	QMap<QUrl, ThumbnailView::Thumbnail> mThumbnailForUrl;
	QMap<QUrl, QPersistentModelIndex> mPersistentIndexForUrl;
};


ThumbnailView::ThumbnailView(QWidget* parent)
: QListView(parent)
, d(new ThumbnailViewPrivate) {
	setViewMode(QListView::IconMode);
	setResizeMode(QListView::Adjust);
	setSpacing(SPACING);
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);

	d->mItemDelegate = new PreviewItemDelegate(this);
	setItemDelegate(d->mItemDelegate);
	viewport()->installEventFilter(d->mItemDelegate);

	viewport()->setMouseTracking(true);
	// Set this attribute, otherwise the item delegate won't get the
	// State_MouseOver state
	viewport()->setAttribute(Qt::WA_Hover);

	setVerticalScrollMode(ScrollPerPixel);
	setHorizontalScrollMode(ScrollPerPixel);

	d->mThumbnailViewHelper = 0;
	// Make sure mThumbnailSize is initialized before calling setThumbnailSize,
	// since it will compare the new size with the old one
	d->mThumbnailSize = 0;
	setThumbnailSize(128);

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),
		SLOT(showContextMenu()) );

	if (KGlobalSettings::singleClick()) {
		connect(this, SIGNAL(clicked(const QModelIndex&)),
			SIGNAL(indexActivated(const QModelIndex&)) );
	} else {
		connect(this, SIGNAL(doubleClicked(const QModelIndex&)),
			SIGNAL(indexActivated(const QModelIndex&)) );
	}
}


ThumbnailView::~ThumbnailView() {
	delete d;
}


void ThumbnailView::setThumbnailSize(int value) {
	if (d->mThumbnailSize == value) {
		return;
	}
	d->mThumbnailSize = value;
	d->mItemDelegate->clearElidedTextMap();
	setSpacing(SPACING);
}


int ThumbnailView::thumbnailSize() const {
	return d->mThumbnailSize;
}

int ThumbnailView::itemWidth() const {
	return d->mThumbnailSize + 2 * ITEM_MARGIN;
}

int ThumbnailView::itemHeight() const {
	return d->mThumbnailSize + fontMetrics().height() + 3*ITEM_MARGIN;
}

void ThumbnailView::setThumbnailViewHelper(AbstractThumbnailViewHelper* helper) {
	d->mThumbnailViewHelper = helper;
	connect(helper, SIGNAL(thumbnailLoaded(const KFileItem&, const QPixmap&)),
		SLOT(setThumbnail(const KFileItem&, const QPixmap&)) );
}

AbstractThumbnailViewHelper* ThumbnailView::thumbnailViewHelper() const {
	return d->mThumbnailViewHelper;
}


void ThumbnailView::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) {
	QListView::rowsAboutToBeRemoved(parent, start, end);

	KFileItemList itemList;
	for (int pos=start; pos<=end; ++pos) {
		QModelIndex index = model()->index(pos, 0, parent);

		QVariant data = index.data(KDirModel::FileItemRole);
		KFileItem item = qvariant_cast<KFileItem>(data);

		QUrl url = item.url();
		d->mThumbnailForUrl.remove(url);
		d->mPersistentIndexForUrl.remove(url);

		itemList.append(item);
	}

	Q_ASSERT(d->mThumbnailViewHelper);
	d->mThumbnailViewHelper->abortThumbnailGenerationForItems(itemList);
}


void ThumbnailView::showContextMenu() {
	d->mThumbnailViewHelper->showContextMenu(this);
}


void ThumbnailView::setThumbnail(const KFileItem& item, const QPixmap& pixmap) {
	QUrl url = item.url();
	QPersistentModelIndex persistentIndex = d->mPersistentIndexForUrl[url];
	if (!persistentIndex.isValid()) {
		return;
	}

	// Alpha check

	d->mThumbnailForUrl[url] = Thumbnail(pixmap);

	QRect rect = visualRect(persistentIndex);
	update(rect);
	viewport()->update(rect);
}


ThumbnailView::Thumbnail ThumbnailView::thumbnailForIndex(const QModelIndex& index) {
	QVariant data = index.data(KDirModel::FileItemRole);
	KFileItem item = qvariant_cast<KFileItem>(data);

	QUrl url = item.url();
	QMap<QUrl, Thumbnail>::ConstIterator it = d->mThumbnailForUrl.find(url);
	if (it != d->mThumbnailForUrl.constEnd()) {
		return it.value();
	}

	if (ArchiveUtils::fileItemIsDirOrArchive(item)) {
		return Thumbnail(item.pixmap(128));
	}

	KFileItemList list;
	list << item;
	d->mPersistentIndexForUrl[url] = QPersistentModelIndex(index);
	d->mThumbnailViewHelper->generateThumbnailsForItems(list);
	return Thumbnail(QPixmap());
}


bool ThumbnailView::isModified(const QModelIndex& index) const {
	KUrl url = urlForIndex(index);
	return d->mThumbnailViewHelper->isDocumentModified(url);
}


void ThumbnailView::slotSaveClicked() {
	QModelIndex index = d->mItemDelegate->indexUnderCursor();
	saveDocumentRequested(urlForIndex(index));
}


void ThumbnailView::slotRotateLeftClicked() {
	QModelIndex index = d->mItemDelegate->indexUnderCursor();
	rotateDocumentLeftRequested(urlForIndex(index));
}


void ThumbnailView::slotRotateRightClicked() {
	QModelIndex index = d->mItemDelegate->indexUnderCursor();
	rotateDocumentRightRequested(urlForIndex(index));
}


void ThumbnailView::slotFullScreenClicked() {
	QModelIndex index = d->mItemDelegate->indexUnderCursor();
	showDocumentInFullScreenRequested(urlForIndex(index));
}


void ThumbnailView::dragEnterEvent(QDragEnterEvent* event) {
	if (event->mimeData()->hasUrls()) {
		event->acceptProposedAction();
	}
}


void ThumbnailView::dragMoveEvent(QDragMoveEvent* event) {
	// Necessary, otherwise we don't reach dropEvent()
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


void ThumbnailView::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
	QListView::selectionChanged(selected, deselected);
	d->mItemDelegate->updateButtonFrameOpacity();
}


} // namespace