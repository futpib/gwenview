// vim: set tabstop=4 shiftwidth=4 noexpandtab:
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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/
#ifndef SCROLLTOOL_H
#define SCROLLTOOL_H

#include <lib/gwenviewlib_export.h>

// Qt

// KDE

// Local
#include <lib/abstractimageviewtool.h>

class QPoint;

namespace Gwenview {


struct ScrollToolPrivate;
class GWENVIEWLIB_EXPORT ScrollTool : public AbstractImageViewTool {
	Q_OBJECT
public:
	ScrollTool(ImageView* view);
	~ScrollTool();

	virtual void mousePressEvent(QMouseEvent*);
	virtual void mouseMoveEvent(QMouseEvent*);
	virtual void mouseReleaseEvent(QMouseEvent*);
	virtual void wheelEvent(QWheelEvent*);

	virtual void toolActivated();
	virtual void toolDeactivated();

Q_SIGNALS:
	void previousImageRequested();
	void nextImageRequested();

private:
	ScrollToolPrivate* const d;
};


} // namespace

#endif /* SCROLLTOOL_H */
