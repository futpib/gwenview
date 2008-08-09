// vim: set tabstop=4 shiftwidth=4 noexpandtab:
/*
Gwenview: an image viewer
Copyright 2008 Aurélien Gâteau <aurelien.gateau@free.fr>

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Cambridge, MA 02110-1301, USA.

*/
#ifndef DOCUMENTVIEW_H
#define DOCUMENTVIEW_H

#include <lib/gwenviewlib_export.h>

// Qt
#include <QWidget>

// KDE
#include <kactioncollection.h>

// Local

class KUrl;

namespace Gwenview {

class AbstractDocumentViewAdapter;
class ZoomWidget;

class DocumentViewPrivate;

/**
 * This widget can display various documents, using an instance of
 * AbstractDocumentViewAdapter
 */
class GWENVIEWLIB_EXPORT DocumentView : public QWidget {
	Q_OBJECT
public:
	DocumentView(QWidget* parent, KActionCollection*);
	~DocumentView();

	AbstractDocumentViewAdapter* adapter() const;

	ZoomWidget* zoomWidget() const;

	bool openUrl(const KUrl&);

	void reset();

	bool isEmpty() const;

Q_SIGNALS:
	/**
	 * Emitted when the part has finished loading
	 */
	void completed();

	void resizeRequested(const QSize&);

	void previousImageRequested();

	void nextImageRequested();

	void enterFullScreenRequested();

	void captionUpdateRequested(const QString&);

protected:
	virtual bool eventFilter(QObject*, QEvent* event);

private Q_SLOTS:
	void slotCompleted();

	void setZoomToFit(bool);

	void zoomActualSize();

	void zoomIn(const QPoint& center = QPoint(-1,-1));
	void zoomOut(const QPoint& center = QPoint(-1,-1));

	void slotZoomChanged(qreal);
	void slotZoomWidgetChanged(qreal);


private:
	friend class DocumentViewPrivate;
	DocumentViewPrivate* const d;

	void createAdapterForUrl(const KUrl&);
};


} // namespace

#endif /* DOCUMENTVIEW_H */