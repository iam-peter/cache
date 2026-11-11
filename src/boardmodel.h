#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QImage;
class QJsonObject;

// One thing on the canvas.
struct BoardItem
{
    QString id;
    // "note"    Markdown document, kept in notes/<id>.md
    // "section" a labelled area that groups what lies inside it; text is
    //           its title
    // "shape"   rect, ellipse, diamond or triangle (shape) with a label
    // "text"    a free text label; size is its font size
    // "stroke"  a pen or highlighter line (shape); points are relative to
    //           x/y, size is the line width
    QString type;
    double x = 0;
    double y = 0;
    double w = 320;
    double h = 220;
    int z = 0;
    QString color;  // palette key
    QString text;
    QString source; // project-relative path; only in files of old versions
    QString shape;
    QList<QPointF> points;
    double size = 0;
    bool collapsed = false; // a mindmap branch folded into its root
};

// A connector from one item to another. Connectors also make the mindmap:
// an item's children are what its connectors point to.
struct BoardLink
{
    QString id;
    QString from;
    QString to;
    QString label;
    QString style = QStringLiteral("curve"); // curve, straight or elbow
    QString arrow = QStringLiteral("end");   // end, both or none
    QString color;                           // palette key
    bool dashed = false;
};

struct BoardState
{
    QList<BoardItem> items;
    QList<BoardLink> links;
};

