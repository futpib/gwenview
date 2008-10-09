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
// Self
#include "tagmodel.moc"

// Qt

// KDE

// Local
#include "abstractsemanticinfobackend.h"

namespace Gwenview {


struct TagModelPrivate {
	AbstractSemanticInfoBackEnd* mBackEnd;
};


TagModel::TagModel(QObject* parent, AbstractSemanticInfoBackEnd* backEnd)
: QStandardItemModel(parent)
, d(new TagModelPrivate) {
	d->mBackEnd = backEnd;
	refresh();
	connect(d->mBackEnd, SIGNAL(tagAdded(const SemanticInfoTag&, const QString&)),
		SLOT(slotTagAdded(const SemanticInfoTag&, const QString&)));
}


void TagModel::refresh() {
	d->mBackEnd->refreshAllTags();
	TagSet set = d->mBackEnd->allTags();

	clear();
	Q_FOREACH(const SemanticInfoTag& tag, set) {
		QString label = d->mBackEnd->labelForTag(tag);
		QStandardItem* item = new QStandardItem(label);
		item->setData(tag, TagRole);
		appendRow(item);
	}
	sort(0);
}


void TagModel::slotTagAdded(const SemanticInfoTag& tag, const QString& label) {
	int row;
	// This is not optimal, implement dichotomic search if necessary
	for (row=0; row < rowCount(); ++row) {
		const QModelIndex idx = index(row, 0);
		if (idx.data().toString().compare(label) > 0) {
			break;
		}
	}
	QStandardItem* item = new QStandardItem(label);
	item->setData(tag, TagRole);
	insertRow(row, item);
}


TagModel::~TagModel() {
	delete d;
}


} // namespace
