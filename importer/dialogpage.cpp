// vim: set tabstop=4 shiftwidth=4 expandtab:
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
#include "dialogpage.h"

// Qt
#include <QEventLoop>
#include <QList>
#include <QVBoxLayout>
#include <QPushButton>

// KDE
#include <KGuiItem>

// Local
#include <ui_dialogpage.h>

namespace Gwenview
{

struct DialogPagePrivate : public Ui_DialogPage
{
    QVBoxLayout* mLayout;
    QList<QPushButton*> mButtons;
    QEventLoop* mEventLoop;
};

DialogPage::DialogPage(QWidget* parent)
: QWidget(parent)
, d(new DialogPagePrivate)
{
    d->setupUi(this);
    d->mLayout = new QVBoxLayout(d->mButtonContainer);
}

DialogPage::~DialogPage()
{
    delete d;
}

void DialogPage::removeButtons()
{
    qDeleteAll(d->mButtons);
    d->mButtons.clear();
}

void DialogPage::setText(const QString& text)
{
    d->mLabel->setText(text);
}

int DialogPage::addButton(const KGuiItem& item)
{
    int id = d->mButtons.size();
    QPushButton* button = new QPushButton;
    KGuiItem::assign(button, item);
    button->setFixedHeight(button->sizeHint().height() * 2);

    connect(button, &QAbstractButton::clicked, this, [this, id]() {
        d->mEventLoop->exit(id);
    });
    d->mLayout->addWidget(button);
    d->mButtons << button;
    return id;
}

int DialogPage::exec()
{
    QEventLoop loop;
    d->mEventLoop = &loop;
    return loop.exec();
}

} // namespace
