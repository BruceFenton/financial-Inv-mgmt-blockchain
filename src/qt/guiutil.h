// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_GUIUTIL_H
#define RAVEN_QT_GUIUTIL_H

#include "amount.h"
#include "fs.h"

#include <QEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QObject>
#include <QProgressBar>
#include <QString>
#include <QTableView>
#include <QLabel>

class QValidatedLineEdit;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QAbstractItemView;
class QDateTime;
class QFont;
class QLineEdit;
class QUrl;
class QWidget;
class QGraphicsDropShadowEffect;
QT_END_NAMESPACE

/** Utility functions used by the Raven Qt UI.
 */
namespace GUIUtil
{
    // Get the font for the sub labels
    QFont getSubLabelFont();
    QFont getSubLabelFontBolded();

    // Get the font for the main labels
    QFont getTopLabelFont();
    QFont getTopLabelFontBolded();
    QFont getTopLabelFont(int weight, int pxsize);

    QGraphicsDropShadowEffect* getShadowEffect();

    // Create human-readable string from date
    QString dateTimeStr(const QDateTime &datetime);
    QString dateTimeStr(qint64 nTime);

    // Return a monospace font
    QFont fixedPitchFont();

    // Set up widgets for address and amounts
    void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent);
    void setupAmountWidget(QLineEdit *widget, QWidget *parent);

    // Parse "raven:" URI into recipient object, return true on successful parsing
    bool parseRavenURI(const QUrl &uri, SendCoinsRecipient *out);
    bool parseRavenURI(QString uri, SendCoinsRecipient *out);
    QString formatRavenURI(const SendCoinsRecipient &info);

    // Returns true if given address+amount meets "dust" definition
    bool isDust(const QString& address, const CAmount& amount);

    // HTML escaping for rich text controls
    QString HtmlEscape(const QString& str, bool fMultiLine=false);
    QString HtmlEscape(const std::string& str, bool fMultiLine=false);

    /** Copy a field of the currently selected entry of a view to the clipboard. Does nothing if nothing
        is selected.
       @param[in] column  Data column to extract from the model
       @param[in] role    Data role to extract from the model
       @see  TransactionView::copyLabel, TransactionView::copyAmount, TransactionView::copyAddress
     */
    void copyEntryData(QAbstractItemView *view, int column, int role=Qt::EditRole);

    /** Return a field of the currently selected entry as a QString. Does nothing if nothing
        is selected.
       @param[in] column  Data column to extract from the model
       @see  TransactionView::copyLabel, TransactionView::copyAmount, TransactionView::copyAddress
     */
    QList<QModelIndex> getEntryData(QAbstractItemView *view, int column);

    void setClipboard(const QString& str);

    /** Get save filename, mimics QFileDialog::getSaveFileName, except that it appends a default suffix
        when no suffix is provided by the user.

      @param[in] parent  Parent window (or 0)
      @param[in] caption Window caption (or empty, for default)
      @param[in] dir     Starting directory (or empty, to default to documents directory)
      @param[in] filter  Filter specification such as "Comma Separated Files (*.csv)"
      @param[out] selectedSuffixOut  Pointer to return the suffix (file type) that was selected (or 0).
                  Can be useful when choosing the save file format based on suffix.
     */
    QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
        const QString &filter,
        QString *selectedSuffixOut);

    /** Get open filename, convenience wrapper for QFileDialog::getOpenFileName.

      @param[in] parent  Parent window (or 0)
      @param[in] caption Window caption (or empty, for default)
      @param[in] dir     Starting directory (or empty, to default to documents directory)
      @param[in] filter  Filter specification such as "Comma Separated Files (*.csv)"
      @param[out] selectedSuffixOut  Pointer to return the suffix (file type) that was selected (or 0).
                  Can be useful when choosing the save file format based on suffix.
     */
    QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
        const QString &filter,
        QString *selectedSuffixOut);

    /** Get connection type to call object slot in GUI thread with invokeMethod. The call will be blocking.

       @returns If called from the GUI thread, return a Qt::DirectConnection.
                If called from another thread, return a Qt::BlockingQueuedConnection.
    */
    Qt::ConnectionType blockingGUIThreadConnection();

    // Determine whether a widget is hidden behind other windows
    bool isObscured(QWidget *w);

    // Open debug.log
    void openDebugLogfile();

    // Open the config file
    bool openRavenConf();

    // Replace invalid default fonts with known good ones
    void SubstituteFonts(const QString& language);

    // Concatenate a string given the painter, static text width, left side of rect, and right side of rect
    // and which side the concatenated string is on (default left)
    void concatenate(QPainter* painter, QString& strToCon, int static_width, int left_side, int right_size);


    class SyncWarningMessage : public QDialog
    {
        Q_OBJECT

    public:
        explicit SyncWarningMessage(QWidget *parent = 0);

        bool showTransactionSyncWarningMessage();
    };


    /** Qt event filter that intercepts ToolTipChange events, and replaces the tooltip with a rich text
      representation if needed. This assures that Qt can word-wrap long tooltip messages.
      Tooltips longer than the provided size threshold (in characters) are wrapped.
     */
    class ToolTipToRichTextFilter : public QObject
    {
        Q_OBJECT

    public:
        explicit ToolTipToRichTextFilter(int size_threshold, QObject *parent = 0);

    protected:
        bool eventFilter(QObject *obj, QEvent *evt);

    private:
        int size_threshold;
    };

    bool GetStartOnSystemStartup();
    bool SetStartOnSystemStartup(bool fAutoStart);

    /* Convert QString to OS specific boost path through UTF-8 */
    fs::path qstringToBoostPath(const QString &path);

    /* Convert OS specific boost path to QString through UTF-8 */
    QString boostPathToQString(const fs::path &path);

    /* Convert seconds into a QString with days, hours, mins, secs */
    QString formatDurationStr(int secs);

    /* Format CNodeStats.nServices bitmask into a user-readable string */
    QString formatServicesStr(quint64 mask);

    /* Format a CNodeCombinedStats.dPingTime into a user-readable string or display N/A, if 0*/
    QString formatPingTime(double dPingTime);

    /* Format a CNodeCombinedStats.nTimeOffset into a user-readable string. */
    QString formatTimeOffset(int64_t nTimeOffset);

    QString formatNiceTimeOffset(qint64 secs);

    QString formatBytes(uint64_t bytes);

    class ClickableLabel : public QLabel
    {
        Q_OBJECT

    Q_SIGNALS:
        /** Emitted when the label is clicked. The relative mouse coordinates of the click are
         * passed to the signal.
         */
        void clicked(const QPoint& point);
    protected:
        void mouseReleaseEvent(QMouseEvent *event);
    };
    
    class ClickableProgressBar : public QProgressBar
    {
        Q_OBJECT
        
    Q_SIGNALS:
        /** Emitted when the progressbar is clicked. The relative mouse coordinates of the click are
         * passed to the signal.
         */
        void clicked(const QPoint& point);
    protected:
        void mouseReleaseEvent(QMouseEvent *event);
    };

