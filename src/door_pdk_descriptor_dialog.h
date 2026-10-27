#ifndef DOOR_PDK_DESCRIPTOR_DIALOG_H
#define DOOR_PDK_DESCRIPTOR_DIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QSyntaxHighlighter;
class QTimer;

/*!*********************************************************************************************************************
 * \brief Edit `.door-pdk.json` with JSON highlighting and a live relative-path filesystem tree.
 **********************************************************************************************************************/
class DoorPdkDescriptorDialog : public QDialog
{
    Q_OBJECT

public:
    DoorPdkDescriptorDialog(const QString &pdkRoot,
                            const QString &descriptorPath,
                            QWidget *parent = nullptr);

    /*! True if Save wrote the descriptor during this session. */
    bool saved() const { return m_saved; }

private slots:
    void onTextChanged();
    void refreshPathTree();
    void applyTemplate(int index);
    void saveDescriptor();
    void reloadFromDisk();

private:
    void loadFile();
    bool parseAndFillTree(const QString &text, QString *errorOut);

    QString m_pdkRoot;
    QString m_descriptorPath;
    bool m_saved = false;
    bool m_updatingEditor = false;

    QLabel *m_pathLabel = nullptr;
    QLabel *m_rulesLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QComboBox *m_templateCombo = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QTreeWidget *m_tree = nullptr;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QTimer *m_debounce = nullptr;
};

#endif // DOOR_PDK_DESCRIPTOR_DIALOG_H
