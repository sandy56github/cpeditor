/*
 * Copyright (C) 2019 Ashar Khan <ashar786khan@gmail.com>
 *
 * This file is part of CPEditor.
 *
 * CPEditor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * I will not be responsible if CPEditor behaves in unexpected way and
 * causes your ratings to go down and or loose any important contest.
 *
 * Believe Software is "Software" and it isn't not immune to bugs.
 *
 */

#include "mainwindow.hpp"

#include <Core.hpp>
#include <DiffViewer.hpp>
#include <MessageLogger.hpp>
#include <QCXXHighlighter>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPythonCompleter>
#include <QPythonHighlighter>
#include <QShortcut>
#include <QSyntaxStyle>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <expand.hpp>

#include "../ui/ui_mainwindow.h"

// ***************************** RAII  ****************************
MainWindow::MainWindow(int index, QString filePath, QWidget *parent)
    : QMainWindow(parent), windowIndex(index), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  setEditor();
  setupCore();
  runner->removeExecutable();

  if (!filePath.isEmpty()) {
    openFile = new QFile(filePath);
    openFile->open(QIODevice::ReadWrite | QFile::Text);
    if (openFile->isOpen()) {
      editor->setPlainText(openFile->readAll());
    } else {
      Log::MessageLogger::warn(
          "Loader",
          "The filepath was not loaded. Read/Write permission missing");
      openFile->close();
      delete openFile;
      openFile = nullptr;
    }
    loadTests();
  }
}

MainWindow::~MainWindow() {
  delete ui;
  delete editor;
  if (openFile != nullptr && openFile->isOpen())
    openFile->close();
  delete openFile;
  delete inputReader;
  delete formatter;
  delete compiler;
  delete runner;

  for (int i = 0; i < 3; ++i)
    delete expected[i];
}

// ************************* RAII HELPER *****************************

void MainWindow::setEditor() {
  editor = new QCodeEditor();
  editor->setMinimumWidth(500);
  editor->setMinimumHeight(300);

  editor->setSyntaxStyle(QSyntaxStyle::defaultStyle()); // default is white
  editor->setHighlighter(new QCXXHighlighter);
  editor->setAutoIndentation(true);
  editor->setAutoParentheses(true);
  editor->setWordWrapMode(QTextOption::NoWrap);

  ui->verticalLayout_8->addWidget(editor);

  input[0] = ui->in1;
  input[1] = ui->in2;
  input[2] = ui->in3;

  output[0] = ui->out1;
  output[1] = ui->out2;
  output[2] = ui->out3;

  verdict[0] = ui->out1_verdict;
  verdict[1] = ui->out2_verdict;
  verdict[2] = ui->out3_verdict;

  for (int i = 0; i < 3; ++i) {
    expected[i] = new QString;
    input[i]->setWordWrapMode(QTextOption::NoWrap);
    output[i]->setWordWrapMode(QTextOption::NoWrap);
  }

  QObject::connect(editor, SIGNAL(textChanged()), this,
                   SLOT(on_textChanged_triggered()));

  for (auto i : {0, 1, 2})
    updateVerdict(Core::Verdict::UNKNOWN, i);
}

void MainWindow::setupCore() {
  formatter = new Core::Formatter(formatCommand, windowIndex);
  inputReader = new Core::IO::InputReader(input, windowIndex);
  compiler = new Core::Compiler(compileCommand, windowIndex);
  runner =
      new Core::Runner(runCommand, compileCommand, prependCommand, windowIndex);

  QObject::connect(runner, SIGNAL(executionFinished(int, int, QString)), this,
                   SLOT(executionFinished(int, int, QString)));
}

void MainWindow::clearTests(bool outputOnly) {
  for (int i = 0; i < 3; ++i) {
    if (!outputOnly) {
      input[i]->clear();
      expected[i]->clear();
    }
    output[i]->clear();
    updateVerdict(Core::Verdict::UNKNOWN, i);
  }
}

void MainWindow::loadTests() {
  if (openFile == nullptr || !shouldSaveTests)
    return;

  QFileInfo fileInfo(*openFile);
  QString testFile =
      fileInfo.dir().absolutePath() + "/" + fileInfo.completeBaseName();

  for (int i = 0; i < 3; ++i) {
    if (QFile::exists(testFile + QString::number(i + 1) + ".in")) {
      QFile *inputFile = new QFile(testFile + QString::number(i + 1) + ".in");
      inputFile->open(QIODevice::ReadOnly | QFile::Text);
      if (inputFile->isOpen()) {
        input[i]->setPlainText(inputFile->readAll());
        inputFile->close();
        Log::MessageLogger::info("Tests", "Input #" + std::to_string(i + 1) +
                                              " successfully loaded");
      } else {
        Log::MessageLogger::error(
            "Tests", "Cannot Open" + inputFile->fileName().toStdString() +
                         ", Do I have read permissions?");
      }
      delete inputFile;
    }

    if (QFile::exists(testFile + QString::number(i + 1) + ".ans")) {
      QFile *answerFile = new QFile(testFile + QString::number(i + 1) + ".ans");
      answerFile->open(QIODevice::ReadOnly | QFile::Text);
      if (answerFile->isOpen()) {
        expected[i]->operator=(answerFile->readAll());
        answerFile->close();
        Log::MessageLogger::info("Tests", "Expected #" + std::to_string(i + 1) +
                                              " successfully loaded");
      } else {
        Log::MessageLogger::error(
            "Tests", "Cannot Open" + answerFile->fileName().toStdString() +
                         ", Do I have read permissions?");
      }
      delete answerFile;
    }
  }
}

