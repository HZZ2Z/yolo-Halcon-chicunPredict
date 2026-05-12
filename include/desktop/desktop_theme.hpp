#pragma once

#include <QString>

class QApplication;

namespace DesktopTheme {

void apply(QApplication& app);
QString appStyleSheet();
QString cardStyle(const QString& border_color = QString());

} // namespace DesktopTheme
