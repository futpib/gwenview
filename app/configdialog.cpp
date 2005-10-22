// vim: set tabstop=4 shiftwidth=4 noexpandtab
// kate: indent-mode csands; indent-width 4; replace-tabs-save off; replace-tabs off; replace-trailing-space-save off; space-indent off; tabs-indents on; tab-width 4;
/*
Gwenview - A simple image viewer for KDE
Copyright 2000-2004 Aur�ien G�eau

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// Qt 
#include <qbuttongroup.h>
#include <qcheckbox.h>
#include <qlayout.h>
#include <qlineedit.h>
#include <qmap.h>
#include <qradiobutton.h>
#include <qspinbox.h>
#include <qstylesheet.h>

// KDE
#include <kcolorbutton.h>
#include <kconfigdialogmanager.h>
#include <kdeversion.h>
#include <kdirsize.h>
#include <kfiledialog.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kio/netaccess.h>
#include <kmessagebox.h>
#include <kurlrequester.h>

#include <config.h>
// KIPI
#ifdef GV_HAVE_KIPI
#include <libkipi/pluginloader.h>
#endif

// Local 
#include "configfileoperationspage.h"
#include "configfullscreenpage.h"
#include "configimagelistpage.h"
#include "configimageviewpage.h"
#include "configmiscpage.h"
#include "configslideshowpage.h"
#include "doublespinbox.h"
#include "mainwindow.h"
#include "gvcore/fileoperation.h"
#include "gvcore/filethumbnailview.h"
#include "gvcore/fileviewstack.h"
// This path is different because it's a generated file, so it's stored in builddir
#include <../gvcore/miscconfig.h>
#include <../gvcore/slideshowconfig.h>
#include <../gvcore/fileoperationconfig.h>
#include "gvcore/imageview.h"
#include "gvcore/thumbnailloadjob.h"

#include "configdialog.moc"
namespace Gwenview {

typedef QValueList<KConfigDialogManager*> ConfigManagerList;

class ConfigDialogPrivate {
public:
	ConfigImageViewPage* mImageViewPage;
	ConfigImageListPage* mImageListPage;
	ConfigFullScreenPage* mFullScreenPage;
	ConfigFileOperationsPage* mFileOperationsPage;
	ConfigMiscPage* mMiscPage;
	ConfigSlideshowPage* mSlideShowPage;
	MainWindow* mMainWindow;
#ifdef GV_HAVE_KIPI
	KIPI::ConfigWidget* mKIPIConfigWidget;
#endif

	ConfigManagerList mManagers;
};


// Two helper functions to create the config pages
template<class T>
void addConfigPage(KDialogBase* dialog, T* content, const QString& header, const QString& name, const char* iconName) {
	QFrame* page=dialog->addPage(name, header, BarIcon(iconName, 32));
	content->reparent(page, QPoint(0,0));
	QVBoxLayout* layout=new QVBoxLayout(page, 0, KDialog::spacingHint());
	layout->addWidget(content);
	layout->addStretch();
}

template<class T>
T* addConfigPage(KDialogBase* dialog, const QString& header, const QString& name, const char* iconName) {
	T* content=new T;
	addConfigPage(dialog, content, header, name, iconName);
	return content;
}


ConfigDialog::ConfigDialog(MainWindow* mainWindow)
: KDialogBase(
	KDialogBase::IconList,
	i18n("Configure"),
	KDialogBase::Ok | KDialogBase::Cancel | KDialogBase::Apply,
	KDialogBase::Ok,
	mainWindow,
	"ConfigDialog",
	true,
	true)
{
	d=new ConfigDialogPrivate;
	d->mMainWindow=mainWindow;

	// Create dialog pages
	d->mImageListPage = addConfigPage<ConfigImageListPage>(
		this, i18n("Configure Image List"), i18n("Image List"), "view_icon");
	
	d->mImageViewPage = addConfigPage<ConfigImageViewPage>(
		this, i18n("Configure Image View"), i18n("Image View"), "looknfeel");
	
	d->mFullScreenPage = addConfigPage<ConfigFullScreenPage>(
		this, i18n("Configure Full Screen Mode"), i18n("Full Screen"), "window_fullscreen");

	d->mFileOperationsPage = addConfigPage<ConfigFileOperationsPage>(
		this, i18n("Configure File Operations"), i18n("File Operations"), "folder");
	d->mManagers << new KConfigDialogManager(d->mFileOperationsPage, FileOperationConfig::self());

	d->mSlideShowPage = addConfigPage<ConfigSlideshowPage>(
		this, i18n("SlideShow"), i18n("SlideShow"), "slideshow");
	d->mManagers << new KConfigDialogManager(d->mSlideShowPage, SlideShowConfig::self());

#ifdef GV_HAVE_KIPI
	d->mKIPIConfigWidget = mainWindow->pluginLoader()->configWidget(this);
	addConfigPage(
		this, d->mKIPIConfigWidget, i18n("Configure KIPI Plugins"), i18n("KIPI Plugins"), "kipi");
#endif

	d->mMiscPage = addConfigPage<ConfigMiscPage>(
		this, i18n("Miscellaneous Settings"), i18n("Misc"), "gear");
	d->mManagers << new KConfigDialogManager(d->mMiscPage, MiscConfig::self());
	// Read config, because the modified behavior might have changed
	MiscConfig::self()->readConfig();

	FileViewStack* fileViewStack=d->mMainWindow->fileViewStack();
	ImageView* imageView=d->mMainWindow->imageView();

	// Image List tab
	d->mImageListPage->mThumbnailMargin->setValue(fileViewStack->fileThumbnailView()->marginSize());
	d->mImageListPage->mShowDirs->setChecked(fileViewStack->showDirs());
	d->mImageListPage->mShownColor->setColor(fileViewStack->shownColor());
	d->mImageListPage->mStoreThumbnailsInCache->setChecked(ThumbnailLoadJob::storeThumbnailsInCache());
	d->mImageListPage->mAutoDeleteThumbnailCache->setChecked(d->mMainWindow->showAutoDeleteThumbnailCache());
	
	int details=fileViewStack->fileThumbnailView()->itemDetails();
	d->mImageListPage->mShowFileName->setChecked(details & FileThumbnailView::FILENAME);
	d->mImageListPage->mShowFileDate->setChecked(details & FileThumbnailView::FILEDATE);
	d->mImageListPage->mShowFileSize->setChecked(details & FileThumbnailView::FILESIZE);
	d->mImageListPage->mShowImageSize->setChecked(details & FileThumbnailView::IMAGESIZE);

	connect(d->mImageListPage->mCalculateCacheSize,SIGNAL(clicked()),
		this,SLOT(calculateCacheSize()));
	connect(d->mImageListPage->mEmptyCache,SIGNAL(clicked()),
		this,SLOT(emptyCache()));

	// Image View tab
	d->mImageViewPage->mSmoothGroup->setButton(imageView->smoothAlgorithm());
	d->mImageViewPage->mDelayedSmoothing->setChecked(imageView->delayedSmoothing());
	d->mImageViewPage->mBackgroundColor->setColor(imageView->normalBackgroundColor());
	d->mImageViewPage->mAutoZoomEnlarge->setChecked(imageView->enlargeSmallImages());
	d->mImageViewPage->mShowScrollBars->setChecked(imageView->showScrollBars());
	d->mImageViewPage->mMouseWheelGroup->setButton(imageView->mouseWheelScroll()?1:0);

    // Slide Show tab
	d->mSlideShowPage->kcfg_delay->setMaxValue( DoubleSpinBox::doubleToInt(10000.) );
	d->mSlideShowPage->kcfg_delay->setLineStep( DoubleSpinBox::doubleToInt(1.) );

	// Full Screen tab
	d->mFullScreenPage->mOSDModeGroup->setButton(imageView->osdMode());
	d->mFullScreenPage->mFreeOutputFormat->setText(imageView->freeOutputFormat());
	d->mFullScreenPage->mShowBusyPtrInFullScreen->setChecked(d->mMainWindow->showBusyPtrInFullScreen());

	// File Operations tab
	d->mFileOperationsPage->kcfg_destDir->fileDialog()->setMode(
		static_cast<KFile::Mode>(KFile::Directory | KFile::ExistingOnly | KFile::LocalOnly));

	d->mFileOperationsPage->mDeleteGroup->setButton(FileOperationConfig::self()->deleteToTrash()?1:0);
	
	ConfigManagerList::Iterator it(d->mManagers.begin());
	for (;it!=d->mManagers.end(); ++it) {
		(*it)->updateWidgets();
		connect(*it, SIGNAL(settingsChanged()),
			this, SIGNAL(settingsChanged()) );
	}
}



ConfigDialog::~ConfigDialog() {
	delete d;
}


void ConfigDialog::slotOk() {
	slotApply();
	accept();
}


void ConfigDialog::slotApply() {
	FileViewStack* fileViewStack=d->mMainWindow->fileViewStack();
	ImageView* imageView=d->mMainWindow->imageView();

	// Image List tab
	fileViewStack->fileThumbnailView()->setMarginSize(d->mImageListPage->mThumbnailMargin->value());
	fileViewStack->fileThumbnailView()->arrangeItemsInGrid();
	fileViewStack->setShowDirs(d->mImageListPage->mShowDirs->isChecked());
	fileViewStack->setShownColor(d->mImageListPage->mShownColor->color());
	ThumbnailLoadJob::setStoreThumbnailsInCache(d->mImageListPage->mStoreThumbnailsInCache->isChecked());
	d->mMainWindow->setAutoDeleteThumbnailCache(d->mImageListPage->mAutoDeleteThumbnailCache->isChecked());
	
	int details=
		(d->mImageListPage->mShowFileName->isChecked() ? FileThumbnailView::FILENAME : 0)
		| (d->mImageListPage->mShowFileDate->isChecked() ? FileThumbnailView::FILEDATE : 0)
		| (d->mImageListPage->mShowFileSize->isChecked() ? FileThumbnailView::FILESIZE : 0)
		| (d->mImageListPage->mShowImageSize->isChecked() ? FileThumbnailView::IMAGESIZE : 0)
		;
	fileViewStack->fileThumbnailView()->setItemDetails(details);
	
	// Image View tab
	int algo=d->mImageViewPage->mSmoothGroup->selectedId();
	
	imageView->setSmoothAlgorithm( static_cast<ImageUtils::SmoothAlgorithm>(algo));
	imageView->setNormalBackgroundColor(d->mImageViewPage->mBackgroundColor->color());
	imageView->setDelayedSmoothing(d->mImageViewPage->mDelayedSmoothing->isChecked());
	imageView->setEnlargeSmallImages(d->mImageViewPage->mAutoZoomEnlarge->isChecked());
	imageView->setShowScrollBars(d->mImageViewPage->mShowScrollBars->isChecked());
	imageView->setMouseWheelScroll(d->mImageViewPage->mMouseWheelGroup->selected()==d->mImageViewPage->mMouseWheelScroll);

	// Full Screen tab
	int osdMode=d->mFullScreenPage->mOSDModeGroup->selectedId();
	imageView->setOSDMode( static_cast<ImageView::OSDMode>(osdMode) );
	imageView->setFreeOutputFormat( d->mFullScreenPage->mFreeOutputFormat->text() );
	d->mMainWindow->setShowBusyPtrInFullScreen(d->mFullScreenPage->mShowBusyPtrInFullScreen->isChecked() );

	// File Operations tab
	FileOperationConfig::self()->setDeleteToTrash(
		d->mFileOperationsPage->mDeleteGroup->selected()==d->mFileOperationsPage->mDeleteToTrash);

	// KIPI tab
#ifdef GV_HAVE_KIPI
	d->mKIPIConfigWidget->apply();
#endif
	
	ConfigManagerList::Iterator it(d->mManagers.begin());
	for (;it!=d->mManagers.end(); ++it) {
		(*it)->updateSettings();
	}
}


void ConfigDialog::calculateCacheSize() {
	KURL url;
	url.setPath(ThumbnailLoadJob::thumbnailBaseDir());
	unsigned long size=KDirSize::dirSize(url);
	KMessageBox::information( this,i18n("Cache size is %1").arg(KIO::convertSize(size)) );
}


void ConfigDialog::emptyCache() {
	QString dir=ThumbnailLoadJob::thumbnailBaseDir();

	if (!QFile::exists(dir)) {
		KMessageBox::information( this,i18n("Cache is already empty.") );
		return;
	}

	int response=KMessageBox::questionYesNo(this,
		"<qt>" + i18n("Are you sure you want to empty the thumbnail cache?"
		" This will remove the folder <b>%1</b>.").arg(QStyleSheet::escape(dir)) + "</qt>");

	if (response==KMessageBox::No) return;

	KURL url;
	url.setPath(dir);
	if (KIO::NetAccess::del(url, topLevelWidget()) ) {
		KMessageBox::information( this,i18n("Cache emptied.") );
	}
}


void ConfigDialog::onCacheEmptied(KIO::Job* job) {
	if ( job->error() ) {
		job->showErrorDialog(this);
		return;
	}
	KMessageBox::information( this,i18n("Cache emptied.") );
}

} // namespace