void MainWindow::saveTests() {
  if (openFile == nullptr || !shouldSaveTests)
    return;

  QFileInfo fileInfo(*openFile);
  QString testFile =
      fileInfo.dir().absolutePath() + "/" + fileInfo.completeBaseName();

  for (int i = 0; i < 3; ++i) {
    if (!input[i]->toPlainText().trimmed().isEmpty()) {
      QFile *inputFile = new QFile(testFile + QString::number(i + 1) + ".in");
      inputFile->open(QIODevice::WriteOnly | QFile::Text);
      if (inputFile->isOpen()) {
        if (inputFile->write(input[i]->toPlainText().toStdString().c_str()) !=
            -1) {
          Log::MessageLogger::info("Tests", "Input #" + std::to_string(i + 1) +
                                                " successfully saved");
        } else {
          Log::MessageLogger::error("Tests", "Input #" + std::to_string(i + 1) +
                                                 " was not successfully saved");
        }
        inputFile->close();
      } else {
        Log::MessageLogger::error("Tests", "Cannot save Input #" +
                                               std::to_string(i + 1) +
                                               ", Do I have write permission?");
      }
      delete inputFile;
    }

    if (!expected[i]->trimmed().isEmpty()) {
      QFile *answerFile = new QFile(testFile + QString::number(i + 1) + ".ans");
      answerFile->open(QIODevice::WriteOnly | QFile::Text);
      if (answerFile->isOpen()) {
        if (answerFile->write(expected[i]->toStdString().c_str()) != -1) {
          Log::MessageLogger::info("Tests", "Expected #" +
                                                std::to_string(i + 1) +
                                                " successfully saved");
        } else {
          Log::MessageLogger::error("Tests", "Expected #" +
                                                 std::to_string(i + 1) +
                                                 " was not successfully saved");
        }
        answerFile->close();
      } else {
        Log::MessageLogger::error("Tests", "Cannot save Expected #" +
                                               std::to_string(i + 1) +
                                               ", Do I have write permission?");
      }
      delete answerFile;
    }
  }
}

QString MainWindow::fileName() const {
  return openFile == nullptr || !openFile->isOpen()
             ? "Unsaved file"
             : QFileInfo(*openFile).fileName();
}

void MainWindow::updateVerdict(Core::Verdict _verdict, int id) {
  QString verdict_text, style_sheet;

  switch (_verdict) {
  case Core::Verdict::ACCEPTED:
    verdict_text = "Verdict : AC";
    style_sheet = "QLabel { color : rgb(0, 180, 0); }";
    break;
  case Core::Verdict::WRONG_ANSWER:
    verdict_text = "Verdict : WA";
    style_sheet = "QLabel { color : rgb(255, 0, 0); }";
    break;
  case Core::Verdict::UNKNOWN:
    verdict_text = "Verdict : **";
    style_sheet = "";
    break;
  }

  verdict[id]->setText(verdict_text);
  verdict[id]->setStyleSheet(style_sheet);
}

void MainWindow::applyCompanion(Network::CompanionData data) {
  if (openFile == nullptr && !isTextChanged()) {
    QString meta = data.toMetaString();
    meta.prepend("\n");
    meta.append(
        "Powered by CP Editor (https://github.com/coder3101/cp-editor2)");

    if (language == "Python")
      meta.replace('\n', "\n# ");
    else
      meta.replace('\n', "\n// ");

    editor->setPlainText(meta + "\n\n" + editor->toPlainText());
  }

  clearTests();

  if (data.testcases.size() > 3) {
    Log::MessageLogger::warn(
        "CP Editor",
        "More than 3 testcase were produced. Only First 3 will be used");
  }

  for (int i = 0; i < data.testcases.size() && i < 3; ++i) {
    input[i]->setPlainText(data.testcases[i].input);
    expected[i]->operator=(data.testcases[i].output);
  }
}

void MainWindow::setCompileCommand(QString s) { compileCommand = s; }

void MainWindow::setFormatCommand(QString s) { formatCommand = s; }

