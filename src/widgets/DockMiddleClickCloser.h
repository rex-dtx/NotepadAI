/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

class QDockWidget;
class QMainWindow;

namespace DockMiddleClickCloser {

// Install a shared event filter so middle-clicking the dock's title bar
// closes it, matching the editor-tab middle-click-to-close UX.
void install(QDockWidget *dock);

// Install a filter on the main window so middle-clicking a tab in Qt's
// internal dock tab bar (shown when docks are tabified) closes that dock.
void installTabBarFilter(QMainWindow *mainWindow);

} // namespace DockMiddleClickCloser
