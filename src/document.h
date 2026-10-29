#pragma once
#include "mapio.h"
#include <QObject>
#include <QUndoStack>
#include "workqueue.h"

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr, bool allowRaw = false);
    const MapIO::Map &map() const { return map_; }
    const QImage &image() const { return map_.image; }
    const MapIO::Metadata &meta() const { return map_.meta; }
    QUndoStack *undo() { return &undo_; }
    bool dirty() const { return !undo_.isClean() || needsPair_ || historyBaselineDirty_; }
    bool empty() const { return map_.image.isNull(); }
    void open(const QString &path);
    void newMap(int width, int height, double resolution);
    void replace(MapIO::Map map, bool needsSave);
    void clear();
    void setWorkQueue(WorkQueue *queue) { worker_ = queue; }
    bool undoChangesGeometry() const;
    bool redoChangesGeometry() const;
    void editImage(const QImage &image, const QString &label, const QRect &changedArea = {});
    void editMetadata(const MapIO::Metadata &meta);
    void rotateImage(double clockwiseDegrees);
    void cropImage(const QRect &area);
    QRect knownBounds() const;
    MapIO::SaveResult save(const QString &yamlPath = {});
signals:
    void changed();
    void loaded();
private:
    friend class PixelCommand;
    friend class MetadataCommand;
    friend class RotateCommand;
    void refreshDiskHashes();
    void push(QUndoCommand *command, qsizetype bytes);
    MapIO::Map map_;
    QUndoStack undo_;
    bool needsPair_ = false;
    bool historyBaselineDirty_ = false;
    qsizetype historyBytes_ = 0;
    QByteArray yamlHash_, imageHash_;
    bool allowRaw_ = false;
    WorkQueue *worker_ = nullptr;
};
