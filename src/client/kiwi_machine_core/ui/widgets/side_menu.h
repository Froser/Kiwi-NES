// Copyright (C) 2023 Yisi Yu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#ifndef UI_WIDGETS_SIDE_MENU_H_
#define UI_WIDGETS_SIDE_MENU_H_

#include <kiwi_nes.h>

#include "models/nes_runtime.h"
#include "ui/widgets/widget.h"
#include "utility/timer.h"

class MainWindow;
class LocalizedStringUpdater;
class SideMenu : public Widget {
 public:
  explicit SideMenu(MainWindow* main_window, NESRuntimeID runtime_id);
  ~SideMenu() override;

 public:
  void AddMenu(std::unique_ptr<LocalizedStringUpdater> string_updater,
               kiwi::base::RepeatingClosure callback);
  void set_activate(bool activate) { activate_ = activate; }
  bool activate() { return activate_; }

 private:
  void Paint() override;
  bool OnKeyPressed(SDL_KeyboardEvent* event) override;
  bool OnControllerButtonPressed(SDL_ControllerButtonEvent* event) override;
  bool OnControllerAxisMotionEvents(SDL_ControllerAxisEvent* event) override;
  bool HandleInputEvents(SDL_KeyboardEvent* k, SDL_ControllerButtonEvent* c);

 private:
  MainWindow* main_window_ = nullptr;
  NESRuntime::Data* runtime_data_ = nullptr;
  std::vector<std::pair<std::unique_ptr<LocalizedStringUpdater>,
                        kiwi::base::RepeatingClosure>>
      menu_items_;
  int current_index_ = 0;
  bool activate_ = false;
};

#endif  // UI_WIDGETS_SIDE_MENU_H_