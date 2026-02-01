#ifndef TRANSLATORSETTINGS_H
#define TRANSLATORSETTINGS_H

#include <QDialog>
#include <QItemSelection>
#include <QSortFilterProxyModel>
#include "Settings.h"
#include "inventory/ModelManager.h"
#include "settings/RepositoryTableModel.h"

namespace Ui {
class TranslatorSettingsDialog;
}

class LLMInterface;

class TranslatorSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TranslatorSettingsDialog(QWidget *parent, Settings *settings, ModelManager *modelManager);
    ~TranslatorSettingsDialog();

protected:
    void showEvent(QShowEvent *ev);

private slots:
    void updateSettings();
    void applySettings();
    void revealSelectedModels();
    void deleteSelectedModels();
    void importModels();
    void updateModelActions();
    void updateRepoActions();

    void on_importRepo_clicked();
    void on_deleteRepo_clicked();

    void on_downloadButton_clicked();

    void on_getMoreButton_clicked();

    void on_llmProviderCombo_currentIndexChanged(int index);
    void on_llmRefreshModelsButton_clicked();
    void on_llmTestButton_clicked();
    void onModelsDiscovered(QStringList models);
    void onConnectionTestResult(bool success, QString message);

signals:
    void downloadModel(Model model);

private:

    Ui::TranslatorSettingsDialog *ui_;
    Settings *settings_;
    ModelManager *modelManager_;
    QSortFilterProxyModel modelProxy_;
    RepositoryTableModel repositoryModel_;
    LLMInterface *llmInterface_;
};

#endif // TRANSLATORSETTINGS_H
