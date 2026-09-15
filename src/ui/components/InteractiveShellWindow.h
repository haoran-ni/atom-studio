#pragma once
#include <QMainWindow>
class QPlainTextEdit;
class QLabel;
class QTableWidget;
class QPushButton;
class QSpinBox;
namespace atom::ui {
class PythonShellController;
class InteractiveShellWindow : public QMainWindow {
public:
    explicit InteractiveShellWindow(PythonShellController* controller);
private:
    void refresh();
    void refreshStructures();
    PythonShellController* m_controller;
    QPlainTextEdit* m_editor;
    QPlainTextEdit* m_output;
    QTableWidget* m_structures;
    QLabel* m_status;
    QLabel* m_directory;
    QPushButton* m_run;
    QPushButton* m_stop;
    QPushButton* m_restart;
    QPushButton* m_chooseDirectory;
    QSpinBox* m_fps;
};
}
