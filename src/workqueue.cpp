#include "workqueue.h"

WorkQueue::WorkQueue(QObject *parent) : QObject(parent) {
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(60000);
}
