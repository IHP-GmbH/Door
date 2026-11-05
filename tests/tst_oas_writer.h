#ifndef TST_OAS_WRITER_H
#define TST_OAS_WRITER_H

#include <QtTest/QtTest>

class OasWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void oasCreate_createsMinimalValidFile();
    void oasCreate_invalidPath_reportsError();
    void oasCreate_secondTopCell_exercisesWriterEncoding();
    void readHierarchy_stdcellFixture_hasRealNamesNotPlaceholders();
    void readHierarchy_stdcellFixture_progressSkipsPlaceholders();
};

#endif // TST_OAS_WRITER_H
