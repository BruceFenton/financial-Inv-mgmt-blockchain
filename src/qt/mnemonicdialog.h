// Copyright (c) 2020 Hans Schmidt
// Copyright (c) 2017-2018 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_QT_MNEMONICDIALOG_H
#define PARTICL_QT_MNEMONICDIALOG_H

#include <QDialog>
#include <QFrame>
#include <QStackedLayout>
#include <string>

namespace Ui {
    class MnemonicDialog1;
    class MnemonicDialog2;
    class MnemonicDialog3; 
}

// =====================

class MnemonicDialog1 : public QFrame
{
    Q_OBJECT
public:
    explicit MnemonicDialog1(QWidget *parent);
    ~MnemonicDialog1();

public Q_SLOTS:
    void on_acceptButton_clicked();
    void on_walletNewRadio_clicked();
    void on_walletOldRadio_clicked();

Q_SIGNALS:             // "signals:" is not supported on older QT versions
    void updateMainWindowStackWidget(int ind);

private:
    Ui::MnemonicDialog1 *ui;
    std::string radioselected;
};

// ======================

class MnemonicDialog2 : public QFrame
{
    Q_OBJECT
public:
    explicit MnemonicDialog2(QWidget *parent);
    ~MnemonicDialog2();
    void GenerateWords(int languageSelected);

public Q_SLOTS:
    void on_acceptButton_clicked();
    void on_backButton_clicked();
    void on_generateButton_clicked();

Q_SIGNALS:             // "signals:" is not supported on older QT versions
    void updateMainWindowStackWidget(int ind);
    void allCloseRequested();

private:
    Ui::MnemonicDialog2 *ui;
};

// =====================

class MnemonicDialog3 : public QFrame
{
    Q_OBJECT
public:
    explicit MnemonicDialog3(QWidget *parent);
    ~MnemonicDialog3();

    bool eventFilter(QObject *obj, QEvent *ev);

public Q_SLOTS:
    void on_acceptButton_clicked();
    void on_backButton_clicked();

Q_SIGNALS:             // "signals:" is not supported on older QT versions
    void updateMainWindowStackWidget(int ind);
    void allCloseRequested();

private:
    Ui::MnemonicDialog3 *ui;
};

// ======================

class MnemonicDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MnemonicDialog(QWidget *parent = nullptr);

public Q_SLOTS:
    void onChangeWindowRequested(int index);
    void closeMainDialog();

private:
    QStackedLayout *stackedLayout;

};

#endif // PARTICL_QT_MNEMONICDIALOG_H
