#ifndef PHOTOS_H
#define PHOTOS_H

#include "globals.h"

void showPhoto(int index);
void showPhotoById(int id);
void showPhotoFromCenterById(int id);
void showPhotoIndex(int index);
void showPhotoId(int id);
void onReceiveNewPic(int id);
void queueNewPhoto(int id); // llamable desde la tarea de red (core 0)
bool hasQueuedNewPhotos();
void processPendingPhoto();
void displayPhotoWithFade();
void displayPhotoFromCenter();
void loadNewPhotoStyle();
void setNewPhotoStyle(const char *name); // llamable desde la tarea de red (core 0)
void showPhotoInfo(String title, String name);
void updatePhotoInfo();
void startAnimationDownloadIfNeeded();
void pumpAnimationRequests();
bool startAnimationPlaybackIfReady(); // true = hubo swap+fade (iteracion larga esperada)
void updateAnimationPlayback();
void checkAnimationDownloadTimeout();
bool animPrefetchDue();
void stopPlayback();
void resetAnimationDownloadState(bool freeBuffer);
void stopAnimation();

#endif