void MainWindow::setRunCommand(QString s) { runCommand = s; }

void MainWindow::setPreprendRunCommand(QString s) { prependCommand = s; }

void MainWindow::setTemplatePath(QString s) { templatePath = s; }

void MainWindow::setSaveTests(bool b) { shouldSaveTests = b; }

void MainWindow::save() { saveFile(true, "Save"); }

void MainWindow::saveAs() {
  if (openFile == nullptr) {
    saveFile(true, "Save as");
  } else {
    auto oldFile = openFile;
    openFile->close();
    openFile = nullptr;
    if (!saveFile(true, "Save as")) {
      openFile = oldFile;
      openFile->open(QIODevice::ReadWrite | QFile::Text);
    } else {
      delete oldFile;
    }
  }
}

int MainWindow::windowIndeX() const { return windowIndex; }

void MainWindow::on_textChanged_triggered() {
  bool isChanged = isTextChanged();
}

void MainWindow::on_compile_clicked() {
  Log::MessageLogger::clear();
  saveFile(false, "Compiler");
  compiler->compile(editor, language);
}

void MainWindow::on_run_clicked() {
  Log::MessageLogger::clear();
  clearTests(true);
  saveFile(false, "Compiler");
  inputReader->readToFile();

  QVector<bool> isRun;
  for (int i = 0; i < 3; ++i) {
    output[i]->clear();
    isRun.push_back(!input[i]->toPlainText().trimmed().isEmpty());
  }

  runner->run(editor, isRun, language);
}

void MainWindow::run() { on_runOnly_clicked(); }

void MainWindow::runAndCompile() { on_run_clicked(); }

void MainWindow::compile() { on_compile_clicked(); }

void MainWindow::formatSource() { formatter->format(editor); }

void MainWindow::on_runOnly_clicked() {
  Log::MessageLogger::clear();
  clearTests(true);
  saveFile(false, "Compiler");
  inputReader->readToFile();

  QVector<bool> isRun;
  for (int i = 0; i < 3; ++i) {
    output[i]->clear();
    isRun.push_back(!input[i]->toPlainText().trimmed().isEmpty());
  }

  runner->run(isRun, language);
}

void MainWindow::detachedExecution() {
  Log::MessageLogger::clear();
  runner->runDetached(editor, language);
}

void MainWindow::killProcesses() { runner->killAll(); }

void MainWindow::executionFinished(int id, int msec, QString Stdout) {
  Log::MessageLogger::info("Runner[" + std::to_string(id + 1) + "]",
                           "Execution for case #" + std::to_string(id + 1) +
                               " completed and took " + std::to_string(msec) +
                               " miliseconds.");

  output[id]->clear();
  output[id]->setPlainText(Stdout);

  if (Stdout.isEmpty() || expected[id]->isEmpty())
    return;

  if (isVerdictPass(Stdout, *expected[id]))
    updateVerdict(Core::Verdict::ACCEPTED, id);
  else
    updateVerdict(Core::Verdict::WRONG_ANSWER, id);
}

// ****************************** Context Menus **************************/

void MainWindow::on_in1_customContextMenuRequested(const QPoint &pos) {
  QMenu *stdMenu = ui->in1->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(ui->in1);
    ptr->setTitle("Input 1");
    ptr->setUpdate(true);
    ptr->setReadFile(true);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->in1->viewport()->mapToGlobal(pos));
}

void MainWindow::on_in2_customContextMenuRequested(const QPoint &pos) {
  QMenu *stdMenu = ui->in2->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(ui->in2);
    ptr->setTitle("Input 2");
    ptr->setUpdate(true);
    ptr->setReadFile(true);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->in2->viewport()->mapToGlobal(pos));
}

void MainWindow::on_in3_customContextMenuRequested(const QPoint &pos) {
  QMenu *stdMenu = ui->in3->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(ui->in3);
    ptr->setTitle("Input 3");
    ptr->setUpdate(true);
    ptr->setReadFile(true);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->in3->viewport()->mapToGlobal(pos));
}

void MainWindow::on_compiler_edit_customContextMenuRequested(
    const QPoint &pos) {
  QMenu *stdMenu = ui->compiler_edit->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(this->ui->compiler_edit);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->compiler_edit->viewport()->mapToGlobal(pos));
}

void MainWindow::on_out1_customContextMenuRequested(const QPoint &pos) {
  QMenu *stdMenu = ui->out1->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(ui->out1);
    ptr->setTitle("Output 1");
    ptr->setUpdate(false);
    ptr->setReadFile(false);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->out1->viewport()->mapToGlobal(pos));
}
void MainWindow::on_out2_customContextMenuRequested(const QPoint &pos) {
  QMenu *stdMenu = ui->out2->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(ui->out2);
    ptr->setTitle("Output 2");
    ptr->setUpdate(false);
    ptr->setReadFile(false);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->out2->viewport()->mapToGlobal(pos));
}

