// vim: set tabstop=4 shiftwidth=4 noexpandtab:
/*
Gwenview: an image viewer
Copyright 2009 Aurélien Gâteau <agateau@kde.org>

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
#include "progresspage.h"

// Local
#include <ui_progresspage.h>
#include "importer.h"

namespace Gwenview {

struct ProgressPagePrivate : public Ui_ProgressPage {
	ProgressPage* q;
	Importer* mImporter;
};

ProgressPage::ProgressPage(Importer* importer)
: d(new ProgressPagePrivate) {
	d->q = this;
	d->mImporter = importer;
	d->setupUi(this);

	connect(d->mImporter, SIGNAL(progressChanged(int)),
		d->mProgressBar, SLOT(setValue(int)));
	connect(d->mImporter, SIGNAL(maximumChanged(int)),
		d->mProgressBar, SLOT(setMaximum(int)));
}

ProgressPage::~ProgressPage() {
	delete d;
}

} // namespace
