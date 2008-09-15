// vim: set tabstop=4 shiftwidth=4 noexpandtab:
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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/
#ifndef REDEYEREDUCTIONSIDEBAR_H
#define REDEYEREDUCTIONSIDEBAR_H

#include <lib/gwenviewlib_export.h>

// Qt
#include <QWidget>

// KDE

// Local
#include <lib/document/document.h>

namespace Gwenview {

class AbstractImageOperation;
class ImageView;

class RedEyeReductionSideBarPrivate;
class GWENVIEWLIB_EXPORT RedEyeReductionSideBar : public QWidget {
	Q_OBJECT
public:
	RedEyeReductionSideBar(QWidget* parent, ImageView*, Document::Ptr);
	~RedEyeReductionSideBar();

Q_SIGNALS:
	void done();
	void imageOperationRequested(AbstractImageOperation*);

private Q_SLOTS:
	void slotAccepted();

private:
	RedEyeReductionSideBarPrivate* const d;
};


} // namespace

#endif /* REDEYEREDUCTIONSIDEBAR_H */