void MainWindow::on_out3_customContextMenuRequested(const QPoint &pos) {
  QMenu *stdMenu = ui->out3->createStandardContextMenu(pos);
  QAction *newAction = new QAction("Expand");

  QObject::connect(newAction, &QAction::triggered, this, [this] {
    auto ptr = new Expand(ui->out3);
    ptr->setTitle("Output 3");
    ptr->setUpdate(false);
    ptr->setReadFile(false);
    ptr->show();
  });

  stdMenu->insertAction(stdMenu->actions().first(), newAction);
  stdMenu->popup(ui->out3->viewport()->mapToGlobal(pos));
}

//********************* DIFF Showers ******************

void MainWindow::on_out1_diff_clicked() {
  auto ptr = new DiffViewer(expected[0], ui->out1);
  ptr->setTitle("Diffviewer for Case #1");
  ptr->show();
}

void MainWindow::on_out2_diff_clicked() {
  auto ptr = new DiffViewer(expected[1], ui->out2);
  ptr->setTitle("Diffviewer for Case #2");
  ptr->show();
}

void MainWindow::on_out3_diff_clicked() {
  auto ptr = new DiffViewer(expected[2], ui->out3);
  ptr->setTitle("Diffviewer for Case #3");
  ptr->show();
}

//***************** HELPER FUNCTIONS *****************

bool MainWindow::isVerdictPass(QString output, QString expected) {
  output = output.remove('\r');
  expected = expected.remove('\r');
  auto a_lines = output.split('\n');
  auto b_lines = expected.split('\n');
  for (int i = 0; i < a_lines.size() || i < b_lines.size(); ++i) {
    if (i >= a_lines.size()) {
      if (b_lines[i].trimmed().isEmpty())
        continue;
      else
        return false;
    }
    if (i >= b_lines.size()) {
      if (a_lines[i].trimmed().isEmpty())
        continue;
      else
        return false;
    }
    auto a_words = a_lines[i].split(' ');
    auto b_words = b_lines[i].split(' ');
    for (int j = 0; j < a_words.size() || j < b_words.size(); ++j) {
      if (j >= a_words.size()) {
        if (b_words[j].trimmed().isEmpty())
          continue;
        else
          return false;
      }
      if (j >= b_words.size()) {
        if (a_words[j].trimmed().isEmpty())
          continue;
        else
          return false;
      }
      if (a_words[j] != b_words[j])
        return false;
    }
  }
  return true;
}

bool MainWindow::saveFile(bool force, std::string head) {
  if (openFile == nullptr) {
    if (force) {
      auto filename = QFileDialog::getSaveFileName(
          this, tr("Save File"), "",
          "Source Files (*.cpp *.hpp *.h *.cc *.cxx *.c *.py *.py3 *.java)");
      if (filename.isEmpty())
        return false;

      openFile = new QFile(filename);
      openFile->open(QIODevice::ReadWrite | QFile::Text);
      if (openFile->isOpen()) {
        openFile->resize(0);
        if (openFile->write(editor->toPlainText().toStdString().c_str()) != -1)
          Log::MessageLogger::info(
              head, "Saved file : " + openFile->fileName().toStdString());
        else
          Log::MessageLogger::warn(head, "File was not saved successfully");
        openFile->flush();
      } else {
        Log::MessageLogger::error(
            head, "Cannot Save file. Do I have write permission?");
      }
      saveTests();
    } else {
      return false;
    }
  } else {
    openFile->resize(0);
    openFile->write(editor->toPlainText().toStdString().c_str());
    openFile->flush();
    Log::MessageLogger::info(head, "Saved with file name " +
                                       openFile->fileName().toStdString());
    saveTests();
  }
  on_textChanged_triggered();
  return true;
}

bool MainWindow::isTextChanged() {
  if (openFile == nullptr) {
    if (templatePath.size() != 0 && QFile::exists(templatePath)) {
      QFile f(templatePath);
      f.open(QIODevice::ReadOnly | QFile::Text);
      return editor->toPlainText() != f.readAll();
    }
    return !editor->toPlainText().isEmpty();
  }
  if (openFile->isOpen()) {
    openFile->seek(0);
    return openFile->readAll() != editor->toPlainText();
  }
  return true;
}

bool MainWindow::closeChangedConfirm() {
  bool isChanged = isTextChanged();
  bool confirmed = !isChanged;
  if (!confirmed) {
    auto res = QMessageBox::warning(
        this, "Save?",
        fileName() + " has been modified.\nDo you want to save your changes?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (res == QMessageBox::Save)
      confirmed = saveFile(true, "Save");
    else if (res == QMessageBox::Discard)
      confirmed = true;
  }
  return confirmed;
}
