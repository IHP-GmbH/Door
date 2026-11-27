#ifndef VIEW_TOOLS_H
#define VIEW_TOOLS_H

#include <QString>
#include <QVector>

class QWidget;
class Properties;

struct ViewToolEntry {
    QString name;
    QString path;
    bool    isDefault = false;
};

QString     viewToolsPropertyKey(const QString &groupName);
QVector<ViewToolEntry> loadViewTools(const Properties *properties, const QString &groupName);
void        saveViewTools(Properties *properties, const QString &groupName, const QVector<ViewToolEntry> &entries);
QString     resolveViewToolPath(const QVector<ViewToolEntry> &entries, QWidget *parent);
/*! Expand a Windows .lnk shortcut to its target path; otherwise return \a path unchanged. */
QString     resolveToolLaunchPath(const QString &path);

#endif // VIEW_TOOLS_H
