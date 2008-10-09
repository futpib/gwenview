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
#ifndef TAGMODEL_H
#define TAGMODEL_H

#include <lib/gwenviewlib_export.h>

// Qt
#include <QStandardItemModel>

// KDE

// Local

namespace Gwenview {

typedef QString SemanticInfoTag;

class AbstractSemanticInfoBackEnd;


class TagModelPrivate;
class GWENVIEWLIB_EXPORT TagModel : public QStandardItemModel {
	Q_OBJECT
public:
	TagModel(QObject*, AbstractSemanticInfoBackEnd*);
	~TagModel();

	enum {
		TagRole = Qt::UserRole
	};

private Q_SLOTS:
	void refresh();
	void slotTagAdded(const SemanticInfoTag& tag, const QString& label);

private:
	TagModelPrivate* const d;
};


} // namespace

#endif /* TAGMODEL_H */