#if defined(Q_OS_MAC) && QT_VERSION >= 0x050000
    // workaround for Qt OSX Bug:
    // https://bugreports.qt-project.org/browse/QTBUG-15631
    // QProgressBar uses around 10% CPU even when app is in background
    class ProgressBar : public ClickableProgressBar
    {
        bool event(QEvent *e) {
            return (e->type() != QEvent::StyleAnimationUpdate) ? QProgressBar::event(e) : false;
        }
    };
#else
    typedef ClickableProgressBar ProgressBar;
#endif

    /**
     * Returns the start-moment of the day in local time.
     *
     * QDateTime::QDateTime(const QDate& date) is deprecated since Qt 5.15.
     * QDate::startOfDay() was introduced in Qt 5.14.
     */
    QDateTime StartOfDay(const QDate& date);


     /** 
     * Splits the string into substrings wherever separator occurs, and returns
     * the list of those strings. Empty strings do not appear in the result.
     *
     * QString::split() signature differs in different Qt versions:
     *  - QString::SplitBehavior is deprecated since Qt 5.15
     *  - Qt::SplitBehavior was introduced in Qt 5.14
     * If {QString|Qt}::SkipEmptyParts behavior is required, use this
     * function instead of QString::split().
     */
    template <typename SeparatorType>
    QStringList SplitSkipEmptyParts(const QString& string, const SeparatorType& separator)
    {
    #if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        return string.split(separator, Qt::SkipEmptyParts);
    #else
        return string.split(separator, QString::SkipEmptyParts);
    #endif
    }

     /**
     * Returns true if pixmap has been set.
     *
     * QPixmap* QLabel::pixmap() is deprecated since Qt 5.15.
     */
    bool HasPixmap(const QLabel* label);
    QImage GetImage(const QLabel* label);

} // namespace GUIUtil

#endif // RAVEN_QT_GUIUTIL_H
