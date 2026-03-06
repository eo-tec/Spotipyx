#ifndef PHOTOS_H
#define PHOTOS_H

#include "globals.h"

void showPhoto(int index);
void showPhotoById(int id);
void showPhotoFromCenterById(int id);
void showPhotoIndex(int index);
void showPhotoId(int id);
void onReceiveNewPic(int id);
void processPendingPhoto();
void displayPhotoWithFade();
void displayPhotoFromCenter();
void showPhotoInfo(String title, String name);
void updatePhotoInfo();
void startAnimationDownloadIfNeeded();
void updateAnimationPlayback();
void stopAnimation();

#endif
