#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QImage;

struct BoardItem
{
    QString id;
    QString type;   // "note", "image" or "video"
    double x = 0;
    double y = 0;
    double w = 320;
    double h = 220;
    int z = 0;
    QString color;  // palette key, notes only
    QString text;   // Markdown, notes only
    QString source; // project-relative path, media only
};

// The items placed on the canvas of the currently open project.
//
// A project is a folder:
//   canvas.json      layout of every item plus the last view
//   notes/<id>.md    one Markdown document per note
//   assets/          images and videos
class BoardModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Board)
    QML_SINGLETON

    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QUrl projectUrl READ projectUrl NOTIFY projectChanged)
    Q_PROPERTY(QString assetsDir READ assetsDir NOTIFY projectChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool hasView READ hasView NOTIFY viewRestored)
    Q_PROPERTY(double viewX READ viewX NOTIFY viewRestored)
    Q_PROPERTY(double viewY READ viewY NOTIFY viewRestored)
    Q_PROPERTY(double viewZoom READ viewZoom NOTIFY viewRestored)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        PosXRole,
        PosYRole,
        WidthRole,
        HeightRole,
        ZRole,
        ColorRole,
        TextRole,
        SourceRole,
        TitleRole,
        SearchRole,
    };
    Q_ENUM(Roles)

    explicit BoardModel(QObject *parent = nullptr);
    ~BoardModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString projectPath() const { return m_projectPath; }
    QUrl projectUrl() const;
    QString assetsDir() const;
    int count() const { return m_items.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool hasView() const { return m_hasView; }
    double viewX() const { return m_viewX; }
    double viewY() const { return m_viewY; }
    double viewZoom() const { return m_viewZoom; }

    // Project lifecycle
    Q_INVOKABLE void load(const QString &projectDir);
    Q_INVOKABLE void close();
    Q_INVOKABLE void save();

    // Creating items. Media positions are the centre of the new item.
    Q_INVOKABLE int addNote(double x, double y, const QString &text = {});
    // Media always goes into a note: into targetRow if that is a note,
    // otherwise into a new one centred on (cx, cy). Each returns the note's
    // row, or -1 if there was nothing to add.
    Q_INVOKABLE int addMedia(const QVariantList &paths, double cx, double cy, int targetRow = -1);
    Q_INVOKABLE int importUrls(const QVariantList &urls, double cx, double cy, int targetRow = -1);
    Q_INVOKABLE int addImageData(const QByteArray &data, double cx, double cy, int targetRow = -1);
    Q_INVOKABLE int pasteFromClipboard(double cx, double cy, int targetRow = -1);

    // Editing items
    Q_INVOKABLE void setGeometry(int row, double x, double y, double w, double h);
    Q_INVOKABLE void setText(int row, const QString &text);
    Q_INVOKABLE void setColor(int row, const QString &color);
    Q_INVOKABLE void raise(int row);
    Q_INVOKABLE void lower(int row);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE int duplicate(int row);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Queries
    Q_INVOKABLE int indexOf(const QString &id) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QRectF itemsBounds() const;
    Q_INVOKABLE int itemAt(double x, double y) const; // topmost, or -1

    // Clipboard
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE QString saveClipboardImage();
    Q_INVOKABLE QStringList clipboardMediaFiles() const;
    Q_INVOKABLE void copyToClipboard(int row) const;

    // Assets. mediaInfo copies an image or video into the project (if it is
    // not there yet) and returns rel, type ("image"/"video"), width, height.
    Q_INVOKABLE QVariantMap mediaInfo(const QString &pathOrUrl);
    Q_INVOKABLE bool isMediaFile(const QString &pathOrUrl) const;

    // A note for display: a list of {kind: "text", html} with images
    // scaled to maxWidth, and {kind: "video", url, name} for each video
    // that stands on its own line. Occurrences of the highlight terms
    // (search matches) get highlightColor behind them.
    Q_INVOKABLE QVariantList renderSegments(const QString &markdown, double maxWidth, int pixelSize,
                                            const QColor &linkColor, const QStringList &highlight = {},
                                            const QColor &highlightColor = {}) const;
    Q_INVOKABLE QString toLocalPath(const QString &pathOrUrl) const;
    Q_INVOKABLE QString exportMarkdown();

    // Remembered view: world point at the centre of the viewport plus zoom
    Q_INVOKABLE void setView(double centerX, double centerY, double zoom);

signals:
    void projectChanged();
    void countChanged();
    void undoStateChanged();
    void viewRestored();

public:
    // For the search, which reads the open project from memory: what is
    // on disk may be up to a save behind
    const QList<BoardItem> &items() const { return m_items; }
    static QString titleFor(const BoardItem &item);
    static QString titleForText(const QString &markdown);

private:
    bool valid(int row) const { return row >= 0 && row < m_items.size(); }
    void emitRow(int row);
    void markDirty();
    void pushUndo(const QString &coalesceKey = {});
    void restore(QList<BoardItem> items);
    int appendItem(const BoardItem &item);
    QStringList importMedia(const QStringList &paths);
    int placeMediaNote(const QStringList &rels, double cx, double cy);
    void appendMediaToNote(int row, const QStringList &rels);
    double mediaHeight(const QString &rel, double width) const;
    static QString mediaMarkdown(const QString &rel);
    QString resolveLocal(const QString &target) const;
    QString renderHtml(const QString &markdown, double maxWidth, int pixelSize, const QColor &linkColor,
                       bool forClipboard, const QStringList &highlight = {},
                       const QColor &highlightColor = {}) const;
    QString dataUri(const QString &path) const;
    BoardItem makeNote(double x, double y, const QString &text) const;
    int topZ() const;
    int bottomZ() const;
    QString absPath(const QString &rel) const;
    QString importAsset(const QString &absPath);
    QString uniqueAssetPath(const QString &base, const QString &extension) const;
    QString saveImageAsset(const QImage &image, const QString &prefix);
    QSizeF displaySize(const QString &absPath, const QString &type) const;
    void adoptLooseNotes();
    static QString mediaType(const QString &path);

    QString m_projectPath;
    QList<BoardItem> m_items;
    QList<QList<BoardItem>> m_undo;
    QList<QList<BoardItem>> m_redo;
    QString m_lastUndoKey;
    QHash<QString, QString> m_savedTexts; // note id -> text last written to disk
    bool m_dirty = false;
    bool m_hasView = false;
    double m_viewX = 0;
    double m_viewY = 0;
    double m_viewZoom = 1;
    QTimer m_saveTimer;
    mutable QHash<QString, QSizeF> m_imageSizes;
};