// The items placed on the canvas of the currently open project.
//
// A project is a folder:
//   canvas.json      layout of every item, the connectors, and the last view
//   notes/<id>.md    one Markdown document per note
//   assets/          images and videos the notes embed
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
    // Every connector: id, from, to, label, style, arrow, color, dashed,
    // hidden (an end is in a folded branch)
    Q_PROPERTY(QVariantList links READ linksVariant NOTIFY linksChanged)
    // [[Wiki links]] between notes of this project: from, to
    Q_PROPERTY(QVariantList wikiEdges READ wikiEdges NOTIFY wikiEdgesChanged)

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
        ShapeRole,
        PointsRole,
        SizeRole,
        CollapsedRole,
        HiddenRole,      // inside a folded mindmap branch
        DescendantsRole, // how many items a fold hides
        ChildCountRole,
        SectionIdRole,   // the section the item lies in
        SectionTitleRole,
        SectionOrderRole, // that section's place in reading order
        ReadingOrderRole,
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
    int count() const { return m_state.items.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool hasView() const { return m_hasView; }
    double viewX() const { return m_viewX; }
    double viewY() const { return m_viewY; }
    double viewZoom() const { return m_viewZoom; }
    QVariantList linksVariant() const;
    QVariantList wikiEdges() const { return m_wikiEdges; }

    // Project lifecycle
    Q_INVOKABLE void load(const QString &projectDir);
    Q_INVOKABLE void close();
    Q_INVOKABLE void save();

    // Creating items. Media positions are the centre of the new item.
    Q_INVOKABLE int addNote(double x, double y, const QString &text = {});
    // type: section, shape, text; kind: the shape. Returns the row.
    Q_INVOKABLE int addItem(const QString &type, double x, double y, double w, double h,
                            const QString &kind = {}, const QString &text = {});
    // A pen or highlighter line through points (canvas coordinates)
    Q_INVOKABLE int addStroke(const QVariantList &points, const QString &kind, const QString &color, double width);
    // Media always goes into a note: into targetRow if that is a note,
    // otherwise into a new one centred on (cx, cy). Each returns the note's
    // row, or -1 if there was nothing to add.
    Q_INVOKABLE int addMedia(const QVariantList &paths, double cx, double cy, int targetRow = -1);
    Q_INVOKABLE int importUrls(const QVariantList &urls, double cx, double cy, int targetRow = -1);
    Q_INVOKABLE int addImageData(const QByteArray &data, double cx, double cy, int targetRow = -1);
    // Pastes items copied from a canvas, media, or text. Returns the ids
    // of what it added.
    Q_INVOKABLE QStringList pasteFromClipboard(double cx, double cy, int targetRow = -1);

    // Editing one item
    Q_INVOKABLE void setGeometry(int row, double x, double y, double w, double h);
    Q_INVOKABLE void setText(int row, const QString &text);
    Q_INVOKABLE void setColor(int row, const QString &color);
    Q_INVOKABLE void setShape(int row, const QString &shape);
    Q_INVOKABLE void setSize(int row, double size);
    Q_INVOKABLE void raise(int row);
    Q_INVOKABLE void lower(int row);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE int duplicate(int row);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Ticks or unticks the n-th "- [ ]" task of a note
    Q_INVOKABLE void toggleTask(int row, int task);

    // Editing several items at once, each one undo step
    Q_INVOKABLE void moveItems(const QVariantList &positions); // [{id, x, y}]
    Q_INVOKABLE void removeItems(const QStringList &ids);
    Q_INVOKABLE QStringList duplicateItems(const QStringList &ids);
    Q_INVOKABLE void setColorOf(const QStringList &ids, const QString &color);
    // left, hcenter, right, top, vcenter, bottom
    Q_INVOKABLE void align(const QStringList &ids, const QString &edge);
    Q_INVOKABLE void distribute(const QStringList &ids, bool horizontally);

    // Connectors
    Q_INVOKABLE QString addLink(const QString &from, const QString &to);
    Q_INVOKABLE void removeLink(const QString &id);
    // Any of label, style, arrow, color, dashed
    Q_INVOKABLE void setLink(const QString &id, const QVariantMap &values);

    // Mindmaps: each returns the new note's id
    Q_INVOKABLE QString addChild(const QString &parentId);
    Q_INVOKABLE QString addSibling(const QString &id);
    // Connected note at (x, y), as when a connector is dropped on empty space
    Q_INVOKABLE QString addConnectedNote(const QString &from, double x, double y);
    Q_INVOKABLE QString parentOf(const QString &id) const;
    Q_INVOKABLE QStringList childrenOf(const QString &id) const; // top to bottom
    Q_INVOKABLE void toggleCollapsed(const QString &id);
    // Lays the branch below `rootId` out as a tree to its right
    Q_INVOKABLE void tidy(const QString &rootId);

    // Queries
    Q_INVOKABLE int indexOf(const QString &id) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap getById(const QString &id) const;
    Q_INVOKABLE QRectF itemsBounds() const;
    Q_INVOKABLE QRectF boundsOf(const QStringList &ids) const;
    Q_INVOKABLE int itemAt(double x, double y) const; // topmost shown, or -1
    // Notes and shapes under (x, y); sections and strokes are skipped
    Q_INVOKABLE QString targetAt(double x, double y, const QString &except = {}) const;
    Q_INVOKABLE QStringList itemsIn(double x, double y, double w, double h) const;
    // What moves with a section: the items lying wholly inside it
    Q_INVOKABLE QStringList contentsOf(const QString &sectionId) const;
    // The note of this project with that title, or ""
    Q_INVOKABLE QString noteByTitle(const QString &title) const;

    // Clipboard
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE QString saveClipboardImage();
    Q_INVOKABLE QStringList clipboardMediaFiles() const;
    // One note, as rich text with its images for Teams and mail
    Q_INVOKABLE void copyToClipboard(int row) const;
    // Several items: to paste on a canvas, plus their text as rich text
    Q_INVOKABLE void copyItems(const QStringList &ids) const;
    Q_INVOKABLE bool copyImageFile(const QString &path) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE QString tempPath(const QString &name) const;
    Q_INVOKABLE bool copyFile(const QString &from, const QString &to) const;
    Q_INVOKABLE bool writePdf(const QString &imagePath, const QString &pdfPath) const;

    // Assets. mediaInfo copies an image or video into the project (if it is
    // not there yet) and returns rel, type ("image"/"video"), width, height.
    Q_INVOKABLE QVariantMap mediaInfo(const QString &pathOrUrl);
    Q_INVOKABLE bool isMediaFile(const QString &pathOrUrl) const;

    // A note for display: a list of
    //   {kind: "text", html}         rich text, images scaled to maxWidth
    //   {kind: "video", url, name}   a video on a line of its own
    //   {kind: "code", html, code, language}  a fenced code block
    //   {kind: "embed", target, line} an ![[embedded]] note preview
    // Tasks become task:<n> links, [[wiki links]] note:<title> links, and
    // occurrences of `highlight` get highlightColor behind them.
    Q_INVOKABLE QVariantList renderSegments(const QString &markdown, double maxWidth, int pixelSize,
                                            const QVariantMap &colors, const QStringList &highlight = {}) const;
    // Shows or hides the preview of the [[link]] on `line` of a note
    Q_INVOKABLE void setEmbed(int row, int line, bool embedded);
    Q_INVOKABLE QString toLocalPath(const QString &pathOrUrl) const;
    Q_INVOKABLE QString exportMarkdown();

    // Templates: built-in ones and those saved to the workspace
    Q_INVOKABLE QVariantList templates(const QString &workspaceRoot) const;
    Q_INVOKABLE QStringList insertTemplate(const QString &path, double cx, double cy);
    Q_INVOKABLE bool saveTemplate(const QStringList &ids, const QString &name, const QString &workspaceRoot);

    // Remembered view: world point at the centre of the viewport plus zoom
    Q_INVOKABLE void setView(double centerX, double centerY, double zoom);

signals:
    void projectChanged();
    void countChanged();
    void undoStateChanged();
    void viewRestored();
    void linksChanged();
    void wikiEdgesChanged();
    void saved();

public:
    // For the search, which reads the open project from memory: what is
    // on disk may be up to a save behind
    const QList<BoardItem> &items() const { return m_state.items; }
    static QString titleFor(const BoardItem &item);
    static QString titleForText(const QString &markdown);

private:
    bool valid(int row) const { return row >= 0 && row < m_state.items.size(); }
    void emitRow(int row);
    void emitAll(const QList<int> &roles = {});
    void markDirty();
    void pushUndo(const QString &coalesceKey = {});
    void restore(BoardState state);
    int appendItem(const BoardItem &item);
    void structureChanged(); // links or folds changed
    void textsChanged();     // wiki edges may have changed
    void updateDerived();
    QStringList importMedia(const QStringList &paths);
    int placeMediaNote(const QStringList &rels, double cx, double cy);
    void appendMediaToNote(int row, const QStringList &rels);
    double mediaHeight(const QString &rel, double width) const;
    static QString mediaMarkdown(const QString &rel);
    QString resolveLocal(const QString &target) const;
    QString renderHtml(const QString &markdown, double maxWidth, int pixelSize, const QVariantMap &colors,
                       bool forClipboard, const QStringList &highlight = {}, int firstTask = 0) const;
    QString dataUri(const QString &path) const;
    QString clipboardHtml(const QString &markdown) const;
    BoardItem makeNote(double x, double y, const QString &text) const;
    QStringList insertState(const BoardState &state, double dx, double dy);
    BoardState stateOf(const QStringList &ids) const;
    int topZ() const;
    int bottomZ() const;
    QString absPath(const QString &rel) const;
    QString importAsset(const QString &absPath);
    QString uniqueAssetPath(const QString &base, const QString &extension) const;
    QString saveImageAsset(const QImage &image, const QString &prefix);
    QSizeF displaySize(const QString &absPath, const QString &type) const;
    void adoptLooseNotes();
    static QString mediaType(const QString &path);
    static QJsonObject toJson(const BoardItem &item, bool inlineText);
    static BoardItem itemFromJson(const QJsonObject &o);
    static QJsonObject toJson(const BoardLink &link);
    static BoardLink linkFromJson(const QJsonObject &o);

    QString m_projectPath;
    BoardState m_state;
    QList<BoardItem> &m_items = m_state.items;
    QList<BoardLink> &m_links = m_state.links;
    QList<BoardState> m_undo;
    QList<BoardState> m_redo;
    QString m_lastUndoKey;
    QHash<QString, QString> m_savedTexts; // note id -> text last written to disk
    bool m_dirty = false;
    bool m_hasView = false;
    double m_viewX = 0;
    double m_viewY = 0;
    double m_viewZoom = 1;
    QTimer m_saveTimer;
    mutable QHash<QString, QSizeF> m_imageSizes;

    // Derived from items and links, rebuilt by updateDerived()
    QSet<QString> m_hidden;
    QHash<QString, int> m_descendants;
    QHash<QString, int> m_childCount;
    QHash<QString, QString> m_sectionOf;
    QHash<QString, int> m_sectionOrder;
    QHash<QString, int> m_readingOrder;
    QVariantList m_wikiEdges;
};
