#include <QtTest>

#include <QComboBox>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include "AcpAgentRegistry.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalConfigWidget.h"
#include "SendWithGoalDialog.h"

class TestSendWithGoalDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void changedAgent_isRestoredAfterCancel();
    void changedAgent_isRestoredAfterEscape();
    void changedAgent_isRestoredAfterAccept();
    void removedStoredAgent_fallsBackAndNormalizesSetting();

private:
    static QComboBox *agentCombo(SendWithGoalDialog &dialog);
    static QPushButton *dialogButton(SendWithGoalDialog &dialog, const QString &text);
    static QString storedAgentId(ApplicationSettings &settings);
    static void selectCodex(SendWithGoalDialog &dialog);

    QTemporaryDir m_settingsDir;
};

void TestSendWithGoalDialog::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NotepadNextTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NotepadNextTest_SendWithGoalDialog"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestSendWithGoalDialog::init()
{
    ApplicationSettings settings;
    settings.clear();
    settings.sync();
}

QComboBox *TestSendWithGoalDialog::agentCombo(SendWithGoalDialog &dialog)
{
    const auto combos = dialog.findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        if (combo->findData(AcpAgentRegistry::builtinClaudeCodeId()) >= 0
            && combo->findData(AcpAgentRegistry::builtinCodexId()) >= 0) {
            return combo;
        }
    }
    return nullptr;
}

QPushButton *TestSendWithGoalDialog::dialogButton(SendWithGoalDialog &dialog, const QString &text)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

QString TestSendWithGoalDialog::storedAgentId(ApplicationSettings &settings)
{
    const QString json = settings.get("Ai/GoalAgentSettings", QString());
    return GoalAgentSettings::fromJson(QJsonDocument::fromJson(json.toUtf8()).object()).agentId;
}

void TestSendWithGoalDialog::selectCodex(SendWithGoalDialog &dialog)
{
    QComboBox *combo = agentCombo(dialog);
    QVERIFY(combo);
    const int codexIndex = combo->findData(AcpAgentRegistry::builtinCodexId());
    QVERIFY(codexIndex >= 0);
    combo->setCurrentIndex(codexIndex);
}

void TestSendWithGoalDialog::changedAgent_isRestoredAfterCancel()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectCodex(first);
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinCodexId());
    QVERIFY(dialogButton(first, QStringLiteral("Cancel")));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Cancel")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Rejected);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(agentCombo(reopened)->currentData().toString(), AcpAgentRegistry::builtinCodexId());
}

void TestSendWithGoalDialog::changedAgent_isRestoredAfterEscape()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectCodex(first);
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinCodexId());
    QTest::keyClick(&first, Qt::Key_Escape);
    QCOMPARE(first.result(), QDialog::Rejected);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(agentCombo(reopened)->currentData().toString(), AcpAgentRegistry::builtinCodexId());
}

void TestSendWithGoalDialog::changedAgent_isRestoredAfterAccept()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectCodex(first);
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinCodexId());
    QVERIFY(dialogButton(first, QStringLiteral("Start Goal")));
    const auto criteria = first.findChildren<QPlainTextEdit *>();
    QVERIFY(!criteria.isEmpty());
    criteria.first()->setPlainText(QStringLiteral("Confirm selected agent"));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Start Goal")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Accepted);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(agentCombo(reopened)->currentData().toString(), AcpAgentRegistry::builtinCodexId());
}

void TestSendWithGoalDialog::removedStoredAgent_fallsBackAndNormalizesSetting()
{
    ApplicationSettings settings;
    GoalAgentSettings goalSettings;
    goalSettings.agentId = QStringLiteral("removed-agent");
    settings.setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog dialog(&registry, &settings);
    QCOMPARE(agentCombo(dialog)->currentData().toString(), AcpAgentRegistry::builtinClaudeCodeId());
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinClaudeCodeId());
}

QTEST_MAIN(TestSendWithGoalDialog)

#include "test_send_with_goal_dialog.moc"
