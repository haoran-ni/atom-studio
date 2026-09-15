#pragma once
#include <QMainWindow>
namespace atom::ui {
class PythonShellController;
class PythonPackagesWindow : public QMainWindow {
public:
    explicit PythonPackagesWindow(PythonShellController* shell);
};
}
